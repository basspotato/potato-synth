// Headless tests for the layer stack, run end to end through the plugin processor.
// Build with -DPOTATO_DEV_TOOLS=ON, run with ctest or ./build/PotatoSynthTests.
#include "PluginProcessor.h"
#include "Loudness.h"
#include "EngineRegistry.h"
#include "ChainKnobs.h"
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <thread>

namespace
{
int failures = 0;

#define CHECK(cond, what)                                                         \
    do {                                                                          \
        if (! (cond)) { ++failures; std::cerr << "FAIL: " << what << "\n"; }      \
        else          { std::cout << "ok:   " << what << "\n"; }                   \
    } while (false)

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

struct Rig
{
    std::unique_ptr<PotatoSynthProcessor> proc { dynamic_cast<PotatoSynthProcessor*> (createPluginFilter()) };
    juce::AudioBuffer<float> buffer { 2, blockSize };
    juce::MidiBuffer midi;

    // Voice tests run on a neutral rig: every layer's role is Full and mono bass is off, so band limits
    // don't colour them. The role and bus tests below switch these on explicitly.
    explicit Rig (bool neutral = true)
    {
        if (neutral)
        {
            for (int i = 0; i < ps::params::numLayers; ++i)
                set (ps::params::layerRole (i), (float) ps::params::Role::full);
            set (ps::params::monoBass, 0.0f);
        }
        // The plain reference voice on layer 3 only, whatever the plugin's default stack is
        for (int i = 0; i < ps::params::numLayers; ++i)
            proc->setLayerEngine (i, i == 2 ? "test.sine" : "");
        proc->setPlayConfigDetails (0, 2, sampleRate, blockSize);
        proc->prepareToPlay (sampleRate, blockSize);
    }

    // Renders numSamples, collecting the left channel. Events are (sample offset, message).
    std::vector<float> right;   // right channel of the last render

    std::vector<float> render (int numSamples, std::vector<std::pair<int, juce::MidiMessage>> events = {},
                               int block = blockSize)
    {
        std::vector<float> out;
        out.reserve ((size_t) numSamples);
        right.clear();
        juce::AudioBuffer<float> buf (2, block);
        for (int pos = 0; pos < numSamples; pos += block)
        {
            const int n = std::min (block, numSamples - pos);
            buf.setSize (2, n, false, false, true);
            midi.clear();
            for (auto& [at, msg] : events)
                if (at >= pos && at < pos + n)
                    midi.addEvent (msg, at - pos);
            proc->processBlock (buf, midi);
            for (int i = 0; i < n; ++i)
            {
                out.push_back (buf.getSample (0, i));
                right.push_back (buf.getSample (1, i));
            }
        }
        return out;
    }

    void collectRetired() { proc->collectRetiredEngines(); }

    // Sets a parameter in its own units (dB, ms, %, semitones, ...)
    void set (const juce::ParameterID& id, float value)
    {
        auto* p = proc->apvts.getParameter (id.getParamID());
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    float get (const juce::ParameterID& id) const
    {
        auto* p = proc->apvts.getParameter (id.getParamID());
        return p->convertFrom0to1 (p->getValue());
    }
};

float peak (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    float p = 0.0f;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

float maxStep (const std::vector<float>& x)
{
    float m = 0.0f;
    for (size_t i = 1; i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

bool allFinite (const std::vector<float>& x)
{
    return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); });
}

// Rising zero crossings with sub-sample interpolation: (crossings - 1) periods between the first and last one.
double estimateFrequency (const std::vector<float>& x, size_t from, size_t to)
{
    int crossings = 0;
    double first = 0.0, last = 0.0;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = (double) (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]);
            if (crossings++ == 0)
                first = t;
            last = t;
        }
    return crossings > 1 ? (crossings - 1) * sampleRate / (last - first) : 0.0;
}

float rms (const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    return (float) std::sqrt (sum / (double) (to - from));
}

// Strongest spectral line that is not a harmonic of f0 (below 20 kHz), relative to the fundamental (or to the
// strongest harmonic), in dB.
// Reads 16384 samples from `from`.
float worstAliasDb (const std::vector<float>& x, size_t from, double f0, bool relativeToStrongest = false)
{
    constexpr int order = 14, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (2 * size, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        // 4-term Blackman-Harris: sidelobes under -92 dB, so window leakage can't pose as aliasing
        const double t = juce::MathConstants<double>::twoPi * i / (size - 1);
        const auto w = (float) (0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t));
        data[(size_t) i] = x[from + (size_t) i] * w;
    }
    fft.performFrequencyOnlyForwardTransform (data.data());
    const double binHz = sampleRate / size;
    float fundamental = 0.0f, worstAlias = 0.0f;
    for (int bin = 1; bin < size / 2; ++bin)
    {
        const double f = bin * binHz;
        const double harmonic = f / f0;
        const bool nearHarmonic = std::abs (harmonic - std::round (harmonic)) * f0 < 40.0;   // main lobe is ±12 Hz
        if (std::abs (f - f0) < 40.0 || (relativeToStrongest && nearHarmonic))
            fundamental = std::max (fundamental, data[(size_t) bin]);
        else if (! nearHarmonic && f < 20000.0)
            worstAlias = std::max (worstAlias, data[(size_t) bin]);
    }
    return juce::Decibels::gainToDecibels (worstAlias / fundamental, -200.0f);
}

auto on (int note, float vel = 0.8f)  { return juce::MidiMessage::noteOn (1, note, vel); }
auto off (int note)                   { return juce::MidiMessage::noteOff (1, note); }
auto pedal (bool down)                { return juce::MidiMessage::controllerEvent (1, 64, down ? 127 : 0); }

void testNoteOnPitchAndRelease()
{
    Rig rig;
    auto x = rig.render (72000, { { 1000, on (69) }, { 24000, off (69) } });
    CHECK (peak (x, 0, 1000) <= 0.0f, "silent before the first note");
    CHECK (peak (x, 2000, 24000) > 0.1f, "note sounds on the default Mid layer");
    const double f = estimateFrequency (x, 4000, 24000);
    CHECK (std::abs (f - 440.0) < 0.1, "A4 plays at 440 Hz (measured " + juce::String (f, 2).toStdString() + ")");
    CHECK (peak (x, 24000 + 9600, 24000 + 14400) < peak (x, 4000, 24000) * 0.1f, "release falls 20 dB within 200 ms");
    CHECK (peak (x, 24000 + 43200, 72000) < 1.0e-4f, "note is fully gone 0.9 s after release");
    CHECK (maxStep (x) < 0.05f, "no clicks at note on or off");
}

void testSampleAccurateStart()
{
    Rig rig;
    auto x = rig.render (2048, { { 777, on (60) } });
    // The output is delayed by the reported latency (the host compensates for it)
    const auto start = (size_t) (777 + rig.proc->getLatencySamples());
    CHECK (peak (x, 0, start - 8) <= 0.0f && peak (x, start, start + 16) > 1.0e-4f, "note starts on its exact sample (after latency)");
    CHECK (peak (x, start - 8, start) < 1.0e-4f, "nothing leaks out ahead of it");
}

void testHold()
{
    namespace p = ps::params;
    Rig rig;
    rig.set (p::hold, 1.0f);
    auto x = rig.render (48000, { { 100, on (64) }, { 4800, off (64) }, { 9600, pedal (true) }, { 14400, pedal (false) } });
    CHECK (peak (x, 40000, 48000) > 0.1f, "HOLD keeps a released note ringing, through a real pedal press and lift");
    rig.set (p::hold, 0.0f);
    x = rig.render (48000);
    CHECK (peak (x, 43200, 48000) < 1.0e-4f, "switching HOLD off releases it");

    Rig pedalRig;
    x = pedalRig.render (48000, { { 0, pedal (true) }, { 100, on (64) }, { 4800, off (64) } });
    pedalRig.set (p::hold, 1.0f);
    x = pedalRig.render (4800);
    pedalRig.set (p::hold, 0.0f);
    x = pedalRig.render (48000);
    CHECK (peak (x, 40000, 48000) > 0.1f, "switching HOLD off leaves notes the real pedal still holds");
}

// CUTOFF / RESO: the stack's low-pass sweep on the main page
void testCutoffSweep()
{
    namespace p = ps::params;
    auto level = [] (int note, float cutoff, float reso)
    {
        Rig rig;
        rig.set (p::cutoff, cutoff);
        rig.set (p::resonance, reso);
        auto x = rig.render (24000, { { 0, on (note) } });
        return rms (x, 12000, 24000);
    };
    const float highOpen = level (95, 100.0f, 0.0f), highClosed = level (95, 0.0f, 0.0f);
    CHECK (juce::Decibels::gainToDecibels (highClosed / highOpen) < -30.0f, "CUTOFF closed takes a 2 kHz tone down by over 30 dB");
    const float subOpen = level (33, 100.0f, 0.0f), subClosed = level (33, 0.0f, 0.0f);
    CHECK (std::abs (juce::Decibels::gainToDecibels (subClosed / subOpen)) < 0.5f, "CUTOFF closed keeps a 55 Hz sub within 0.5 dB");
    const float at440 = 100.0f * (float) (std::log (440.0 / 180.0) / std::log (20000.0 / 180.0));
    CHECK (level (69, at440, 100.0f) > 2.0f * level (69, 100.0f, 0.0f), "RESO peaks at the cutoff");

    Rig rig;
    std::vector<float> x;
    rig.render (4800, { { 0, on (60) } });
    for (int step = 0; step <= 20; ++step)
    {
        rig.set (p::cutoff, 100.0f - 5.0f * (float) step);
        rig.set (p::resonance, 5.0f * (float) step);
        auto part = rig.render (2400);
        x.insert (x.end(), part.begin(), part.end());
    }
    rig.set (p::cutoff, 100.0f);
    auto part = rig.render (9600);
    x.insert (x.end(), part.begin(), part.end());
    CHECK (allFinite (x) && maxStep (x) < 0.05f, "sweeping CUTOFF and RESO in steps does not click");
}

// Pitch bend, MPE per-note bend, mod wheel and aftertouch
void testExpression()
{
    namespace p = ps::params;
    auto bend = [] (int channel, float amount)   // amount -1..1
    {
        return juce::MidiMessage::pitchWheel (channel, juce::jlimit (0, 16383, 8192 + juce::roundToInt (amount * 8191.0f)));
    };
    {
        Rig rig;
        auto x = rig.render (24000, { { 0, bend (1, 1.0f) }, { 100, on (69) } });
        const double f = estimateFrequency (x, 4000, 24000);
        CHECK (std::abs (f - 440.0 * std::exp2 (2.0 / 12.0)) < 0.2, "a full bend up raises a note 2 semitones (" + std::to_string (f) + " Hz)");
        rig.set (p::bendRange, 12.0f);
        x = rig.render (24000);
        const double g = estimateFrequency (x, 4000, 24000);
        CHECK (std::abs (g - 880.0) < 0.3, "with a 12 semitone range it raises an octave (" + std::to_string (g) + " Hz)");
    }
    // Every engine follows the bend (each adds it where it adds the layer tune)
    for (const char* id : { "jp", "sub", "wavetable", "spectral", "dirt" })
    {
        auto peakHz = [id, &bend] (float amount)
        {
            Rig rig;
            rig.proc->setLayerEngine (2, id);
            rig.render (512);
            rig.collectRetired();
            auto x = rig.render (40000, { { 0, bend (1, amount) }, { 0, on (57) } });
            constexpr int order = 15, size = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> data (2 * size, 0.0f);
            for (int i = 0; i < size; ++i)
                data[(size_t) i] = x[6000 + (size_t) i] * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / size));
            fft.performFrequencyOnlyForwardTransform (data.data());
            int best = 0;
            for (int b = (int) (90.0 * size / sampleRate); b < (int) (300.0 * size / sampleRate); ++b)
                if (data[(size_t) b] > data[(size_t) best])
                    best = b;
            return best * sampleRate / size;
        };
        const double ratio = peakHz (1.0f) / peakHz (0.0f);
        CHECK (std::abs (ratio / std::exp2 (2.0 / 12.0) - 1.0) < 0.01,
               std::string (id) + " follows the pitch bend (ratio " + std::to_string (ratio) + ")");
    }
    {
        // A bend sweep on a held note glides without steps
        Rig rig;
        std::vector<std::pair<int, juce::MidiMessage>> ev { { 0, on (57) } };
        for (int i = 0; i <= 64; ++i)
            ev.push_back ({ 4800 + i * 300, bend (1, (float) i / 64.0f) });
        auto x = rig.render (48000, ev);
        CHECK (allFinite (x) && maxStep (x) < 0.05f, "a pitch bend sweep does not click or zipper");
    }
    {
        // MPE: each note bends on its own channel; channel 1 bends them all
        Rig rig;
        rig.set (p::mpe, 1.0f);
        auto x = rig.render (24000, { { 0, bend (3, 1.0f) }, { 0, bend (2, 0.25f) }, { 100, juce::MidiMessage::noteOn (2, 69, 0.8f) } });
        double f = estimateFrequency (x, 4000, 24000);
        CHECK (std::abs (f - 880.0) < 0.5, "MPE: a quarter bend on the note's own channel is +12 semitones (" + std::to_string (f) + " Hz)");
        x = rig.render (24000, { { 0, bend (1, 1.0f) } });
        f = estimateFrequency (x, 4000, 24000);
        CHECK (std::abs (f - 880.0 * std::exp2 (2.0 / 12.0)) < 0.5, "MPE: channel 1 bends every note on top (" + std::to_string (f) + " Hz)");
    }
    {
        // The mod wheel moves MORPH by default; aftertouch goes where it is sent
        Rig rig;
        rig.render (9600, { { 0, juce::MidiMessage::controllerEvent (1, 1, 127) }, { 0, on (60) } });
        CHECK (rig.proc->getEffectiveKnob (ps::chain::morph) > 0.95f, "the mod wheel turns MORPH up");
        rig.set (p::aftertouchTarget, 12.0f);   // CUTOFF
        rig.set (p::aftertouchDepth, -100.0f);
        auto open = rig.render (9600);
        auto closed = rig.render (24000, { { 0, juce::MidiMessage::channelPressureChange (1, 127) } });
        CHECK (rms (closed, 12000, 24000) < 0.5f * rms (open, 0, 9600), "aftertouch on CUTOFF closes the filter");
    }
}

void testVoiceStealingAndChords()
{
    Rig rig;
    std::vector<std::pair<int, juce::MidiMessage>> ev;
    for (int i = 0; i < 24; ++i)
        ev.push_back ({ 100 + i * 50, on (40 + i, 0.5f) });
    auto x = rig.render (24000, ev);
    CHECK (allFinite (x), "24 held notes stay finite");
    CHECK (maxStep (x) < 0.25f, "stealing beyond 16 notes does not click hard");
}

void testSustainPedal()
{
    Rig rig;
    auto x = rig.render (96000, { { 0, pedal (true) }, { 100, on (64) }, { 4800, off (64) }, { 48000, pedal (false) } });
    CHECK (peak (x, 40000, 48000) > 0.1f, "pedal holds a released note");
    CHECK (peak (x, 48000 + 43200, 96000) < 1.0e-4f, "lifting the pedal releases it");
}

void testLayerMuteAndEmpty()
{
    Rig rig;
    auto* level = rig.proc->apvts.getParameter (ps::params::layerLevel (2).getParamID());
    level->setValueNotifyingHost (0.0f);   // -inf
    auto x = rig.render (24000, { { 0, on (69) } });
    CHECK (peak (x, 4800, 24000) <= 0.0f, "a muted layer is silent");

    level->setValueNotifyingHost (level->getDefaultValue());
    rig.proc->setLayerEngine (2, {});
    x = rig.render (24000, { { 0, on (72) } });
    CHECK (peak (x) <= 0.0f, "an empty stack is silent");
}

void testEngineSwapWhileSounding()
{
    Rig rig;
    auto x = rig.render (12000, { { 0, on (57) } });
    rig.proc->setLayerEngine (2, {});              // remove while sounding: old voices fade out
    auto y = rig.render (12000);
    x.insert (x.end(), y.begin(), y.end());
    CHECK (maxStep (x) < 0.05f, "removing an engine while it sounds does not click");
    CHECK (peak (y, 4800, 12000) <= 0.0f, "removed engine goes silent");

    rig.collectRetired();
    rig.proc->setLayerEngine (2, "test.sine");
    auto z = rig.render (12000, { { 100, on (57) } });
    CHECK (peak (z, 2000, 12000) > 0.1f, "a new engine plays after a swap");
}

void testLayersStack()
{
    Rig rig;
    auto one = rig.render (9600, { { 0, on (69, 0.5f) } });
    Rig rig2;
    rig2.proc->setLayerEngine (0, "test.sine");
    auto two = rig2.render (9600, { { 0, on (69, 0.5f) } });
    // Two identical layers add coherently (x2), and the stack's sqrt-weights scale two layers by 1/sqrt(2)
    CHECK (peak (two, 4800, 9600) > peak (one, 4800, 9600) * 1.9f / std::sqrt (2.0f), "two layers both sound on one note");
}

void testStateRoundTrip()
{
    Rig rig;
    rig.proc->setLayerEngine (0, "test.sine");
    rig.proc->setLayerEngine (2, {});
    rig.proc->apvts.getParameter (ps::params::layerLevel (0).getParamID())->setValueNotifyingHost (0.5f);
    juce::MemoryBlock state;
    rig.proc->getStateInformation (state);

    Rig other;
    other.proc->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (other.proc->getLayerEngine (0) == "test.sine" && other.proc->getLayerEngine (2).isEmpty(),
           "engines survive a save and reload");
    CHECK (std::abs (other.proc->apvts.getParameter (ps::params::layerLevel (0).getParamID())->getValue() - 0.5f) < 1.0e-4f,
           "layer levels survive a save and reload");
}

void testEngineSlots()
{
    namespace p = ps::params;
    {
        Rig rig;
        rig.set (p::layerSlot (2, 0), 1.0f);
        auto x = rig.render (24000, { { 0, on (69) } });
        CHECK (std::abs (estimateFrequency (x, 4000, 24000) - 1760.0) < 0.5, "engine slot 1 reaches the engine (+2 oct)");
        rig.set (p::layerSlot (2, 0), 0.25f);
        auto y = rig.render (24000);
        CHECK (std::abs (estimateFrequency (y, 4000, 24000) - 220.0) < 0.2, "slot changes reach held notes (-1 oct)");
    }
    {
        Rig rig;
        auto* slot = rig.proc->apvts.getParameter (p::layerSlot (2, 0).getParamID());
        CHECK (slot->getText (1.0f, 32) == "+2 oct" && slot->getText (0.5f, 32) == "0 oct",
               "slot value text comes from the engine");
        CHECK (rig.proc->apvts.getParameter (p::layerSlot (2, 5).getParamID())->getText (0.3f, 32) == "-",
               "slots the engine does not use read as unused");
        CHECK (rig.proc->apvts.getParameter (p::layerSlot (0, 0).getParamID())->getText (0.5f, 32) == "-",
               "an empty layer's slots read as unused");

        rig.set (p::layerSlot (2, 0), 1.0f);
        rig.proc->setLayerEngine (2, "test.sine");
        CHECK (std::abs (rig.get (p::layerSlot (2, 0)) - 0.5f) < 1.0e-6f, "picking an engine resets its slots to the engine's defaults");
    }
    {
        Rig rig;
        rig.set (p::layerSlot (2, 0), 0.75f);
        juce::MemoryBlock state;
        rig.proc->getStateInformation (state);
        Rig other;
        other.proc->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (std::abs (other.get (p::layerSlot (2, 0)) - 0.75f) < 1.0e-4f, "slot values survive a save and reload");
        auto x = other.render (24000, { { 0, on (69) } });
        CHECK (std::abs (estimateFrequency (x, 4000, 24000) - 880.0) < 0.5, "a reloaded slot value reaches the engine");
    }
}

void testAttackDecaySustain()
{
    Rig rig;
    rig.set (ps::params::layerAttack (2), 500.0f);
    rig.set (ps::params::layerDecay (2), 200.0f);
    rig.set (ps::params::layerSustain (2), 25.0f);
    auto x = rig.render (96000, { { 0, on (69) } });
    const float early = rms (x, 2400, 2400 + 480);       // 50 ms in
    const float top   = peak (x, 23000, 25000);          // end of the attack
    const float held  = rms (x, 72000, 96000) * std::sqrt (2.0f);
    CHECK (early < top * 0.4f, "a 500 ms attack starts slowly");
    CHECK (std::abs (held / top - 0.25f) < 0.02f, "decay settles on the 25% sustain level (got " + std::to_string (held / top) + ")");
    CHECK (maxStep (x) < 0.05f, "attack and decay are click-free");
}

void testReleaseTime()
{
    Rig rig;
    rig.set (ps::params::layerRelease (2), 2000.0f);
    auto x = rig.render (192000, { { 0, on (69) }, { 24000, off (69) } });
    const float held = peak (x, 12000, 24000);
    CHECK (peak (x, 24000 + 24000, 24000 + 26400) > held * 0.05f, "a 2 s release still sounds 0.5 s after note off");
    CHECK (peak (x, 24000 + 100000, 192000) < 1.0e-4f, "and has finished after 2 s");
}

void testRetriggerDuringRelease()
{
    Rig rig;
    rig.set (ps::params::layerRelease (2), 1000.0f);
    auto x = rig.render (48000, { { 0, on (64) }, { 12000, off (64) }, { 18000, on (64) }, { 19000, on (67) } });
    CHECK (maxStep (x) < 0.05f, "re-pressing a releasing note is click-free");
    CHECK (peak (x, 40000, 48000) > 0.1f,
           "the retriggered note sustains again");
}

void testPan()
{
    Rig rig;
    auto centre = rig.render (9600, { { 0, on (69) } });
    auto centreRight = rig.right;
    rig.set (ps::params::layerPan (2), -1.0f);
    auto left = rig.render (24000);
    CHECK (std::abs (peak (left, 12000, 24000) - peak (centre, 4800, 9600)) < 0.01f, "hard left keeps the left level (no boost)");
    CHECK (peak (rig.right, 12000, 24000) < 1.0e-4f && peak (centreRight, 4800, 9600) > 0.1f, "hard left silences the right channel");
    CHECK (maxStep (rig.right) < 0.05f, "moving pan is click-free");
}

void testTune()
{
    auto measure = [] (float oct, float semi, float fine)
    {
        Rig rig;
        rig.set (ps::params::layerOctave (2), oct);
        rig.set (ps::params::layerSemi (2), semi);
        rig.set (ps::params::layerFine (2), fine);
        auto x = rig.render (24000, { { 0, on (69) } });
        return estimateFrequency (x, 4000, 24000);
    };
    CHECK (std::abs (measure (1, 0, 0) - 880.0) < 0.2, "octave +1 doubles the pitch");
    CHECK (std::abs (measure (0, 7, 0) - 659.255) < 0.2, "semitone +7 plays a fifth");
    CHECK (std::abs (measure (0, 0, 50) - 452.893) < 0.2, "fine +50 ct plays a quarter tone up");
    CHECK (std::abs (measure (-1, -12, -100) - 103.826) < 0.1, "tune adds up downwards too (-25 semitones)");

    Rig rig;
    auto x = rig.render (12000, { { 0, on (69) } });
    rig.set (ps::params::layerSemi (2), 12.0f);
    auto y = rig.render (24000);
    x.insert (x.end(), y.begin(), y.end());
    CHECK (std::abs (estimateFrequency (y, 4000, 24000) - 880.0) < 0.2, "tune changes reach held notes");
    CHECK (maxStep (x) < 0.05f, "a tune change on a held note does not click");
}

void testOldStateLoadsDefaults()
{
    Rig source;
    juce::MemoryBlock block;
    source.proc->getStateInformation (block);
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    auto state = juce::ValueTree::fromXml (*xml);
    // Strip it down to a version 1 save: master and layer levels only
    for (int i = state.getNumChildren(); --i >= 0;)
    {
        const auto id = state.getChild (i).getProperty ("id").toString();
        if (state.getChild (i).hasType ("PARAM") && id != "master" && ! id.endsWith ("_level"))
            state.removeChild (i, nullptr);
    }
    state.setProperty ("stateVersion", 1, nullptr);
    juce::MemoryBlock v1;
    juce::AudioProcessor::copyXmlToBinary (*state.createXml(), v1);

    Rig rig;
    rig.set (ps::params::layerAttack (2), 5000.0f);
    rig.set (ps::params::layerPan (0), 0.5f);
    rig.proc->setStateInformation (v1.getData(), (int) v1.getSize());
    CHECK (std::abs (rig.get (ps::params::layerAttack (2)) - 2.0f) < 0.01f && std::abs (rig.get (ps::params::layerPan (0))) < 1.0e-4f,
           "a version 1 save loads new parameters at their defaults");
    auto x = rig.render (9600, { { 0, on (69) } });
    CHECK (peak (x, 480, 9600) > 0.1f, "and plays");
}

constexpr float poly = 0.0f, mono = 1.0f, legato = 2.0f;

void testMonoLastNotePriority()
{
    Rig rig;
    rig.set (ps::params::layerVoiceMode (2), mono);
    auto x = rig.render (96000, { { 0, on (57) }, { 24000, on (64) }, { 48000, off (64) }, { 72000, off (57) } });
    CHECK (std::abs (estimateFrequency (x, 4000, 24000) - 220.0) < 0.1, "mono plays the first note");
    CHECK (std::abs (estimateFrequency (x, 28000, 48000) - 329.628) < 0.1, "a second key takes over (one pitch, no chord)");
    CHECK (peak (x, 28000, 48000) < peak (x, 4000, 24000) * 1.1f, "mono never stacks two voices");
    CHECK (std::abs (estimateFrequency (x, 52000, 72000) - 220.0) < 0.1, "releasing it returns to the held key");
    CHECK (peak (x, 72000 + 43200, 96000) < 1.0e-4f, "releasing the last key ends the note");
    CHECK (maxStep (x) < 0.05f, "mono note changes are click-free");
}

void testLegatoDoesNotRetrigger()
{
    auto run = [] (float mode)
    {
        Rig rig;
        rig.set (ps::params::layerVoiceMode (2), mode);
        rig.set (ps::params::layerDecay (2), 100.0f);
        rig.set (ps::params::layerSustain (2), 25.0f);
        return rig.render (48000, { { 0, on (60) }, { 24000, on (62) } });
    };
    auto leg = run (legato);
    auto mon = run (mono);
    const float sustained = peak (leg, 20000, 24000);
    CHECK (peak (leg, 24000, 26400) < sustained * 1.1f, "legato keeps the envelope where it is");
    CHECK (peak (mon, 24000, 26400) > sustained * 3.0f, "mono retriggers the envelope");
    CHECK (maxStep (leg) < 0.05f && maxStep (mon) < 0.05f, "both are click-free");
}

void testGlide()
{
    Rig rig;
    rig.set (ps::params::layerVoiceMode (2), mono);
    rig.set (ps::params::layerGlide (2), 200.0f);
    auto x = rig.render (72000, { { 0, on (57) }, { 24000, on (69) } });
    const double mid = estimateFrequency (x, 24000 + 4320, 24000 + 5280);   // around 100 ms into the glide
    CHECK (mid > 280.0 && mid < 340.0, "glide is halfway (in pitch) halfway through (" + std::to_string (mid) + " Hz)");
    CHECK (std::abs (estimateFrequency (x, 24000 + 9800, 72000) - 440.0) < 0.1, "glide lands exactly on the note after its time");
    CHECK (maxStep (x) < 0.05f, "glide is click-free");

    Rig leg;
    leg.set (ps::params::layerVoiceMode (2), legato);
    leg.set (ps::params::layerGlide (2), 200.0f);
    auto y = leg.render (48000, { { 0, on (57) }, { 12000, off (57) }, { 24000, on (69) } });
    CHECK (std::abs (estimateFrequency (y, 24000 + 500, 24000 + 4000) - 440.0) < 0.5, "legato does not glide between separate notes");
}

void testPolyGlide()
{
    Rig rig;
    rig.set (ps::params::layerGlide (2), 300.0f);
    auto x = rig.render (72000, { { 0, on (57) }, { 12000, off (57) }, { 24000, on (69) } });
    CHECK (estimateFrequency (x, 24000 + 480, 24000 + 2400) < 300.0, "poly glide starts a new voice from the last note");
    CHECK (std::abs (estimateFrequency (x, 24000 + 15000, 72000) - 440.0) < 0.1, "and lands on its own note");
}

void testVoiceModeSwitchWhileSounding()
{
    Rig rig;
    auto x = rig.render (12000, { { 0, on (60) }, { 10, on (64) }, { 20, on (67) } });
    rig.set (ps::params::layerVoiceMode (2), mono);
    auto y = rig.render (24000, { { 6000, on (72) } });
    rig.set (ps::params::layerVoiceMode (2), poly);
    auto z = rig.render (24000, { { 100, off (60) }, { 200, off (64) }, { 300, off (67) }, { 400, off (72) } });
    x.insert (x.end(), y.begin(), y.end());
    x.insert (x.end(), z.begin(), z.end());
    CHECK (maxStep (x) < 0.05f, "switching voice mode while playing is click-free");
    CHECK (std::abs (estimateFrequency (y, 8000, 24000) - 523.251) < 0.1, "mono plays after a switch from poly");
    CHECK (peak (z, 12000, 24000) < 1.0e-4f, "no voice hangs after switching back");
}

// Peak level of one note in dB, after it settles, on the given layer and role
float noteLevelDb (int note, float role, int layer = 2, bool monoBassOn = false)
{
    Rig rig;
    if (layer != 2)
    {
        rig.proc->setLayerEngine (2, {});
        rig.proc->setLayerEngine (layer, "test.sine");
    }
    rig.set (ps::params::layerRole (layer), role);
    rig.set (ps::params::monoBass, monoBassOn ? 1.0f : 0.0f);
    auto x = rig.render (24000, { { 0, on (note) } });
    return juce::Decibels::gainToDecibels (peak (x, 12000, 24000), -200.0f);
}

void testRoleBands()
{
    using R = ps::params::Role;
    const float full110 = noteLevelDb (33, (float) R::full), full880 = noteLevelDb (81, (float) R::full);
    const float full4k = noteLevelDb (107, (float) R::full);
    CHECK (std::abs (noteLevelDb (33, (float) R::sub) - full110) < 0.5f, "Sub passes 55 Hz");
    CHECK (noteLevelDb (81, (float) R::sub) < full880 - 40.0f, "Sub removes 880 Hz (LR4 at 120 Hz)");
    CHECK (noteLevelDb (33, (float) R::body) < full110 - 20.0f, "Body removes 55 Hz");
    CHECK (std::abs (noteLevelDb (69, (float) R::body) - noteLevelDb (69, (float) R::full)) < 1.0f, "Body passes 440 Hz");
    CHECK (noteLevelDb (107, (float) R::body) < full4k - 6.0f, "Body rolls off above the high crossover");
    CHECK (noteLevelDb (45, (float) R::mid) < noteLevelDb (45, (float) R::full) - 20.0f, "Mid removes 110 Hz");
    CHECK (std::abs (noteLevelDb (81, (float) R::mid) - full880) < 0.5f, "Mid passes 880 Hz");
    CHECK (noteLevelDb (69, (float) R::air) < noteLevelDb (69, (float) R::full) - 40.0f, "Air removes 440 Hz");
    CHECK (std::abs (noteLevelDb (111, (float) R::air) - noteLevelDb (111, (float) R::full)) < 1.0f, "Air passes 5 kHz");
}

void testAddingALayerKeepsThePower()
{
    // Two layers a fifth apart (uncorrelated): the stack's sqrt-weights keep the summed power of one layer, and
    // fading the second layer in is smooth
    Rig one;
    auto a = one.render (24000, { { 0, on (57) } });
    Rig two;
    two.proc->setLayerEngine (1, "test.sine");
    two.set (ps::params::layerSemi (1), 7.0f);
    two.set (ps::params::layerLevel (1), -50.0f);   // quiet but not muted, so it starts the note
    two.render (blockSize);
    two.collectRetired();
    auto b = two.render (12000, { { 0, on (57) } });
    std::vector<float> fade;
    for (int i = 0; i < 48; ++i)
    {
        two.set (ps::params::layerLevel (1), -50.0f + 50.0f * (float) (i + 1) / 48.0f);
        auto x = two.render (blockSize);
        fade.insert (fade.end(), x.begin(), x.end());
    }
    auto c = two.render (24000);
    const float change = juce::Decibels::gainToDecibels (rms (c, 12000, 24000) / rms (a, 12000, 24000));
    CHECK (std::abs (change) < 0.3f, "adding an uncorrelated layer keeps the stack's power (" + std::to_string (change) + " dB)");
    CHECK (maxStep (fade) < 0.05f, "fading a layer in is click-free");
}

void testSubAndBodySumFlat()
{
    // At the crossover the LR4 low-pass (Sub) and high-pass (Body) add back to the full signal, in phase
    for (int note : { 40, 47, 52 })   // 82, 123, 165 Hz: below, at and above the 120 Hz crossover
    {
        Rig split;
        split.proc->setLayerEngine (2, {});
        split.proc->setLayerEngine (0, "test.sine");
        split.proc->setLayerEngine (1, "test.sine");
        split.set (ps::params::layerRole (0), (float) ps::params::Role::sub);
        split.set (ps::params::layerRole (1), (float) ps::params::Role::body);
        auto x = split.render (24000, { { 0, on (note) } });
        // Two active layers: the stack's sqrt-weights scale the sum by 1/sqrt(2) at every note
        const float full = juce::Decibels::decibelsToGain (noteLevelDb (note, (float) ps::params::Role::full)) / std::sqrt (2.0f);
        const float sum = peak (x, 12000, 24000);
        CHECK (std::abs (juce::Decibels::gainToDecibels (sum / full)) < 0.3f,
               "Sub + Body sum flat at note " + std::to_string (note) + " (" + std::to_string (juce::Decibels::gainToDecibels (sum / full)) + " dB)");
    }
}

void testRoleChangeWhilePlaying()
{
    Rig rig;
    auto x = rig.render (12000, { { 0, on (69) } });
    rig.set (ps::params::layerRole (2), (float) ps::params::Role::air);
    auto y = rig.render (12000);
    rig.set (ps::params::layerRole (2), (float) ps::params::Role::full);
    rig.set (ps::params::crossoverHigh, 1000.0f);
    auto z = rig.render (12000);
    x.insert (x.end(), y.begin(), y.end());
    x.insert (x.end(), z.begin(), z.end());
    CHECK (maxStep (x) < 0.05f, "changing role while a note plays is click-free");
    CHECK (peak (y, 4800, 12000) < peak (x, 4800, 12000) * 0.01f, "the new role's band applies within 100 ms");
    CHECK (peak (z, 4800, 12000) > peak (x, 4800, 12000) * 0.95f, "and switching back restores it");
}

void testMonoBass()
{
    auto run = [] (int note, bool monoBassOn)
    {
        Rig rig;
        rig.set (ps::params::monoBass, monoBassOn ? 1.0f : 0.0f);
        rig.set (ps::params::layerPan (2), -1.0f);   // all signal on the left
        auto x = rig.render (24000, { { 0, on (note) } });
        return std::pair<float, float> (peak (x, 12000, 24000), peak (rig.right, 12000, 24000));
    };
    // Well below the 120 Hz crossover the LR4 low band is the whole signal (at 27.5 Hz the high band is -51 dB);
    // nearer the crossover a little stays stereo, as with any crossover slope.
    const auto [l27, r27] = run (21, true);
    CHECK (std::abs (l27 - r27) < 0.02f * l27, "mono bass puts 27.5 Hz equally in both channels");
    CHECK (std::abs (run (21, false).first * 0.5f - l27) < 0.02f * l27, "summing to mono halves a hard-panned low (no level jump for centred lows)");
    const auto [l55, r55] = run (33, true);
    CHECK (r55 > 0.85f * l55, "55 Hz is mostly mono too");
    const auto [l2k, r2k] = run (95, true);
    CHECK (r2k < l2k * 0.001f, "mono bass leaves 1.8 kHz stereo");

    Rig rig (false);
    auto x = rig.render (12000, { { 0, on (33) } });
    rig.set (ps::params::monoBass, 0.0f);
    auto y = rig.render (12000);
    rig.set (ps::params::crossoverLow, 60.0f);
    rig.set (ps::params::monoBass, 1.0f);
    auto z = rig.render (12000);
    x.insert (x.end(), y.begin(), y.end());
    x.insert (x.end(), z.begin(), z.end());
    CHECK (maxStep (x) < 0.05f, "toggling mono bass and moving the crossover are click-free");
}

void testDefaultStackIsTrueSilentAfterNotes()
{
    Rig rig (false);
    auto x = rig.render (96000, { { 0, on (45) }, { 10, on (69) }, { 9600, off (45) }, { 9600, off (69) } });
    CHECK (peak (x, 72000, 96000) <= 0.0f, "filters stop once their tails die: the output returns to true zero");
}

void testMeters()
{
    std::vector<float> x (48000 * 5);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (double) i / sampleRate);
    const double lufs = ps::loudness::integratedLufs (x.data(), x.data(), x.size(), sampleRate);
    CHECK (std::abs (lufs + 20.0) < 0.1, "a -20 dBFS 1 kHz sine in both channels measures -20 LUFS (" + std::to_string (lufs) + ")");

    // A sine at a quarter of the sample rate, sampled 45 degrees off its peaks: sample peak -3 dB, true peak 0 dB
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = (float) std::sin (juce::MathConstants<double>::halfPi * (double) i + juce::MathConstants<double>::pi / 4.0);
    const double tp = ps::loudness::truePeakDb (x.data(), x.size());
    CHECK (std::abs (tp) < 0.2, "true peak finds the peak between samples (" + std::to_string (tp) + " dBTP)");
}

// Renders a loud 8-note chord on all four layers, master boosted by the given dB
std::pair<std::vector<float>, std::vector<float>> loudChord (float masterDb, bool safe)
{
    Rig rig;
    for (int i = 0; i < 4; ++i)
        rig.proc->setLayerEngine (i, "test.sine");
    rig.collectRetired();
    rig.set (ps::params::master, masterDb);
    rig.set (ps::params::safeOutput, safe ? 1.0f : 0.0f);
    std::vector<std::pair<int, juce::MidiMessage>> chord;
    for (int n : { 48, 52, 55, 60, 64, 67, 71, 72 })
        chord.push_back ({ 0, on (n, 1.0f) });
    auto l = rig.render (48000, chord);
    return { l, rig.right };
}

void testTruePeakSafeOutput()
{
    const auto [rawL, rawR] = loudChord (6.0f, false);
    const auto [l, r] = loudChord (6.0f, true);
    const double rawTp = ps::loudness::truePeakDb (rawL.data(), rawL.size());
    const double tp = std::max (ps::loudness::truePeakDb (l.data(), l.size()), ps::loudness::truePeakDb (r.data(), r.size()));
    CHECK (rawTp > 6.0, "without safe output the loud chord goes well over 0 dBFS (" + std::to_string (rawTp) + " dBTP)");
    CHECK (tp <= -1.0, "safe output holds it under -1 dBTP (" + std::to_string (tp) + " dBTP)");
    CHECK (allFinite (l) && allFinite (r), "and stays finite");
}

void testSafeOutputIsTransparentBelowTheKnee()
{
    auto run = [] (bool safe)
    {
        Rig rig;
        rig.set (ps::params::safeOutput, safe ? 1.0f : 0.0f);
        return rig.render (24000, { { 0, on (69, 0.5f) } });   // about -18 dBFS
    };
    const auto a = run (true), b = run (false);
    const float diffDb = juce::Decibels::gainToDecibels (peak (a, 12000, 24000) / peak (b, 12000, 24000));
    CHECK (std::abs (diffDb) < 0.05f, "safe output leaves normal levels untouched (" + std::to_string (diffDb) + " dB)");
}

void testSafeOutputDoesNotAlias()
{
    // Drive a 3.1 kHz note 12 dB into the clipper; its odd harmonics above Nyquist must not fold back
    Rig rig;
    rig.set (ps::params::master, 6.0f);
    rig.set (ps::params::layerLevel (2), 6.0f);
    auto x = rig.render (48000, { { 0, on (103, 1.0f) } });
    const float aliasDb = worstAliasDb (x, 24000, 440.0 * std::pow (2.0, (103 - 69) / 12.0));
    CHECK (aliasDb < -70.0f, "clipping a loud 3.1 kHz note leaves aliases below -70 dB (" + std::to_string (aliasDb) + " dB)");
}

void testLatencyIsConstant()
{
    Rig a;
    const int latency = a.proc->getLatencySamples();
    a.proc->prepareToPlay (sampleRate, 64);
    const int small = a.proc->getLatencySamples();
    a.proc->prepareToPlay (sampleRate, 2048);
    CHECK (latency == small && latency == a.proc->getLatencySamples() && latency < 96,
           "reported latency is under 2 ms and the same for any block size (" + std::to_string (latency) + " samples)");
}

// CPU benchmark (CLAUDE.md target: an 8-note chord on 4 layers at 48 kHz within 20% of one core).
// Prints the share of one core; only a gross regression fails.
void benchmarkStack()
{
    Rig rig (false);
    for (int i = 0; i < 4; ++i)
        rig.proc->setLayerEngine (i, "test.sine");
    rig.collectRetired();
    std::vector<std::pair<int, juce::MidiMessage>> chord;
    for (int n : { 36, 43, 48, 52, 55, 60, 64, 67 })
        chord.push_back ({ 0, on (n) });
    rig.render (blockSize, chord);

    const int seconds = 10;
    const auto start = juce::Time::getHighResolutionTicks();
    rig.render ((int) sampleRate * seconds);
    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
    const double share = 100.0 * elapsed / seconds;
    std::cout << "info: 8-note chord on 4 layers (test sine) uses " << share << "% of one core\n";
    CHECK (share < 20.0, "stack CPU is within budget");
}

// ---- JP engine ----------------------------------------------------------------------------------------

// A neutral rig with the JP engine on layer 3 (the only sounding layer), its slots set as given
struct JpRig : Rig
{
    explicit JpRig (std::initializer_list<std::pair<int, float>> slots = {})
    {
        proc->setLayerEngine (2, "jp");
        render (blockSize);
        collectRetired();
        for (auto& [slot, value] : slots)
            setSlot (slot, value);
    }

    void setSlot (int slot, float value) { set (ps::params::layerSlot (2, slot), value); }
};

using Jp = ps::JpEngine;

float bandEnergy (const std::vector<float>& x, size_t from, double loHz, double hiHz)
{
    constexpr int order = 13, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (2 * size, 0.0f);
    for (int i = 0; i < size; ++i)
        data[(size_t) i] = x[from + (size_t) i] * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / size));
    fft.performFrequencyOnlyForwardTransform (data.data());
    double sum = 0.0;
    for (int bin = 1; bin < size / 2; ++bin)
        if (bin * sampleRate / size >= loHz && bin * sampleRate / size < hiHz)
            sum += (double) data[(size_t) bin] * data[(size_t) bin];
    return (float) sum;
}

void testJpPitchAndSlots()
{
    const auto& info = ps::engines::create ("jp")->info();
    CHECK ((int) info.slots.size() == Jp::numUsedSlots && Jp::numUsedSlots <= ps::Engine::numSlots, "JP declares all its slots");

    JpRig rig ({ { Jp::unison, Jp::choiceValue (0, Jp::numUnison) } });
    auto x = rig.render (24000, { { 0, on (57) } });
    CHECK (std::abs (estimateFrequency (x, 4000, 24000) - 220.0) < 0.3, "JP plays in tune (single saw, A3)");
    CHECK (allFinite (x) && peak (x) > 0.1f, "JP sounds");

    auto* slot = rig.proc->apvts.getParameter (ps::params::layerSlot (2, Jp::unison).getParamID());
    CHECK (slot->getText (Jp::choiceValue (1, Jp::numUnison), 32) == "JP 7", "JP slot text names the unison mode");
}

void testJpAliasing()
{
    for (int w = 0; w < Jp::numWaves1; ++w)
    {
        JpRig rig ({ { Jp::unison, Jp::choiceValue (0, Jp::numUnison) }, { Jp::wave1, Jp::choiceValue (w, Jp::numWaves1) } });
        auto x = rig.render (48000, { { 0, on (96, 1.0f) } });   // 2093 Hz
        const float db = worstAliasDb (x, 24000, 440.0 * std::pow (2.0, (96 - 69) / 12.0));
        const char* names[] = { "saw", "pulse", "triangle" };
        std::cout << "info: JP " << names[w] << " at 2.1 kHz, worst alias " << db << " dB\n";
        CHECK (db < -60.0f, std::string ("JP ") + names[w] + " is band-limited (aliases below -60 dB at 2.1 kHz)");
    }
}

void testJpSuperSawLevelAndWidth()
{
    auto levelFor = [] (int mode, float mixValue, float spreadValue, std::vector<float>* rightOut = nullptr)
    {
        JpRig rig ({ { Jp::unison, Jp::choiceValue (mode, Jp::numUnison) }, { Jp::mix, mixValue }, { Jp::spread, spreadValue } });
        auto x = rig.render (48000, { { 0, on (57) } });
        if (rightOut != nullptr)
            *rightOut = rig.right;
        return juce::Decibels::gainToDecibels (rms (x, 12000, 48000));
    };
    const float reference = levelFor (1, 0.65f, 0.0f);
    float worst = 0.0f;
    for (int mode = 0; mode < Jp::numUnison; ++mode)
        for (float m : { 0.0f, 0.5f, 1.0f })
            worst = std::max (worst, std::abs (levelFor (mode, m, 0.0f) - reference));
    CHECK (worst < 1.5f, "unison mode and Mix keep the level within 1.5 dB (" + std::to_string (worst) + " dB)");

    std::vector<float> right;
    JpRig narrow ({ { Jp::spread, 0.0f } });
    auto l = narrow.render (24000, { { 0, on (57) } });
    CHECK (maxStep ([&] { std::vector<float> d; for (size_t i = 0; i < l.size(); ++i) d.push_back (l[i] - narrow.right[i]); return d; }()) < 1.0e-6f,
           "Spread 0 is mono");
    JpRig wide;
    auto wl = wide.render (24000, { { 0, on (57) } });
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = 6000; i < wl.size(); ++i)
    {
        lr += (double) wl[i] * wide.right[i];
        ll += (double) wl[i] * wl[i];
        rr += (double) wide.right[i] * wide.right[i];
    }
    const double correlation = lr / std::sqrt (ll * rr);
    CHECK (correlation < 0.9 && correlation > 0.0, "the default Super Saw is wide but mono-compatible (correlation " + std::to_string (correlation) + ")");
}

void testJpFilter()
{
    auto bright = [] (std::initializer_list<std::pair<int, float>> slots)
    {
        JpRig rig (slots);
        auto x = rig.render (24000, { { 0, on (45) } });
        return bandEnergy (x, 12000, 2000.0, 20000.0) / bandEnergy (x, 12000, 50.0, 20000.0);
    };
    const float open = bright ({});
    const float closed = bright ({ { Jp::cutoff, 0.3f } });
    CHECK (closed < open * 1.0e-3f, "LP 24 at 160 Hz removes the highs (60 dB down)");
    const float lp12 = bright ({ { Jp::cutoff, 0.3f }, { Jp::filterType, Jp::choiceValue (1, Jp::numFilterTypes) } });
    CHECK (lp12 > closed && lp12 < open * 0.05f, "LP 12 is gentler than LP 24");
    const float hp = bright ({ { Jp::cutoff, 0.6f }, { Jp::filterType, Jp::choiceValue (3, Jp::numFilterTypes) } });
    CHECK (hp > open, "HP 12 keeps the highs and removes the lows");

    for (int type = 0; type < Jp::numFilterTypes; ++type)
    {
        JpRig rig ({ { Jp::filterType, Jp::choiceValue (type, Jp::numFilterTypes) }, { Jp::resonance, 1.0f }, { Jp::cutoff, 0.5f },
                     { Jp::drive, 1.0f } });
        auto x = rig.render (48000, { { 0, on (40, 1.0f) }, { 0, on (64, 1.0f) }, { 0, on (71, 1.0f) } });
        CHECK (allFinite (x) && peak (x) < 4.0f, "full resonance and drive stay bounded (filter " + std::to_string (type) + ")");
    }

    // Filter envelope: bright at the attack, darker once decayed
    JpRig env ({ { Jp::cutoff, 0.3f }, { Jp::envAmount, 0.85f }, { Jp::envDecay, Jp::timeValue (150.0f) } });
    auto x = env.render (48000, { { 0, on (45) } });
    CHECK (bandEnergy (x, 0, 1000.0, 20000.0) > 30.0f * bandEnergy (x, 36000, 1000.0, 20000.0), "the filter envelope opens and closes the filter");
}

void testJpOsc2()
{
    for (int mod = 0; mod < Jp::numMods; ++mod)
        for (int w = 0; w < Jp::numWaves2; ++w)
        {
            JpRig rig ({ { Jp::balance, 0.5f }, { Jp::osc2Mod, Jp::choiceValue (mod, Jp::numMods) }, { Jp::wave2, Jp::choiceValue (w, Jp::numWaves2) },
                         { Jp::pitch2, 0.5f + 7.0f / 48.0f }, { Jp::xmodDepth, 0.5f } });
            auto x = rig.render (24000, { { 0, on (57) } });
            CHECK (allFinite (x) && peak (x) > 0.05f && peak (x) < 2.0f,
                   "OSC2 wave " + std::to_string (w) + " with mod " + std::to_string (mod) + " sounds and stays bounded");
        }

    // X-Mod depth works with OSC2 Mod off (OSC2 silent in the mix): it reshapes OSC1's spectrum
    {
        JpRig dry ({ { Jp::unison, Jp::choiceValue (0, Jp::numUnison) }, { Jp::pitch2, 0.5f + 7.0f / 48.0f } });
        JpRig wet ({ { Jp::unison, Jp::choiceValue (0, Jp::numUnison) }, { Jp::pitch2, 0.5f + 7.0f / 48.0f }, { Jp::xmodDepth, 0.25f } });
        auto a = dry.render (24000, { { 0, on (57) } });
        auto b = wet.render (24000, { { 0, on (57) } });
        double diff = 0, energy = 0;
        for (size_t i = 4000; i < 20000; ++i)
        {
            diff += std::pow (a[i] - b[i], 2.0);
            energy += (double) a[i] * a[i];
        }
        CHECK (allFinite (b) && diff > 0.1 * energy, "X-Mod depth changes the sound with OSC2 Mod off");
    }

    // Hard sync: OSC2 alone, synced at a fifth above, repeats at OSC1's period
    JpRig sync ({ { Jp::unison, Jp::choiceValue (0, Jp::numUnison) }, { Jp::balance, 1.0f }, { Jp::osc2Mod, Jp::choiceValue (1, Jp::numMods) },
                  { Jp::pitch2, 0.5f + 7.0f / 48.0f } });
    auto x = sync.render (24000, { { 0, on (45) } });
    const int period = (int) std::lround (sampleRate / 110.0);   // 436.36 samples: compare across 11 periods (4800 samples)
    double diff = 0, energy = 0;
    for (size_t i = 8000; i < 16000; ++i)
    {
        diff += std::pow (x[i] - x[i + 4800], 2.0);
        energy += (double) x[i] * x[i];
    }
    juce::ignoreUnused (period);
    CHECK (diff < energy * 0.02, "hard sync locks OSC2 to OSC1's period");
    const float syncAlias = worstAliasDb (x, 4000, 110.0);
    CHECK (syncAlias < -60.0f, "hard sync is band-limited (aliases below -60 dB, " + std::to_string (syncAlias) + " dB)");
}

void testJpSwitchesDoNotClick()
{
    // Low-passed so the waveform itself is smooth; any discontinuity from a switch shows as a step
    const char* names[] = { "wave", "unison mode", "wave 2", "OSC2 mod", "filter type", "mix", "detune", "cutoff", "balance", "drive" };
    const std::pair<int, float> moves[] = {
        { Jp::wave1, Jp::choiceValue (1, Jp::numWaves1) }, { Jp::unison, Jp::choiceValue (4, Jp::numUnison) },
        { Jp::wave2, Jp::choiceValue (2, Jp::numWaves2) }, { Jp::osc2Mod, Jp::choiceValue (2, Jp::numMods) },
        { Jp::filterType, Jp::choiceValue (1, Jp::numFilterTypes) }, { Jp::mix, 1.0f }, { Jp::detune, 1.0f },
        { Jp::cutoff, 0.3f }, { Jp::balance, 0.5f }, { Jp::drive, 0.7f } };
    for (size_t m = 0; m < std::size (moves); ++m)
    {
        JpRig rig ({ { Jp::cutoff, 0.25f }, { Jp::balance, 0.2f } });
        auto x = rig.render (12000, { { 0, on (45) } });
        const float baseline = maxStep (std::vector<float> (x.begin() + 6000, x.end()));
        rig.setSlot (moves[m].first, moves[m].second);
        auto y = rig.render (9600);
        // The new setting can have bigger steady steps of its own (a pulse, a brighter filter), so compare the
        // transition against the larger of the two steady states
        const float steady = std::max (baseline, maxStep (std::vector<float> (y.begin() + 4800, y.end())));
        const float worst = maxStep (std::vector<float> (y.begin(), y.begin() + 2400));
        CHECK (worst < steady * 1.5f, std::string ("JP: changing ") + names[m] + " mid-note does not click (" + std::to_string (worst / steady) + "x the steady step)");
    }
}

// ---- Sub engine ---------------------------------------------------------------------------------------

struct SubRig : Rig
{
    explicit SubRig (std::initializer_list<std::pair<int, float>> slots = {})
    {
        proc->setLayerEngine (2, "sub");
        render (blockSize);
        collectRetired();
        for (auto& [slot, value] : slots)
            set (ps::params::layerSlot (2, slot), value);
    }
};

using Sub = ps::SubEngine;

void testSubEngine()
{
    {
        SubRig rig;
        auto x = rig.render (48000, { { 0, on (33) } });   // A1, 55 Hz
        CHECK (std::abs (estimateFrequency (x, 6000, 48000) - 55.0) < 0.05, "Sub plays in tune (55 Hz)");
        CHECK (std::abs (x[0]) < 1.0e-3f && maxStep (x) < 0.01f, "a retriggered Sub starts at zero phase, click-free");
    }

    auto levelFor = [] (float shapeValue)
    {
        SubRig rig ({ { Sub::shape, shapeValue } });
        auto x = rig.render (48000, { { 0, on (40) } });
        return juce::Decibels::gainToDecibels (rms (x, 12000, 48000));
    };
    const float sine = levelFor (0.0f);
    float worst = 0.0f;
    for (float s : { 0.25f, 0.5f, 0.75f, 1.0f })
        worst = std::max (worst, std::abs (levelFor (s) - sine));
    CHECK (worst < 1.0f, "Shape keeps the level within 1 dB from sine to square (" + std::to_string (worst) + " dB)");

    {
        SubRig rig ({ { Sub::drive, 1.0f }, { Sub::even, 1.0f }, { Sub::subOctave, 1.0f } });
        auto x = rig.render (48000, { { 0, on (33, 1.0f) } });
        double mean = 0.0;
        for (size_t i = 24000; i < 48000; ++i)
            mean += x[i];
        mean /= 24000.0;
        CHECK (allFinite (x) && peak (x) < 1.0f, "full drive and even stay bounded");
        CHECK (std::abs (mean) < 1.0e-3, "even harmonics leave no DC (" + std::to_string (mean) + ")");
        CHECK (bandEnergy (x, 24000, 100.0, 125.0) > 1.0e-3f * bandEnergy (x, 24000, 50.0, 60.0), "Even adds the 2nd harmonic");
    }
    {
        SubRig rig ({ { Sub::punch, 1.0f }, { Sub::punchDecay, Sub::punchValue (40.0f) } });
        auto x = rig.render (48000, { { 0, on (33) } });
        CHECK (estimateFrequency (x, 0, 960) > 150.0, "Punch starts the note high");
        CHECK (std::abs (estimateFrequency (x, 24000, 48000) - 55.0) < 0.1, "and drops to the note");
        CHECK (maxStep (x) < 0.05f, "the punch drop is click-free");
    }
    {
        // Translate: harmonics 2-5 for small speakers, with the level held
        SubRig plain, translated ({ { Sub::translate, 1.0f } });
        auto a = plain.render (48000, { { 0, on (33) } });
        auto b = translated.render (48000, { { 0, on (33) } });
        CHECK (bandEnergy (b, 24000, 100.0, 300.0) > 0.2f * bandEnergy (b, 24000, 40.0, 70.0)
                   && bandEnergy (a, 24000, 100.0, 300.0) < 1.0e-3f * bandEnergy (a, 24000, 40.0, 70.0),
               "Translate adds harmonics 2-5");
        const float change = std::abs (juce::Decibels::gainToDecibels (rms (b, 24000, 48000) / rms (a, 24000, 48000)));
        CHECK (change < 1.0f, "Translate keeps the level within 1 dB (" + std::to_string (change) + " dB)");
        CHECK (maxStep (b) < 0.05f && allFinite (b), "Translate is click-free");
    }
    {
        // Low Cut: a 27.5 Hz note loses level at a 40 Hz cut and keeps it at the lowest setting
        auto level = [] (float cut)
        {
            SubRig rig ({ { Sub::lowCut, cut } });
            auto x = rig.render (96000, { { 0, on (21) } });
            return rms (x, 48000, 96000);
        };
        const float open = level (0.0f), cut = level (1.0f);
        CHECK (juce::Decibels::gainToDecibels (cut / open) < -6.0f, "Low Cut at 40 Hz cuts a 27.5 Hz sub");
    }
    {
        // Changing the shape on a held note is click-free
        SubRig rig ({ { Sub::tone, 0.5f } });
        auto x = rig.render (12000, { { 0, on (33) } });
        const float steady = maxStep (std::vector<float> (x.begin() + 6000, x.end()));
        rig.set (ps::params::layerSlot (2, Sub::shape), 1.0f);
        auto y = rig.render (9600);
        const float after = std::max (steady, maxStep (std::vector<float> (y.begin() + 4800, y.end())));
        const float step = maxStep (std::vector<float> (y.begin(), y.begin() + 2400));
        CHECK (step < after * 1.5f, "moving Shape on a held note does not click (" + std::to_string (step / after) + "x the steady step)");
    }
}

// ---- Wavetable engine ---------------------------------------------------------------------------------

struct WtRig : Rig
{
    explicit WtRig (std::initializer_list<std::pair<int, float>> slots = {})
    {
        proc->setLayerEngine (2, "wavetable");
        render (blockSize);
        collectRetired();
        for (auto& [slot, value] : slots)
            setSlot (slot, value);
    }

    void setSlot (int slot, float value) { set (ps::params::layerSlot (2, slot), value); }
};

using Wt = ps::WavetableEngine;

void testWavetableEngine()
{
    const auto& tables = ps::Wavetables::get();
    const float off = Wt::choiceValue (0, Wt::numUnison);
    auto tableValue = [&] (int t) { return Wt::choiceValue (t, tables.size()); };
    {
        WtRig rig ({ { Wt::unison, off } });
        auto x = rig.render (24000, { { 0, on (57) } });
        CHECK (std::abs (estimateFrequency (x, 4000, 24000) - 220.0) < 0.3, "Wavetable plays in tune (A3)");
        CHECK (allFinite (x) && peak (x) > 0.1f, "Wavetable sounds");
    }

    // Every frame is loudness-matched to a sine of its pitch, so tables and Position keep the loudness
    for (int note : { 33, 57, 81 })
    {
        auto loudnessFor = [&] (int t, float pos)
        {
            WtRig rig ({ { Wt::unison, off }, { Wt::table, tableValue (t) }, { Wt::position, pos } });
            auto x = rig.render (48000, { { 0, on (note) } });
            return ps::loudness::integratedLufs (x.data() + 12000, rig.right.data() + 12000, 36000, sampleRate);
        };
        const double reference = loudnessFor (0, 0.0f);
        double worst = 0.0;
        for (int t = 0; t < tables.size(); ++t)
            for (float pos : { 0.0f, 0.37f, 1.0f })
                worst = std::max (worst, std::abs (loudnessFor (t, pos) - reference));
        CHECK (worst < 0.5, "every table and Position keep the loudness within 0.5 LU at note " + std::to_string (note) + " (" + std::to_string (worst) + " LU)");
    }

    for (int t = 0; t < tables.size(); ++t)
        for (int note : { 40, 96 })
        {
            WtRig rig ({ { Wt::unison, off }, { Wt::table, tableValue (t) }, { Wt::position, 0.8f } });
            auto x = rig.render (48000, { { 0, on (note, 1.0f) } });
            const float db = worstAliasDb (x, 24000, 440.0 * std::pow (2.0, (note - 69) / 12.0));
            CHECK (db < -60.0f, "table " + tables.table (t).name.toStdString() + " at note " + std::to_string (note)
                                    + " is band-limited (" + std::to_string (db) + " dB)");
        }

    {
        // Basic table: Position 0 is a sine; the envelope starts it at a square and decays back
        WtRig rig ({ { Wt::unison, off }, { Wt::posEnv, 1.0f }, { Wt::posEnvTime, Wt::envValue (100.0f) } });
        auto x = rig.render (48000, { { 0, on (45) } });
        CHECK (bandEnergy (x, 600, 300.0, 20000.0) > 100.0f * bandEnergy (x, 36000, 300.0, 20000.0), "the Position envelope sweeps the table");
    }

    {
        // Warp: bends a sine into a brighter wave, stays band-limited at its widest, and keeps the level near
        WtRig plain ({ { Wt::unison, off } }), bent ({ { Wt::unison, off }, { Wt::warp, 1.0f } });
        auto a = plain.render (36000, { { 0, on (45) } });
        auto b = bent.render (36000, { { 0, on (45) } });
        CHECK (bandEnergy (b, 12000, 300.0, 20000.0) > 100.0f * bandEnergy (a, 12000, 300.0, 20000.0), "Warp adds harmonics");
        const double levelChange = ps::loudness::integratedLufs (b.data() + 12000, bent.right.data() + 12000, 24000, sampleRate)
                                 - ps::loudness::integratedLufs (a.data() + 12000, plain.right.data() + 12000, 24000, sampleRate);
        std::cout << "info: full Warp moves the loudness " << levelChange << " LU\n";
        CHECK (std::abs (levelChange) < 1.0, "Warp keeps the loudness within 1 LU (" + std::to_string (levelChange) + " LU)");
        for (float w : { 0.0f, 1.0f })
            for (int note : { 40, 96 })
            {
                WtRig rig ({ { Wt::unison, off }, { Wt::table, tableValue (5) }, { Wt::position, 1.0f }, { Wt::warp, w } });
                auto x = rig.render (48000, { { 0, on (note, 1.0f) } });
                const float db = worstAliasDb (x, 24000, 440.0 * std::pow (2.0, (note - 69) / 12.0));
                CHECK (db < -60.0f, "Digital at full " + std::string (w > 0.5f ? "+" : "-") + "Warp, note " + std::to_string (note)
                                        + ", is band-limited (" + std::to_string (db) + " dB)");
            }
    }
    {
        // Pos Spread: Modern 8 on a sine frame, fanned across Basic, reads brighter frames on its outer voices
        WtRig narrow, fanned ({ { Wt::posSpread, 1.0f } });
        auto a = narrow.render (36000, { { 0, on (45) } });
        auto b = fanned.render (36000, { { 0, on (45) } });
        CHECK (bandEnergy (b, 12000, 300.0, 20000.0) > 100.0f * bandEnergy (a, 12000, 300.0, 20000.0), "Pos Spread fans the voices across the table");
    }
    {
        // A glide up through every band limit (each change crossfades between the two copies)
        WtRig rig ({ { Wt::unison, off }, { Wt::position, 0.67f } });
        rig.set (ps::params::layerVoiceMode (2), mono);
        rig.set (ps::params::layerGlide (2), 500.0f);
        auto x = rig.render (96000, { { 0, on (36) }, { 12000, on (96) } });
        CHECK (allFinite (x) && std::abs (estimateFrequency (x, 60000, 96000) - 440.0 * std::pow (2.0, (96 - 69) / 12.0)) < 0.5,
               "a glide across every band limit lands in tune");
        CHECK (worstAliasDb (x, 70000, 440.0 * std::pow (2.0, (96 - 69) / 12.0)) < -60.0f, "and lands band-limited");
    }

    const char* names[] = { "table", "unison mode", "position", "detune", "warp", "pos spread" };
    const std::pair<int, float> moves[] = { { Wt::table, tableValue (3) }, { Wt::unison, Wt::choiceValue (4, Wt::numUnison) },
                                            { Wt::position, 1.0f }, { Wt::detune, 1.0f }, { Wt::warp, 0.0f }, { Wt::posSpread, 1.0f } };
    for (size_t m = 0; m < std::size (moves); ++m)
    {
        WtRig rig ({ { Wt::position, 0.3f } });
        auto x = rig.render (12000, { { 0, on (45) } });
        const float baseline = maxStep (std::vector<float> (x.begin() + 6000, x.end()));
        rig.setSlot (moves[m].first, moves[m].second);
        // A second of the new state: random-phase unison beats slowly, so its worst steady step needs a long look
        auto y = rig.render (52800);
        const float steady = std::max (baseline, maxStep (std::vector<float> (y.begin() + 4800, y.end())));
        const float step = maxStep (std::vector<float> (y.begin(), y.begin() + 2400));
        CHECK (step < steady * 1.5f, std::string ("Wavetable: changing ") + names[m] + " mid-note does not click ("
                                         + std::to_string (step / steady) + "x the steady step)");
    }
}

// ---- Chain knobs and the Harmony Engine ---------------------------------------------------------------

// The init stack the user hears: Sub, Dirt body, JP mid, Spectral air
struct StackRig : Rig
{
    StackRig() : Rig (false)
    {
        const char* engines[ps::params::numLayers] = { "sub", "dirt", "jp", "spectral", "", "", "", "" };
        for (int i = 0; i < ps::params::numLayers; ++i)
            proc->setLayerEngine (i, engines[i]);
        if (const char* m = std::getenv ("POTATO_MASTER")) set (ps::params::master, (float) std::atof (m));
        render (blockSize);
        collectRetired();
    }

    static std::vector<std::pair<int, juce::MidiMessage>> chord()
    {
        std::vector<std::pair<int, juce::MidiMessage>> c;
        for (int n : { 36, 48, 55, 60, 64, 67 })
            c.push_back ({ 0, on (n, 0.9f) });
        return c;
    }

    // Renders with a parameter ramped linearly from its current value to `to` over the whole render
    std::vector<float> renderSweep (const juce::ParameterID& id, float to, int numSamples)
    {
        std::vector<float> out, outR;
        const float from = get (id);
        for (int pos = 0; pos < numSamples; pos += blockSize)
        {
            set (id, from + (to - from) * (float) (pos + blockSize) / (float) numSamples);
            auto x = render (blockSize);
            out.insert (out.end(), x.begin(), x.end());
            outR.insert (outR.end(), right.begin(), right.end());
        }
        right = outR;
        return out;
    }
};

double lufs (const std::vector<float>& l, const std::vector<float>& r, size_t from, size_t to)
{
    return ps::loudness::integratedLufs (l.data() + from, r.data() + from, to - from, sampleRate);
}

// Side energy below 120 Hz relative to mid, in dB
float lowSideDb (const std::vector<float>& l, const std::vector<float>& r, size_t from)
{
    std::vector<float> mid (l.size()), side (l.size());
    for (size_t i = 0; i < l.size(); ++i)
    {
        mid[i] = 0.5f * (l[i] + r[i]);
        side[i] = 0.5f * (l[i] - r[i]);
    }
    return juce::Decibels::gainToDecibels (std::sqrt (bandEnergy (side, from, 20.0, 120.0) / bandEnergy (mid, from, 20.0, 120.0)), -200.0f);
}

void testChainPaths()
{
    // Monotone waypoints give a monotone path that passes through them and never overshoots
    const ps::chain::Path path ({ 0.0f, 0.1f, 0.8f, 0.85f, 1.0f });
    bool monotone = true;
    float previous = path.at (0.0f);
    for (int i = 1; i <= 1000; ++i)
    {
        const float v = path.at ((float) i / 1000.0f);
        monotone = monotone && v >= previous - 1.0e-6f && v <= 1.0f + 1.0e-6f;
        previous = v;
    }
    CHECK (monotone, "a chain path is monotone between monotone waypoints");
    CHECK (std::abs (path.at (0.5f) - 0.8f) < 1.0e-6f && std::abs (path.at (0.75f) - 0.85f) < 1.0e-6f, "and passes through every waypoint");

    // Offsets go only to layers of the route's role
    std::array<float, ps::chain::numKnobs> u {};
    u[ps::chain::mass] = 1.0f;
    using R = ps::params::Role;
    const auto o = ps::chain::compute (u, { R::sub, R::body, R::mid, R::air });
    CHECK (o.slot[0][(size_t) ps::SlotRole::translate] > 0.4f && std::abs (o.slot[2][(size_t) ps::SlotRole::translate]) < 1.0e-9f,
           "MASS adds Translate on the Sub layer only");
    CHECK (o.monoBassOctaves > 0.0f && o.lowFocusDb > 2.0f, "MASS widens the mono zone and lifts the tactile band");
}

void testOnsetDuck()
{
    // GROOVE ducks the Body and Air layers under each new note; Full (like Sub and Mid) leads and isn't ducked.
    // The pump is the same in both rigs, so the ratio across the onset compares only the duck.
    auto dipAt = [] (ps::params::Role role)
    {
        Rig rig;
        rig.set (ps::params::layerRole (2), (float) role);
        rig.set (ps::params::chainKnob (ps::chain::groove), 1.0f);
        auto x = rig.render (40000, { { 0, on (57) }, { 36000, on (69) } });
        return juce::Decibels::gainToDecibels (rms (x, 36096, 36480) / rms (x, 35000, 35900));
    };
    const float full = dipAt (ps::params::Role::full), body = dipAt (ps::params::Role::body);
    CHECK (body < full - 3.0f, "GROOVE ducks the Body layer under a new note's onset (" + std::to_string (body - full) + " dB)");
}

void testBloomSpace()
{
    // BLOOM opens a space: after the chord is released, its tail rings on well past the dry release
    auto tailDb = [] (float bloom)
    {
        StackRig rig;
        rig.set (ps::params::chainKnob (ps::chain::bloom), bloom);
        std::vector<std::pair<int, juce::MidiMessage>> events = StackRig::chord();
        for (int n : { 36, 48, 55, 60, 64, 67 })
            events.push_back ({ 48000, off (n) });
        auto x = rig.render (96000, events);
        return juce::Decibels::gainToDecibels (rms (x, 48000 + 24000, 48000 + 36000) / rms (x, 24000, 48000), -200.0f);
    };
    const float dry = tailDb (0.0f), wet = tailDb (1.0f);
    CHECK (wet > dry + 10.0f && wet < -10.0f, "BLOOM's space rings on after the release (" + std::to_string (dry) + " -> " + std::to_string (wet) + " dB)");
}

// A host transport at 120 BPM in 4/4 that advances one block per processBlock call
struct FakeTransport : juce::AudioPlayHead
{
    juce::int64 samples = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (120.0);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setIsPlaying (true);
        const double ppq = (double) samples / sampleRate * 2.0;
        p.setPpqPosition (ppq);
        p.setPpqPositionOfLastBarStart (4.0 * std::floor (ppq / 4.0));
        const_cast<FakeTransport*> (this)->samples += blockSize;
        return p;
    }
};

void testDropOnTheBar()
{
    // DROP pressed at beat 1.3 waits for the bar line at beat 4 (sample 96000 at 120 BPM): the -1 octave and
    // MASS come in there, not when the button goes down. Compared with an untouched rig on the same transport.
    auto play = [] (bool press)
    {
        StackRig rig;
        FakeTransport transport;
        rig.proc->setPlayHead (&transport);
        auto x = rig.render (31200, StackRig::chord());   // to beat 1.3
        if (press)
            rig.set (ps::params::drop, 1.0f);
        auto y = rig.render (160800);                      // to beat 8
        x.insert (x.end(), y.begin(), y.end());
        rig.proc->setPlayHead (nullptr);
        return x;
    };
    const auto control = play (false), dropped = play (true);
    auto lift = [&] (size_t from, size_t to)
    {
        auto band = [&] (const std::vector<float>& x) { return bandEnergy (std::vector<float> (x.begin() + (long) from, x.begin() + (long) to), 0, 40.0, 80.0); };
        return 10.0 * std::log10 (band (dropped) / band (control));
    };
    // After: beats 6 to 8 (on beat 4 the pump ducks the downbeat, as a sidechain under the kick would)
    const double waiting = lift (36000, 95000), after = lift (144000, 192000);
    CHECK (std::abs (waiting) < 0.5 && after > 1.0, "DROP waits for the next bar line (" + std::to_string (waiting) + " dB before it, +"
                                                        + std::to_string (after) + " dB after)");
}

void testXyPad()
{
    // Each corner emphasises its layer (Sub is anchored, so its corner changes nothing) at the same loudness,
    // click-free, with mono lows
    StackRig control;
    const int settle = 48000, measure = 96000;
    control.render (settle + measure, StackRig::chord());
    control.render (96000);
    auto c = control.render (settle + measure);
    auto cR = control.right;
    const double reference = lufs (c, cR, (size_t) settle, c.size());
    const char* names[] = { "Sub", "Body", "Mid", "Air" };
    const float cx[] = { 0.0f, 1.0f, 0.0f, 1.0f }, cy[] = { 1.0f, 1.0f, 0.0f, 0.0f };
    for (int corner = 0; corner < 4; ++corner)
    {
        StackRig rig;
        auto a = rig.render (settle + measure, StackRig::chord());
        std::vector<float> sweep;
        for (int pos = 0; pos < 96000; pos += blockSize)
        {
            const float t = (float) (pos + blockSize) / 96000.0f;
            rig.set (ps::params::padX, 0.5f + (cx[corner] - 0.5f) * t);
            rig.set (ps::params::padY, 0.5f + (cy[corner] - 0.5f) * t);
            auto x = rig.render (blockSize);
            sweep.insert (sweep.end(), x.begin(), x.end());
        }
        auto b = rig.render (settle + measure);
        auto bR = rig.right;
        const double change = lufs (b, bR, (size_t) settle, b.size()) - reference;
        const std::string name = std::string ("XY to the ") + names[corner] + " corner";
        CHECK (std::abs (change) <= 0.5, name + " keeps the loudness within 0.5 LU (" + std::to_string (change) + " LU)");
        const float side = lowSideDb (b, bR, (size_t) settle);
        CHECK (side < -40.0f, name + " keeps the lows mono (side " + std::to_string (side) + " dB)");
        const float steady = std::max (maxStep (std::vector<float> (a.begin() + settle, a.end())), maxStep (std::vector<float> (b.begin() + settle, b.end())));
        CHECK (maxStep (sweep) < steady * 1.5f, name + " moves click-free");
        if (corner > 0)
        {
            // With three free layers its layer comes up 4 dB (10 log 2.5) and the others go down 6 dB; the anchored Sub stays
            auto db = [&] (int l) { return juce::Decibels::gainToDecibels (rig.proc->getStack().layer (l).knobGain()); };
            bool others = std::abs (db (0)) < 0.01f;
            for (int l = 1; l < 4; ++l)
                others = others && (l == corner || std::abs (db (l) + 6.0f) < 0.1f);
            CHECK (std::abs (db (corner) - 3.98f) < 0.1f && others, name + " brings its layer forward (" + std::to_string (db (corner)) + " dB)");
        }
    }
}

// Developer report (POTATO_KNOB_REPORT=1): what each knob does to the init stack, knob at 0 vs full, in octave
// bands (averaged over 2 s of frames), loudness and low/high stereo width. A tuning aid, not a test.
std::vector<double> octaveBandsDb (const std::vector<float>& l, const std::vector<float>& r, size_t from, size_t to)
{
    constexpr int order = 13, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<double> bands (10, 0.0);
    std::vector<float> data (2 * size);
    for (size_t start = from; start + size <= to; start += size / 2)
        for (const auto* x : { &l, &r })
        {
            std::fill (data.begin(), data.end(), 0.0f);
            for (int i = 0; i < size; ++i)
                data[(size_t) i] = (*x)[start + (size_t) i] * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / size));
            fft.performFrequencyOnlyForwardTransform (data.data());
            for (int bin = 1; bin < size / 2; ++bin)
            {
                const double f = bin * sampleRate / size;
                const int b = (int) std::floor (std::log2 (f / 22.0));
                if (b >= 0 && b < 10)
                    bands[(size_t) b] += (double) data[(size_t) bin] * data[(size_t) bin];
            }
        }
    for (auto& b : bands)
        b = 10.0 * std::log10 (b + 1.0e-20);
    return bands;
}

void knobReport()
{
    const int settle = 48000, measure = 96000;
    auto render = [&] (int knob, float value, bool chord, std::vector<float>& l, std::vector<float>& r)
    {
        StackRig rig;
        if (knob >= 0)
            rig.set (ps::params::chainKnob (knob), value);
        std::vector<std::pair<int, juce::MidiMessage>> notes;
        if (chord)
            notes = StackRig::chord();
        else
            notes = { { 0, on (48, 0.9f) } };
        l = rig.render (settle + measure, notes);
        r = rig.right;
    };
    for (const bool chord : { false, true })
    {
        std::vector<float> l0, r0;
        render (-1, 0.0f, chord, l0, r0);
        const auto base = octaveBandsDb (l0, r0, (size_t) settle, l0.size());
        const double lu0 = lufs (l0, r0, (size_t) settle, l0.size());
        std::cout << (chord ? "CHORD" : "C3") << "  bands (Hz)  22   44   88  176  352  704  1.4k 2.8k 5.6k 11k\n";
        std::cout << "  base dB     ";
        for (auto b : base) std::cout << juce::String (b - base[2], 0).paddedLeft (' ', 5);
        std::cout << "\n";
        for (int k = 0; k < ps::params::numChainKnobs; ++k)
        {
            if (const char* only = std::getenv ("POTATO_REPORT_KNOB"); only != nullptr && std::atoi (only) != k)
                continue;
            std::vector<float> l1, r1;
            render (k, 1.0f, chord, l1, r1);
            const auto bands = octaveBandsDb (l1, r1, (size_t) settle, l1.size());
            const double lu = lufs (l1, r1, (size_t) settle, l1.size()) - lu0;
            double change = 0.0;
            std::cout << "  " << juce::String (ps::params::chainKnobNames[k]).paddedRight (' ', 10) << "  ";
            for (size_t b = 0; b < bands.size(); ++b)
            {
                const double d = bands[b] - base[b] - lu;
                change += d * d;
                std::cout << juce::String (d, 1).paddedLeft (' ', 5);
            }
            std::cout << "   LU " << juce::String (lu, 2) << "  tone " << juce::String (std::sqrt (change / (double) bands.size()), 2) << " dB\n";
        }
    }
}

void testBassHarmonics()
{
    ps::BassHarmonics h;
    h.prepare (sampleRate);
    const int n = 48000;
    std::vector<float> l (n), r (n), dry (n);
    for (int i = 0; i < n; ++i)
        dry[(size_t) i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 55.0 * i / sampleRate);
    l = r = dry;
    for (int pos = 0; pos < n; pos += 512)
        h.process (l.data() + pos, r.data() + pos, std::min (512, n - pos), 1.0f);
    std::vector<float> added (n);
    for (int i = 0; i < n; ++i)
        added[(size_t) i] = l[(size_t) i] - dry[(size_t) i];
    const float second = bandEnergy (added, 24000, 100.0, 120.0), third = bandEnergy (added, 24000, 155.0, 175.0);
    const float above = bandEnergy (added, 24000, 400.0, 20000.0), total = bandEnergy (added, 24000, 20.0, 20000.0);
    CHECK (second > 0.2f * total && third > 0.1f * total, "MASS harmonics: a 55 Hz sine gets its 2nd and 3rd harmonics");
    CHECK (above < 1.0e-3f * total, "MASS harmonics: nothing above 400 Hz (no aliasing, no fizz)");
    CHECK (l == r, "MASS harmonics are added in mono");
}

void testChainKnobInvariants()
{
    // A control rig plays the same notes with the knobs untouched: the loudness change is measured against it at
    // the same moment, so the patch's own slow motion (unison beating) doesn't read as the knob's
    StackRig control;
    const int settle = 48000, measure = 96000, sweepLength = 96000;
    auto c0 = control.render (settle + measure, StackRig::chord());
    control.render (sweepLength);
    auto c1 = control.render (settle + measure);
    auto c1R = control.right;
    const double reference = lufs (c1, c1R, (size_t) settle, c1.size());
    std::cout << "info: the test stack plays at " << reference << " LUFS, peak " << ps::loudness::truePeakDb (c1.data(), c1.size()) << " dBTP\n";
    {
        const float side = lowSideDb (c1, c1R, (size_t) settle);
        CHECK (side < -40.0f, "the default stack's lows are mono (side " + std::to_string (side) + " dB below 120 Hz)");
    }

    // Every knob, then SUPER, BUILD and DROP (the buttons are pressed halfway through the sweep and held)
    auto knobId = [] (int k)
    {
        const int n = ps::params::numChainKnobs;
        return k < n ? ps::params::chainKnob (k) : k == n ? ps::params::super : k == n + 1 ? ps::params::build : ps::params::drop;
    };
    for (int k = 0; k <= ps::params::numChainKnobs + 2; ++k)
    {
        const char* extra[] = { "SUPER", "BUILD", "DROP" };
        const std::string name = k < ps::params::numChainKnobs ? ps::params::chainKnobNames[k] : extra[k - ps::params::numChainKnobs];
        StackRig rig;
        auto a = rig.render (settle + measure, StackRig::chord());
        auto aR = rig.right;

        // Sweep to full over 2 s, then hold
        auto sweep = rig.renderSweep (knobId (k), 1.0f, sweepLength);
        auto sweepR = rig.right;
        auto b = rig.render (settle + measure);
        auto bR = rig.right;

        const double change = lufs (b, bR, (size_t) settle, b.size()) - reference;
        std::cout << "info: " << name << " 0 -> 1: " << change << " LU, trim " << juce::Decibels::gainToDecibels (rig.proc->getLoudnessTrim()) << " dB\n";
        CHECK (std::abs (change) <= 0.5, name + " keeps the loudness within 0.5 LU (" + std::to_string (change) + " LU)");

        double tp = -200.0;
        for (auto* x : { &a, &aR, &sweep, &sweepR, &b, &bR })
            tp = std::max (tp, ps::loudness::truePeakDb (x->data(), x->size()));
        CHECK (tp <= -1.0, name + " stays under -1 dBTP (" + std::to_string (tp) + " dBTP)");

        const float side = lowSideDb (b, bR, (size_t) settle);
        CHECK (side < -40.0f, name + " keeps the lows mono (side " + std::to_string (side) + " dB below 120 Hz)");

        // Each knob does what its name says, measured against the untouched control at the same moment
        auto midOf = [] (const std::vector<float>& l, const std::vector<float>& r)
        {
            std::vector<float> m (l.size());
            for (size_t i = 0; i < l.size(); ++i)
                m[i] = 0.5f * (l[i] + r[i]);
            return m;
        };
        auto correlation = [&] (const std::vector<float>& l, const std::vector<float>& r)
        {
            double lr = 0, ll = 0, rr = 0;
            for (size_t i = (size_t) settle; i < l.size(); ++i)
            {
                lr += (double) l[i] * r[i];
                ll += (double) l[i] * l[i];
                rr += (double) r[i] * r[i];
            }
            return lr / std::sqrt (ll * rr);
        };
        const auto bm = midOf (b, bR), cm = midOf (c1, c1R);
        auto bandShare = [&] (const std::vector<float>& m, double lo, double hi)
        {
            return 10.0 * std::log10 (bandEnergy (m, (size_t) settle, lo, hi) / bandEnergy (m, (size_t) settle, 20.0, 20000.0));
        };
        if (name == "MASS")
        {
            // At the same loudness (the trim holds it), the tactile band carries more energy
            const double lift = 10.0 * std::log10 (bandEnergy (bm, (size_t) settle, 40.0, 80.0) / bandEnergy (cm, (size_t) settle, 40.0, 80.0));
            CHECK (lift > 1.0, "MASS lifts the 40-80 Hz tactile band at the same loudness (+" + std::to_string (lift) + " dB)");
        }
        if (name == "SCALE")
        {
            const double before = correlation (c1, c1R), after = correlation (b, bR);
            CHECK (after < before - 0.05 && after > 0.2, "SCALE widens (correlation " + std::to_string (before) + " -> " + std::to_string (after) + ")");
        }
        if (name == "PRESSURE")
        {
            const double plrBefore = ps::loudness::truePeakDb (cm.data() + settle, cm.size() - (size_t) settle) - reference;
            const double plrAfter = ps::loudness::truePeakDb (bm.data() + settle, bm.size() - (size_t) settle) - lufs (b, bR, (size_t) settle, b.size());
            CHECK (plrAfter < plrBefore - 2.0, "PRESSURE makes it denser (peak-to-loudness " + std::to_string (plrBefore) + " -> " + std::to_string (plrAfter) + " dB)");
        }
        if (name == "GROOVE")
        {
            // The pump: the short-term level swings with the beat (10 ms windows, loudest against quietest)
            auto swing = [&] (const std::vector<float>& m)
            {
                float lo = 1.0e9f, hi = 0.0f;
                for (size_t i = (size_t) settle; i + 480 <= m.size(); i += 480)
                {
                    const float r = rms (m, i, i + 480);
                    lo = std::min (lo, r);
                    hi = std::max (hi, r);
                }
                return juce::Decibels::gainToDecibels (hi / lo);
            };
            const float before = swing (cm), after = swing (bm);
            CHECK (after > before + 4.0f, "GROOVE pumps with the beat (swing " + std::to_string (before) + " -> " + std::to_string (after) + " dB)");
        }
        if (name == "CLARITY")
        {
            const double calmer = bandShare (cm, 2000.0, 5000.0) - bandShare (bm, 2000.0, 5000.0);
            CHECK (calmer > 0.5, "CLARITY calms the 2-5 kHz presence band (-" + std::to_string (calmer) + " dB share)");
            float dipped = 1.0f;
            for (int i = 0; i < ps::params::numLayers; ++i)
                dipped = std::min (dipped, rig.proc->getStack().layer (i).carvePowerRatio());
            CHECK (dipped < 0.95f, "CLARITY gives contested bands one owner (a layer gives way: "
                                       + std::to_string (juce::Decibels::gainToDecibels (dipped, -100.0f) * 0.5f) + " dB)");
        }
        if (name == "BUILD")
        {
            const double thinner = bandShare (cm, 40.0, 80.0) - bandShare (bm, 40.0, 80.0);
            CHECK (thinner > 3.0, "BUILD thins the low end toward the drop (-" + std::to_string (thinner) + " dB at 40-80 Hz)");
        }
        if (name == "DROP")
        {
            // Mass, the -1 octave and the full pump: more weight in the tactile band at the same loudness
            const double lift = 10.0 * std::log10 (bandEnergy (bm, (size_t) settle, 40.0, 80.0) / bandEnergy (cm, (size_t) settle, 40.0, 80.0));
            CHECK (lift > 1.0, "DROP lands heavier (+" + std::to_string (lift) + " dB at 40-80 Hz)");
        }
        if (name == "SHINE")
        {
            const double lift = bandShare (bm, 6000.0, 20000.0) - bandShare (cm, 6000.0, 20000.0);
            CHECK (lift > 1.0, "SHINE brightens the top (+" + std::to_string (lift) + " dB above 6 kHz)");
        }

        // No click while sweeping: no step larger than the steady states have on their own
        const float steady = std::max (maxStep (std::vector<float> (a.begin() + settle, a.end())), maxStep (std::vector<float> (b.begin() + settle, b.end())));
        const float moving = maxStep (sweep);
        CHECK (moving < steady * 1.5f, name + " sweeps click-free (" + std::to_string (moving / steady) + "x the steady step)");
    }
}

double jpCpuShare (int unisonMode)
{
    Rig rig (false);
    for (int i = 0; i < 4; ++i)
        rig.proc->setLayerEngine (i, "jp");
    rig.render (blockSize);
    rig.collectRetired();
    for (int i = 0; i < 4; ++i)
        rig.set (ps::params::layerSlot (i, Jp::unison), Jp::choiceValue (unisonMode, Jp::numUnison));
    std::vector<std::pair<int, juce::MidiMessage>> chord;
    for (int n : { 36, 43, 48, 52, 55, 60, 64, 67 })
        chord.push_back ({ 0, on (n) });
    rig.render (blockSize, chord);

    // Best of three 2-second runs, so a busy shared machine doesn't read as engine cost
    double best = 1.0e9;
    for (int run = 0; run < 3; ++run)
    {
        const int seconds = 2;
        const auto start = juce::Time::getHighResolutionTicks();
        rig.render ((int) sampleRate * seconds);
        const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
        best = std::min (best, 100.0 * elapsed / seconds);
    }
    return best;
}

double wavetableCpuShare (int unisonMode)
{
    Rig rig (false);
    for (int i = 0; i < 4; ++i)
        rig.proc->setLayerEngine (i, "wavetable");
    rig.render (blockSize);
    rig.collectRetired();
    for (int i = 0; i < 4; ++i)
        rig.set (ps::params::layerSlot (i, Wt::unison), Wt::choiceValue (unisonMode, Wt::numUnison));
    std::vector<std::pair<int, juce::MidiMessage>> chord;
    for (int n : { 36, 43, 48, 52, 55, 60, 64, 67 })
        chord.push_back ({ 0, on (n) });
    rig.render (blockSize, chord);
    double best = 1.0e9;
    for (int run = 0; run < 3; ++run)
    {
        const auto start = juce::Time::getHighResolutionTicks();
        rig.render ((int) sampleRate * 2);
        best = std::min (best, 50.0 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start));
    }
    return best;
}

void benchmarkWavetable()
{
    const double modern8 = wavetableCpuShare (2), modern16 = wavetableCpuShare (4);
    std::cout << "info: 8-note chord on 4 Wavetable layers uses " << modern8 << "% of one core (Modern 8), " << modern16 << "% (Modern 16)\n";
    CHECK (modern8 < 20.0, "Wavetable stack CPU is within budget (Modern 8 on every layer)");
    CHECK (modern16 < 30.0, "16-voice Wavetable unison stays under 30% of one core");
}

void benchmarkJp()
{
    const double jp7 = jpCpuShare (1), modern16 = jpCpuShare (4);
    std::cout << "info: 8-note chord on 4 JP layers uses " << jp7 << "% of one core (JP 7), " << modern16 << "% (Modern 16)\n";
    CHECK (jp7 < 20.0, "JP stack CPU is within budget (JP 7 Super Saw on every layer)");
    CHECK (modern16 < 30.0, "the heaviest unison stays under 30% of one core");
}

// ---- Spectral engine -------------------------------------------------------------------------------------

struct SpRig : Rig
{
    explicit SpRig (std::initializer_list<std::pair<int, float>> slots = {})
    {
        proc->setLayerEngine (2, "spectral");
        render (blockSize);
        collectRetired();
        for (auto& [slot, value] : slots)
            setSlot (slot, value);
    }

    void setSlot (int slot, float value) { set (ps::params::layerSlot (2, slot), value); }
};

using Sp = ps::SpectralEngine;

void testSpectralEngine()
{
    const float off = Sp::choiceValue (0, Sp::numUnison);
    auto src = [] (int i) { return Sp::choiceValue (i, Sp::numSources); };
    {
        SpRig rig ({ { Sp::unison, off } });
        auto x = rig.render (24000, { { 0, on (57) } });
        CHECK (std::abs (estimateFrequency (x, 4000, 24000) - 220.0) < 0.3, "Spectral plays in tune (A3)");
        CHECK (allFinite (x) && peak (x) > 0.1f, "Spectral sounds");
    }

    // Every source, morph position and operation keeps the loudness (each spectrum is K-weighted to a sine)
    for (int note : { 33, 57, 81 })
    {
        auto loudnessFor = [&] (std::initializer_list<std::pair<int, float>> slots)
        {
            SpRig rig ({ { Sp::unison, off } });
            for (auto& [slot, value] : slots)
                rig.setSlot (slot, value);
            auto x = rig.render (48000, { { 0, on (note) } });
            return ps::loudness::integratedLufs (x.data() + 12000, rig.right.data() + 12000, 36000, sampleRate);
        };
        const double reference = loudnessFor ({});
        double worst = 0.0;
        for (int a = 0; a < Sp::numSources; ++a)
        {
            const double d = loudnessFor ({ { Sp::sourceA, src (a) } }) - reference;
            if (std::abs (d) > 0.5) std::cout << "info: note " << note << " source " << a << " " << d << " LU\n";
            worst = std::max (worst, std::abs (d));
        }
        for (auto op : { std::pair<int, float> { Sp::morph, 0.5f }, { Sp::morph, 1.0f }, { Sp::tilt, 0.0f }, { Sp::tilt, 1.0f },
                         { Sp::oddEven, 0.0f }, { Sp::oddEven, 1.0f }, { Sp::formant, 0.0f }, { Sp::formant, 1.0f },
                         { Sp::smear, 1.0f }, { Sp::shepard, 1.0f } })
        {
            const double d = loudnessFor ({ op }) - reference;
            if (std::abs (d) > 0.5) std::cout << "info: note " << note << " op " << op.first << "=" << op.second << " " << d << " LU\n";
            worst = std::max (worst, std::abs (d));
        }
        CHECK (worst < 0.5, "every Spectral source and operation keeps the loudness within 0.5 LU at note " + std::to_string (note)
                                + " (" + std::to_string (worst) + " LU)");
    }

    // Band-limited: the brightest settings at a high note
    for (int note : { 40, 96 })
        for (auto slots : { std::initializer_list<std::pair<int, float>> { { Sp::sourceA, src (8) }, { Sp::tilt, 1.0f } },
                            std::initializer_list<std::pair<int, float>> { { Sp::sourceA, src (3) }, { Sp::formant, 1.0f }, { Sp::smear, 1.0f } } })
        {
            SpRig rig ({ { Sp::unison, off } });
            for (auto& [slot, value] : slots)
                rig.setSlot (slot, value);
            auto x = rig.render (48000, { { 0, on (note, 1.0f) } });
            // Relative to the strongest harmonic: a rising spectrum's fundamental is not its reference level
            const float db = worstAliasDb (x, 24000, 440.0 * std::pow (2.0, (note - 69) / 12.0), true);
            CHECK (db < -60.0f, "Spectral at note " + std::to_string (note) + " is band-limited (" + std::to_string (db) + " dB under its strongest harmonic)");
        }

    // The operations do what they say (single voice, A2)
    auto spectrum = [&] (std::initializer_list<std::pair<int, float>> slots, std::vector<float>* out = nullptr)
    {
        SpRig rig ({ { Sp::unison, off } });
        for (auto& [slot, value] : slots)
            rig.setSlot (slot, value);
        auto x = rig.render (36000, { { 0, on (45) } });
        if (out != nullptr)
            *out = x;
        return x;
    };
    auto harmonic = [] (const std::vector<float>& x, int h) { return bandEnergy (x, 12000, 110.0 * h - 30.0, 110.0 * h + 30.0); };
    {
        const auto saw = spectrum ({}), odd = spectrum ({ { Sp::oddEven, 1.0f } }), even = spectrum ({ { Sp::oddEven, 0.0f } });
        CHECK (harmonic (odd, 2) < 1.0e-3f * harmonic (odd, 3) && harmonic (saw, 2) > 0.5f * harmonic (saw, 3), "Odd/Even at Odd removes the even harmonics");
        CHECK (harmonic (even, 3) < 1.0e-3f * harmonic (even, 2), "Odd/Even at Even removes the odd harmonics above the fundamental");
        const auto bright = spectrum ({ { Sp::tilt, 1.0f } }), dark = spectrum ({ { Sp::tilt, 0.0f } });
        auto share = [] (const std::vector<float>& x) { return bandEnergy (x, 12000, 1500.0, 20000.0) / bandEnergy (x, 12000, 50.0, 20000.0); };
        CHECK (share (bright) > 10.0f * share (saw) && share (dark) < 0.1f * share (saw), "Tilt brightens and darkens");
        // Formant: Vowel A's first formant (800 Hz) moves up an octave at +12 st, while the pitch stays
        const auto vowel = spectrum ({ { Sp::sourceA, src (6) } }), up = spectrum ({ { Sp::sourceA, src (6) }, { Sp::formant, 0.75f } });
        std::cout << "info: formant: vowel ratio " << bandEnergy (vowel, 12000, 1400.0, 1900.0) / bandEnergy (vowel, 12000, 650.0, 950.0)
                  << ", +12 st ratio " << bandEnergy (up, 12000, 1400.0, 1900.0) / bandEnergy (up, 12000, 650.0, 950.0)
                  << "\n";
        CHECK (bandEnergy (up, 12000, 1400.0, 1900.0) / bandEnergy (up, 12000, 650.0, 950.0)
                   > 4.0f * bandEnergy (vowel, 12000, 1400.0, 1900.0) / bandEnergy (vowel, 12000, 650.0, 950.0)
               && [&] { double onHarmonics = 0.0; for (int h = 1; h < 150; ++h) onHarmonics += harmonic (up, h);
                        return onHarmonics > 0.99 * bandEnergy (up, 12000, 50.0, 20000.0); }(),
               "Formant moves the resonances, not the pitch (every partial still on a harmonic of 110 Hz)");
        // Smear fills the gaps of a square
        const auto square = spectrum ({ { Sp::sourceA, src (1) } }), smeared = spectrum ({ { Sp::sourceA, src (1) }, { Sp::smear, 1.0f } });
        CHECK (harmonic (square, 4) < 1.0e-4f * harmonic (square, 3) && harmonic (smeared, 4) > 0.1f * harmonic (smeared, 3), "Smear fills the missing harmonics");
    }

    // Morph travels: halfway from Saw to Vowel O is neither endpoint, and the sweep never clicks
    {
        SpRig rig ({ { Sp::unison, off }, { Sp::sourceB, src (7) } });
        auto x = rig.render (12000, { { 0, on (45) } });
        float worst = 0.0f, base = maxStep (std::vector<float> (x.begin() + 6000, x.end()));
        for (int step = 1; step <= 20; ++step)
        {
            rig.setSlot (Sp::morph, step / 20.0f);
            auto y = rig.render (2400);
            worst = std::max (worst, maxStep (y));
        }
        auto end = rig.render (24000);
        base = std::max (base, maxStep (std::vector<float> (end.begin() + 12000, end.end())));
        CHECK (worst < 1.3f * base, "a Saw -> Vowel O morph sweep does not click (" + std::to_string (worst / base) + "x)");
    }

    // Shepard: the emphasis climbs forever; one full period later the spectrum is back where it was
    {
        SpRig rig ({ { Sp::unison, off }, { Sp::shepard, 1.0f }, { Sp::shepardRate, 1.0f } });   // +0.5 oct/s
        auto x = rig.render (48000 * 5, { { 0, on (33) } });
        auto centroid = [&] (size_t from)
        {
            double w = 0.0, sum = 0.0;
            for (int h = 1; h <= 64; ++h)
            {
                const double e = bandEnergy (x, from, 55.0 * h - 15.0, 55.0 * h + 15.0);
                w += e;
                sum += e * std::log2 ((double) h);
            }
            return sum / w;
        };
        const double c0 = centroid (48000), cHalf = centroid (48000 + 24000), c1 = centroid (48000 + 96000);
        std::cout << "info: Shepard centroid (log2 h) " << c0 << " -> " << cHalf << " -> " << c1 << " over one period\n";
        CHECK (std::abs (c1 - c0) < 0.15 && std::abs (cHalf - c0) > 0.2, "Shepard moves the emphasis and repeats every period");
        CHECK (allFinite (x), "Shepard renders cleanly");
    }

    // Unison: JP 7 at the default spreads wide, mono-compatible
    {
        SpRig rig;
        auto l = rig.render (36000, { { 0, on (57) } });
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 12000; i < l.size(); ++i) { lr += (double) l[i] * rig.right[i]; ll += (double) l[i] * l[i]; rr += (double) rig.right[i] * rig.right[i]; }
        const double c = lr / std::sqrt (ll * rr);
        CHECK (c < 0.9 && c > 0.0, "Spectral JP 7 unison is wide but mono-compatible (" + std::to_string (c) + ")");
    }
}

double spectralCpuShare (bool moving)
{
    Rig rig (false);
    for (int i = 0; i < 4; ++i)
        rig.proc->setLayerEngine (i, "spectral");
    rig.render (blockSize);
    rig.collectRetired();
    for (int i = 0; i < 4; ++i)
        if (moving)
        {
            rig.set (ps::params::layerSlot (i, Sp::shepard), 1.0f);
            rig.set (ps::params::layerSlot (i, Sp::shepardRate), 1.0f);
        }
    std::vector<std::pair<int, juce::MidiMessage>> chord;
    for (int n : { 36, 43, 48, 52, 55, 60, 64, 67 })
        chord.push_back ({ 0, on (n) });
    rig.render (blockSize, chord);
    double best = 1.0e9;
    for (int run = 0; run < 3; ++run)
    {
        const auto start = juce::Time::getHighResolutionTicks();
        rig.render ((int) sampleRate * 2);
        best = std::min (best, 50.0 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start));
    }
    return best;
}

void benchmarkSpectral()
{
    const double still = spectralCpuShare (false), moving = spectralCpuShare (true);
    std::cout << "info: 8-note chord on 4 Spectral layers (JP 7) uses " << still << "% of one core, " << moving << "% with the spectrum moving\n";
    CHECK (still < 20.0 && moving < 25.0, "Spectral stack CPU is within budget");
}

// ---- Dirt engine -----------------------------------------------------------------------------------------

struct DirtRig : Rig
{
    explicit DirtRig (std::initializer_list<std::pair<int, float>> slots = {})
    {
        proc->setLayerEngine (2, "dirt");
        render (blockSize);
        collectRetired();
        for (auto& [slot, value] : slots)
            setSlot (slot, value);
    }

    void setSlot (int slot, float value) { set (ps::params::layerSlot (2, slot), value); }
};

using Dirt = ps::DirtEngine;

// A bare oscillator: filter open, no envelope, drive, subs or drift
DirtRig dirtOsc (float shape, std::initializer_list<std::pair<int, float>> more = {})
{
    DirtRig rig ({ { Dirt::shape, shape }, { Dirt::sub1, 0.0f }, { Dirt::drive, 0.0f }, { Dirt::cutoff, 1.0f },
                   { Dirt::resonance, 0.0f }, { Dirt::envAmount, 0.5f }, { Dirt::keyTrack, 0.0f }, { Dirt::drift, 0.0f } });
    for (auto& [slot, value] : more)
        rig.setSlot (slot, value);
    return rig;
}

void testDirtEngine()
{
    {
        auto rig = dirtOsc (0.5f);
        auto x = rig.render (24000, { { 0, on (57) } });
        const double f = estimateFrequency (x, 4000, 24000);
        CHECK (std::abs (f - 220.0) < 0.2, "Dirt: A3 plays at 220 Hz with no drift (" + std::to_string (f) + " Hz)");
        double mean = 0.0;
        for (size_t i = 4000; i < 24000; ++i)
            mean += x[i];
        mean /= 20000.0;
        CHECK (std::abs (mean) < 0.01 * rms (x, 4000, 24000), "Dirt: the curved saw has no DC");
    }

    // Every shape at the saw's level
    {
        float sawRms = 0.0f, worst = 0.0f;
        for (float s : { 0.5f, 0.0f, 0.25f, 0.4f, 0.75f, 1.0f })
        {
            auto rig = dirtOsc (s);
            auto x = rig.render (24000, { { 0, on (45) } });
            const float level = rms (x, 6000, 24000);
            if (s == 0.5f)
                sawRms = level;
            worst = std::max (worst, std::abs (juce::Decibels::gainToDecibels (level / sawRms)));
        }
        CHECK (worst < 1.0f, "Dirt: every shape plays at the saw's level (worst " + std::to_string (worst) + " dB)");
    }

    // Band-limited: the curved reset, the triangle and the square
    for (int note : { 57, 69, 81, 93 })
        for (float s : { 0.0f, 0.3f, 0.5f, 1.0f })
        {
            auto rig = dirtOsc (s);
            auto x = rig.render (24000, { { 0, on (note) } });
            const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            const float alias = worstAliasDb (x, 4000, f0);
            CHECK (alias < -60.0f, "Dirt: shape " + std::to_string (s) + " is band-limited at " + std::to_string ((int) f0) + " Hz (aliases " + std::to_string (alias) + " dB)");
        }

    // Subs one and two octaves down
    {
        auto one = dirtOsc (0.5f, { { Dirt::sub1, 1.0f } });
        auto x = one.render (24000, { { 0, on (57) } });
        auto two = dirtOsc (0.5f, { { Dirt::sub2, 1.0f } });
        auto y = two.render (24000, { { 0, on (57) } });
        const float s1 = bandEnergy (x, 4000, 100.0, 120.0) / bandEnergy (x, 4000, 20.0, 20000.0);
        const float s2 = bandEnergy (y, 4000, 50.0, 60.0) / bandEnergy (y, 4000, 20.0, 20000.0);
        CHECK (s1 > 0.2f && s2 > 0.2f, "Dirt: the subs sound an octave and two octaves down (" + std::to_string (s1) + ", " + std::to_string (s2) + " of the energy)");
        CHECK (std::abs (juce::Decibels::gainToDecibels (rms (x, 6000, 24000) / rms (y, 6000, 24000))) < 1.0f, "Dirt: the subs keep the level");
    }

    // Resonance keeps the lows, on both filters
    for (int p = 0; p < Dirt::numPoles; ++p)
    {
        auto band = [p] (float res, double lo, double hi)
        {
            auto rig = dirtOsc (0.5f, { { Dirt::poles, Dirt::choiceValue (p, Dirt::numPoles) }, { Dirt::cutoff, Dirt::cutoffValue (800.0f) },
                                        { Dirt::resonance, res } });
            auto x = rig.render (24000, { { 0, on (33) } });
            return bandEnergy (x, 8000, lo, hi);
        };
        const float lowsDb = juce::Decibels::gainToDecibels (std::sqrt (band (0.9f, 40.0, 300.0) / band (0.0f, 40.0, 300.0)));
        const float peakDb = juce::Decibels::gainToDecibels (std::sqrt (band (0.9f, 650.0, 950.0) / band (0.0f, 650.0, 950.0)));
        const std::string name = p == 0 ? "2-pole" : "3-pole";
        CHECK (std::abs (lowsDb) < 1.5f, "Dirt " + name + ": resonance keeps the lows (" + std::to_string (lowsDb) + " dB)");
        CHECK (peakDb > 6.0f, "Dirt " + name + ": resonance lifts the cutoff band (" + std::to_string (peakDb) + " dB)");
    }

    for (int p = 0; p < Dirt::numPoles; ++p)
    {
        DirtRig rig ({ { Dirt::poles, Dirt::choiceValue (p, Dirt::numPoles) }, { Dirt::resonance, 1.0f }, { Dirt::drive, 1.0f },
                       { Dirt::cutoff, 0.5f }, { Dirt::sub2, 1.0f }, { Dirt::noise, 0.5f } });
        auto x = rig.render (48000, { { 0, on (36, 1.0f) }, { 0, on (60, 1.0f) }, { 0, on (79, 1.0f) } });
        CHECK (allFinite (x) && peak (x) < 4.0f, "Dirt: full resonance, drive and subs stay bounded (filter " + std::to_string (p) + ", peak " + std::to_string (peak (x)) + ")");
    }

    // Filter envelope: bright at the attack, darker once decayed
    {
        // +50 st, as in the JP's test; a decay long enough to span the 170 ms analysis window
        DirtRig rig ({ { Dirt::cutoff, Dirt::cutoffValue (300.0f) }, { Dirt::envAmount, 0.5f + 50.0f / 120.0f }, { Dirt::envSustain, 0.0f },
                       { Dirt::envDecay, Dirt::timeValue (1500.0f) }, { Dirt::velToEnv, 0.0f }, { Dirt::keyTrack, 0.0f } });
        auto x = rig.render (96000, { { 0, on (45) } });
        const float ratio = bandEnergy (x, 0, 1000.0, 20000.0) / bandEnergy (x, 72000, 1000.0, 20000.0);
        CHECK (ratio > 30.0f, "Dirt: the filter envelope opens and closes the filter (" + std::to_string (ratio) + "x the highs at the attack)");
    }

    // Drift: each re-roll retunes the voice within the Drift range
    {
        std::vector<double> cents;
        for (int seed = 0; seed < 4; ++seed)
        {
            auto rig = dirtOsc (0.5f, { { Dirt::drift, 1.0f }, { Dirt::reroll, Dirt::choiceValue (seed, Dirt::numSeeds) } });
            auto x = rig.render (24000, { { 0, on (57) } });
            cents.push_back (1200.0 * std::log2 (estimateFrequency (x, 4000, 24000) / 220.0));
        }
        const auto [lo, hi] = std::minmax_element (cents.begin(), cents.end());
        CHECK (*hi - *lo > 2.0 && std::abs (*lo) < 15.5 && std::abs (*hi) < 15.5,
               "Dirt: re-roll gives each voice a new tuning inside the drift range (" + std::to_string (*lo) + " .. " + std::to_string (*hi) + " ct)");
    }

    // Loudness against the JP's default patch
    {
        DirtRig dirt;
        JpRig jp;
        auto a = dirt.render (24000, { { 0, on (48) } });
        auto b = jp.render (24000, { { 0, on (48) } });
        const float diff = juce::Decibels::gainToDecibels (rms (a, 12000, 24000) / rms (b, 12000, 24000));
        std::cout << "info: Dirt default patch vs JP default: " << diff << " dB\n";
        CHECK (std::abs (diff) < 3.0f, "Dirt: the default patch plays near the other engines' level");
    }
}

void testDirtSwitchesDoNotClick()
{
    const char* names[] = { "shape", "filter poles", "sub -1", "sub -2", "drive", "cutoff", "resonance", "drift", "re-roll" };
    const std::pair<int, float> moves[] = {
        { Dirt::shape, 1.0f }, { Dirt::poles, Dirt::choiceValue (0, Dirt::numPoles) }, { Dirt::sub1, 1.0f }, { Dirt::sub2, 1.0f },
        { Dirt::drive, 0.8f }, { Dirt::cutoff, 0.3f }, { Dirt::resonance, 0.8f }, { Dirt::drift, 1.0f },
        { Dirt::reroll, Dirt::choiceValue (9, Dirt::numSeeds) } };
    for (size_t m = 0; m < std::size (moves); ++m)
    {
        DirtRig rig ({ { Dirt::cutoff, 0.45f }, { Dirt::envAmount, 0.5f } });
        auto x = rig.render (12000, { { 0, on (45) } });
        const float baseline = maxStep (std::vector<float> (x.begin() + 6000, x.end()));
        rig.setSlot (moves[m].first, moves[m].second);
        auto y = rig.render (9600);
        const float steady = std::max (baseline, maxStep (std::vector<float> (y.begin() + 4800, y.end())));
        const float worst = maxStep (std::vector<float> (y.begin(), y.begin() + 2400));
        CHECK (worst < steady * 1.5f, std::string ("Dirt: changing ") + names[m] + " mid-note does not click (" + std::to_string (worst / steady) + "x the steady step)");
    }
}

void benchmarkDirt()
{
    Rig rig (false);
    for (int i = 0; i < 4; ++i)
        rig.proc->setLayerEngine (i, "dirt");
    rig.render (blockSize);
    rig.collectRetired();
    std::vector<std::pair<int, juce::MidiMessage>> chord;
    for (int n : { 36, 43, 48, 52, 55, 60, 64, 67 })
        chord.push_back ({ 0, on (n) });
    rig.render (blockSize, chord);
    double best = 1.0e9;
    for (int run = 0; run < 3; ++run)
    {
        const auto start = juce::Time::getHighResolutionTicks();
        rig.render ((int) sampleRate * 2);
        best = std::min (best, 50.0 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start));
    }
    std::cout << "info: 8-note chord on 4 Dirt layers uses " << best << "% of one core\n";
    CHECK (best < 20.0, "Dirt stack CPU is within budget");
}

// The patch's master chain: OTT, glue, width
void testMasterChain()
{
    namespace p = ps::params;
    auto levelDb = [] (std::initializer_list<std::pair<juce::ParameterID, float>> settings, float velocity)
    {
        Rig rig;
        for (auto& [id, v] : settings)
            rig.set (id, v);
        auto x = rig.render (48000, { { 0, on (57, velocity) } });
        return juce::Decibels::gainToDecibels (rms (x, 24000, 48000));
    };
    // OTT: quiet notes come up, loud ones go down, so the range between them shrinks
    const float rangeDry = levelDb ({}, 1.0f) - levelDb ({}, 0.15f);
    const float rangeOtt = levelDb ({ { p::ottAmount, 100.0f } }, 1.0f) - levelDb ({ { p::ottAmount, 100.0f } }, 0.15f);
    CHECK (rangeOtt < rangeDry - 3.0f, "OTT squeezes the dynamic range (" + std::to_string (rangeDry) + " -> " + std::to_string (rangeOtt) + " dB)");
    // Upward lifts only what is well under the thresholds: a very quiet note
    const float lifted = levelDb ({ { p::ottAmount, 100.0f } }, 0.01f) - levelDb ({ { p::ottAmount, 100.0f }, { p::ottUpward, 0.0f } }, 0.01f);
    CHECK (lifted > 3.0f, "OTT Upward lifts the quiet parts, and 0 % turns that off (" + std::to_string (lifted) + " dB)");

    // Glue: about its set reduction on a steady note
    const float glued = levelDb ({ { p::glueAmount, 6.0f } }, 0.8f) - levelDb ({}, 0.8f);
    CHECK (glued < -2.0f && glued > -9.0f, "Glue pulls the level down by its amount (" + std::to_string (glued) + " dB)");

    // Width: the side above 250 Hz scales with Width; the lows stay mono
    auto sideRatio = [] (float width)
    {
        JpRig rig ({ { Jp::spread, 1.0f } });
        rig.set (p::stackWidth, width);
        auto l = rig.render (48000, { { 0, on (45) } });
        double side = 0.0, mid = 0.0;
        for (size_t i = 24000; i < l.size(); ++i)
        {
            side += juce::square (0.5 * (l[i] - rig.right[i]));
            mid += juce::square (0.5 * (l[i] + rig.right[i]));
        }
        return std::sqrt (side / mid);
    };
    const double s0 = sideRatio (0.0f), s100 = sideRatio (100.0f), s200 = sideRatio (200.0f);
    CHECK (s0 < 1.0e-4 && std::abs (s200 / s100 - 2.0) < 0.05,
           "Width scales the side signal (0 %: " + std::to_string (s0) + ", 200 %: " + std::to_string (s200 / s100) + "x)");

    // Turning the chain up mid-note doesn't click
    {
        Rig rig;
        auto x = rig.render (24000, { { 0, on (45) } });
        const float steady = maxStep (std::vector<float> (x.begin() + 12000, x.end()));
        rig.set (p::ottAmount, 100.0f);
        rig.set (p::glueAmount, 6.0f);
        rig.set (p::stackWidth, 200.0f);
        auto y = rig.render (4800);
        CHECK (maxStep (y) < steady * 1.5f, "the master chain switches in without a click (" + std::to_string (maxStep (y) / steady) + "x)");
    }
}

// The sub ducks the body's lows while it sounds; without a sub the body is untouched
void testSubDuck()
{
    auto render = [] (bool withSub, bool withBody, float duck)
    {
        Rig rig (false);
        rig.set (ps::params::monoBass, 0.0f);
        for (int i = 0; i < ps::params::numLayers; ++i)
            rig.proc->setLayerEngine (i, i == 0 && withSub ? "sub" : i == 1 && withBody ? "test.sine" : "");
        rig.render (blockSize);
        rig.collectRetired();
        rig.set (ps::params::subDuck, duck);
        return rig.render (48000, { { 0, on (48) } });
    };
    const auto subOnly = render (true, false, 0.0f), dry = render (true, true, 0.0f), ducked = render (true, true, 100.0f);
    std::vector<float> bodyDry (dry.size()), bodyDucked (dry.size());
    for (size_t i = 0; i < dry.size(); ++i)
    {
        bodyDry[i] = dry[i] - subOnly[i];
        bodyDucked[i] = ducked[i] - subOnly[i];
    }
    const float dip = juce::Decibels::gainToDecibels (rms (bodyDucked, 24000, 48000) / rms (bodyDry, 24000, 48000));
    CHECK (dip < -3.0f && dip > -9.5f, "Sub Duck: the body's lows give way to the sub (" + std::to_string (dip) + " dB)");
    CHECK (render (false, true, 100.0f) == render (false, true, 0.0f), "Sub Duck: without a sub the body is untouched");
    const float steady = maxStep (std::vector<float> (dry.begin() + 24000, dry.end()));
    CHECK (maxStep (std::vector<float> (ducked.begin() + 2000, ducked.end())) < 1.5f * steady, "Sub Duck: ducking never clicks");
}

// The layer insert slots: every type is exactly dry at Amount 0, stays level-sane at full Amount, the
// frequency shifter and EQ land where their controls say, and type changes and swaps never click
struct InsertRig : Rig
{
    // Layer 3 plays: the test sine, or a single JP saw
    explicit InsertRig (bool saw)
    {
        if (saw)
        {
            proc->setLayerEngine (2, "jp");
            render (blockSize);
            collectRetired();
            set (ps::params::layerSlot (2, ps::JpEngine::unison), ps::JpEngine::choiceValue (0, ps::JpEngine::numUnison));
            set (ps::params::layerSlot (2, ps::JpEngine::spread), 0.0f);
        }
    }

    void insert (int slot, ps::inserts::Type t, float amount, std::array<float, 3> controls = { -1.0f, -1.0f, -1.0f })
    {
        namespace p = ps::params;
        set (p::layerInsertType (2, slot), (float) t);
        set (p::layerInsertAmount (2, slot), amount);
        for (int c = 0; c < 3; ++c)
            set (p::layerInsertControl (2, slot, c), controls[(size_t) c] >= 0.0f ? controls[(size_t) c]
                                                                                 : ps::inserts::controls[(size_t) t][(size_t) c].defaultValue);
    }
};

void testInserts()
{
    using T = ps::inserts::Type;
    const auto dry = InsertRig (true).render (48000, { { 0, on (57) } });
    const auto drySine = InsertRig (false).render (48000, { { 0, on (57) } });
    const float dryLevel = rms (dry, 24000, 48000);
    for (int t = 1; t < ps::inserts::numTypes; ++t)
    {
        const auto name = ps::params::insertTypeNames[t].toStdString();
        {
            InsertRig rig (true);
            rig.insert (0, (T) t, 0.0f);
            CHECK (rig.render (48000, { { 0, on (57) } }) == dry, name + " at Amount 0 is exactly the dry signal");
        }
        InsertRig rig (true);
        if ((T) t == T::eq)
            rig.insert (1, T::eq, 100.0f, { 0.8f, 0.3f, 0.7f });
        else
            rig.insert (1, (T) t, 100.0f);
        const auto x = rig.render (48000, { { 0, on (57) } });
        const float db = juce::Decibels::gainToDecibels (rms (x, 24000, 48000) / dryLevel);
        float diff = 0.0f;
        for (size_t i = 24000; i < x.size(); ++i)
            diff = std::max (diff, std::abs (x[i] - dry[i]));
        std::cout << "info: insert " << name << " at 100 %: " << db << " dB\n";
        CHECK (allFinite (x) && diff > 1.0e-3f, name + " at full Amount changes the sound");
        CHECK (std::abs (db) < 6.0f, name + " at full Amount stays within 6 dB of dry (" + std::to_string (db) + " dB)");
        CHECK (peak (x) < 1.0f, name + " at full Amount stays below 0 dBFS");
    }

    // Frequency shifter: 440 Hz shifted +100 Hz lands at 540 Hz, the image at 340 Hz stays far below
    {
        InsertRig rig (false);
        const float s = std::cbrt (100.0f / 2000.0f);
        rig.insert (0, T::freqShift, 100.0f, { 0.5f + 0.5f * s, 0.0f, 0.0f });
        const auto x = rig.render (48000, { { 0, on (69) } });
        const float up = bandEnergy (x, 24000, 520.0, 560.0), image = bandEnergy (x, 24000, 320.0, 360.0), left = bandEnergy (x, 24000, 420.0, 460.0);
        const float imageDb = 10.0f * std::log10 (image / up), leftDb = 10.0f * std::log10 (left / up);
        std::cout << "info: freq shift +100 Hz: image " << imageDb << " dB, original " << leftDb << " dB\n";
        CHECK (imageDb < -30.0f && leftDb < -30.0f, "Freq Shift moves 440 Hz to 540 Hz");
    }

    // EQ: +12 dB low shelf on 55 Hz, -12 dB bell on 988 Hz
    auto eqGainDb = [&] (int note, std::array<float, 3> controls)
    {
        InsertRig rig (false), ref (false);
        rig.insert (0, T::eq, 100.0f, controls);
        const auto x = rig.render (48000, { { 0, on (note) } }), y = ref.render (48000, { { 0, on (note) } });
        return juce::Decibels::gainToDecibels (rms (x, 24000, 48000) / rms (y, 24000, 48000));
    };
    const float low = eqGainDb (33, { 1.0f, 0.5f, 0.5f }), mid = eqGainDb (83, { 0.5f, 0.0f, 0.5f });
    CHECK (low > 11.0f && low < 12.5f, "EQ low shelf +12 dB at 55 Hz (" + std::to_string (low) + " dB)");
    CHECK (mid < -11.0f && mid > -12.5f, "EQ bell -12 dB at 988 Hz (" + std::to_string (mid) + " dB)");

    // Transient: Attack up makes the onset louder against the sustain
    {
        InsertRig rig (false);
        rig.insert (0, T::transient, 100.0f, { 1.0f, 0.5f, 0.5f });
        rig.render (blockSize);
        const auto x = rig.render (24000, { { 0, on (57) } });
        const float punch = rms (x, 0, 480) / rms (x, 12000, 24000), dryPunch = rms (drySine, 0, 480) / rms (drySine, 12000, 24000);
        const float liftDb = juce::Decibels::gainToDecibels (punch / dryPunch);
        CHECK (liftDb > 3.0f, "Transient Attack lifts the onset (" + std::to_string (liftDb) + " dB)");
    }

    // Order matters (drive into EQ is not EQ into drive), and the swap is repeatable
    {
        auto play = [] (bool swap)
        {
            InsertRig rig (true);
            rig.insert (0, T::drive, 100.0f);
            rig.insert (1, T::eq, 100.0f, { 1.0f, 0.5f, 0.0f });
            rig.set (ps::params::layerInsertSwap (2), swap ? 1.0f : 0.0f);
            return rig.render (24000, { { 0, on (45) } });
        };
        CHECK (play (false) != play (true) && play (true) == play (true), "insert order changes the sound; the same order renders the same");
    }

    // Type changes, Amount jumps and swaps mid-note never click. On a 220 Hz sine, a click is broadband, so it
    // shows above 5 kHz (where the tone has nothing) beyond what the effects themselves put there before and
    // after the change. A slot passes the dry signal while it switches, so the level itself may move.
    {
        InsertRig rig (false);
        ps::LinkwitzRiley4 high (ps::LinkwitzRiley4::Type::highPass);
        high.setCutoff (5000.0, sampleRate);
        auto highPeak = [] (const std::vector<float>& x, size_t from, size_t to)
        {
            float m = 0.0f;
            for (size_t i = from; i < to; ++i)
                m = std::max (m, std::abs (x[i]));
            return m;
        };
        auto filtered = [&] (std::vector<float> x)
        {
            for (auto& v : x)
                v = (float) high.process (v);
            return x;
        };
        auto steady = filtered (rig.render (12000, { { 0, on (57) } }));
        float before = highPeak (steady, 9600, 12000), worst = 0.0f;
        std::string worstAt;
        auto step = [&] (const std::string& what, std::function<void()> change)
        {
            change();
            auto x = filtered (rig.render (4800));
            const float after = highPeak (x, 2400, 4800);
            const float r = highPeak (x, 0, 2400) / std::max ({ before, after, 1.0e-4f });
            if (r > worst)
            {
                worst = r;
                worstAt = what;
            }
            before = after;
        };
        for (int t = 1; t < ps::inserts::numTypes; ++t)
            step (ps::params::insertTypeNames[t].toStdString(), [&] { rig.insert (0, (T) t, 100.0f); });
        step ("Drive mode", [&] { rig.insert (0, T::drive, 100.0f, { 0.9f, 0.5f, 0.0f }); });
        step ("second slot", [&] { rig.insert (1, T::phaser, 100.0f); });
        step ("swap", [&] { rig.set (ps::params::layerInsertSwap (2), 1.0f); });
        step ("amount 0", [&] { rig.set (ps::params::layerInsertAmount (2, 1), 0.0f); });
        step ("off", [&] { rig.insert (0, T::off, 0.0f); rig.insert (1, T::off, 0.0f); });
        CHECK (worst < 1.5f, "insert changes never click (worst " + std::to_string (worst) + "x the steady level above 5 kHz, at " + worstAt + ")");
    }
}

// Mix Health: the meters agree with the offline BS.1770 measurements, watching never changes the sound, the
// listen modes do what they say without clicking, and two layers on the same band are flagged
void testMixHealth()
{
    using MH = ps::MixHealth;
    auto play = [] (bool watching, std::vector<float>* rightOut = nullptr)
    {
        JpRig rig ({ { Jp::spread, 1.0f } });
        rig.proc->getMixHealth().setActive (watching);
        auto x = rig.render (192000, { { 0, on (45) }, { 0, on (57) }, { 0, on (64) } });
        if (rightOut != nullptr)
            *rightOut = rig.right;
        const auto& r = rig.proc->getMixHealth().readings();
        return std::make_tuple (x, r.truePeakHoldDb.load(), r.integratedLufs.load(), r.shortTermLufs.load(), r.correlation.load());
    };
    std::vector<float> right;
    const auto [watched, tp, integrated, shortTerm, corr] = play (true, &right);
    const auto unwatched = std::get<0> (play (false));
    CHECK (watched == unwatched, "Mix Health watching never changes the sound");

    const double tpOffline = std::max (ps::loudness::truePeakDb (watched.data(), watched.size()), ps::loudness::truePeakDb (right.data(), right.size()));
    const double lufsOffline = ps::loudness::integratedLufs (watched.data(), right.data(), watched.size(), sampleRate);
    std::cout << "info: Mix Health true peak " << tp << " dBTP (offline " << tpOffline << "), integrated " << integrated << " LUFS (offline "
              << lufsOffline << "), short-term " << shortTerm << ", correlation " << corr << "\n";
    CHECK (std::abs (tp - tpOffline) < 0.2, "Mix Health true peak matches the offline measurement");
    CHECK (std::abs (integrated - lufsOffline) < 0.3, "Mix Health integrated loudness matches the offline measurement");
    CHECK (std::abs (shortTerm - integrated) < 1.5f, "short-term loudness of a steady chord sits at the integrated");
    CHECK (corr > 0.0f && corr < 0.999f, "a wide supersaw reads as wide but mono-compatible (" + std::to_string (corr) + ")");

    // The mono sine: correlation 1 and no side in the lows
    {
        Rig rig;
        rig.proc->getMixHealth().setActive (true);
        rig.render (48000, { { 0, on (33) } });
        const auto& r = rig.proc->getMixHealth().readings();
        CHECK (r.correlation.load() > 0.999f && r.sideLowsDb.load() < -60.0f, "a centred sine reads mono, with no side in the lows");
    }

    // Listen modes: mono makes the channels equal; phone takes out the lows; switching never clicks
    {
        JpRig rig ({ { Jp::spread, 1.0f } });
        auto& mh = rig.proc->getMixHealth();
        rig.render (24000, { { 0, on (57) } });
        mh.setListen (MH::Listen::mono);
        rig.render (4800);
        auto l = rig.render (4800);
        float diff = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
            diff = std::max (diff, std::abs (l[i] - rig.right[i]));
        CHECK (diff < 1.0e-6f, "Mono club listen sums to mono");
        mh.setListen (MH::Listen::normal);
        rig.render (4800);
    }
    {
        auto lowLevel = [] (MH::Listen mode)
        {
            Rig rig;
            rig.proc->getMixHealth().setListen (mode);
            auto x = rig.render (48000, { { 0, on (33) } });
            return rms (x, 24000, 48000);
        };
        const float cut = juce::Decibels::gainToDecibels (lowLevel (MH::Listen::phone) / lowLevel (MH::Listen::normal));
        CHECK (cut < -20.0f, "Phone listen takes 55 Hz down by more than 20 dB (" + std::to_string (cut) + " dB)");

        Rig rig;
        auto& mh = rig.proc->getMixHealth();
        auto steady = rig.render (12000, { { 0, on (57) } });
        const float base = maxStep (std::vector<float> (steady.begin() + 6000, steady.end()));
        float worst = 0.0f;
        for (auto mode : { MH::Listen::phone, MH::Listen::mono, MH::Listen::normal })
        {
            mh.setListen (mode);
            worst = std::max (worst, maxStep (rig.render (4800)));
        }
        CHECK (worst < 1.1f * base, "switching listen modes never clicks (" + std::to_string (worst / base) + "x)");
    }

    // Masking: two sines on the same note fight over the 400 Hz band; one alone doesn't
    {
        auto bandBits = [] (bool both)
        {
            Rig rig;
            if (both)
            {
                rig.proc->setLayerEngine (3, "test.sine");
                rig.render (blockSize);
                rig.collectRetired();
            }
            rig.proc->getMixHealth().setActive (true);
            rig.render (24000, { { 0, on (67) } });   // 392 Hz
            return rig.proc->getMixHealth().readings().maskedLayers[1].load();
        };
        CHECK (bandBits (true) == ((1u << 2) | (1u << 3)) && bandBits (false) == 0u, "two layers on the same band are flagged as masking");
    }
}

// ---- Potato Ready: every factory preset, rendered and checked (brief, "Mix/master-ready standard") --------------

namespace potatoReady
{
    using Category = ps::presets::Category;

    // Integrated loudness of the held chord each category sits at (presets match each other within +-1 LU)
    // Plucks, arps and keys sit lower: their transients peak 13-18 dB over their loudness, so a louder target would
    // only be reached by the safe clipper squashing the attack. Init is a category of one: the fresh-instance patch.
    float targetLufs (Category c)
    {
        switch (c)
        {
            case Category::init:  return -9.0f;
            case Category::lead:  return -11.0f;
            case Category::bass:  return -11.0f;
            case Category::pad:   return -14.0f;
            case Category::pluck: return -19.0f;
            case Category::keys:  return -15.0f;
            case Category::arp:   return -16.0f;
            case Category::fx:    return -16.0f;
            case Category::count: break;
        }
        return -12.0f;
    }

    // How long a held chord needs before it is at its sustained level (slow pads and risers take longer)
    int settleSamples (Category c) { return (int) sampleRate * (c == Category::pad || c == Category::fx ? 3 : 1); }

    struct PresetRig : Rig
    {
        explicit PresetRig (const ps::presets::Preset& preset) : Rig (false)
        {
            proc->loadPreset (preset);
            render (blockSize);
            collectRetired();
            render (blockSize * 8);
        }
    };

    // The held chord at full velocity; basses (mono) play one low note
    std::vector<int> chordNotes (Category c) { return c == Category::bass ? std::vector<int> { 36 } : std::vector<int> { 36, 48, 55, 60, 64, 67 }; }
    std::vector<std::pair<int, juce::MidiMessage>> chordOn (Category c)
    {
        std::vector<std::pair<int, juce::MidiMessage>> events;
        for (int n : chordNotes (c))
            events.push_back ({ 0, on (n, 1.0f) });
        return events;
    }

    double lufs (const std::vector<float>& l, const std::vector<float>& r, size_t from, size_t to)
    {
        return ps::loudness::integratedLufs (l.data() + from, r.data() + from, to - from, sampleRate);
    }

    double truePeak (const std::vector<float>& l, const std::vector<float>& r)
    {
        return std::max (ps::loudness::truePeakDb (l.data(), l.size()), ps::loudness::truePeakDb (r.data(), r.size()));
    }

    // Peak above 8 kHz in [from, to), for the click checks
    std::vector<float> highBand (const std::vector<float>& x)
    {
        ps::LinkwitzRiley4 hp (ps::LinkwitzRiley4::Type::highPass);
        hp.setCutoff (8000.0, sampleRate);
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            y[i] = (float) hp.process (x[i]);
        return y;
    }

    // Octave-band levels (dB), 63 Hz .. 16 kHz
    std::array<float, 9> octaves (const std::vector<float>& x, size_t from)
    {
        std::array<float, 9> b {};
        for (int k = 0; k < 9; ++k)
        {
            const double centre = 62.5 * std::pow (2.0, k);
            b[(size_t) k] = (float) (10.0 * std::log10 (std::max (1.0e-20f, bandEnergy (x, from, centre / std::sqrt (2.0), centre * std::sqrt (2.0)))));
        }
        return b;
    }

    struct Result
    {
        std::string name, category;
        double lufs = 0.0, peak = 0.0, sideLow = 0.0, correlation = 0.0, dc = 0.0;
        double worstSweepPeak = -100.0, worstSweepLu = 0.0;
        std::string worstSweepKnob;
        int roleLayers = 0;
        std::vector<std::string> failures;
    };

    Result check (const ps::presets::Preset& preset)
    {
        Result res;
        res.name = preset.name.toStdString();
        res.category = ps::presets::categoryNames[(int) preset.category];
        auto fail = [&res] (const std::string& what) { res.failures.push_back (what); };

        // Layers: at least three role-tagged layers (Sub/Body/Mid/Air) with an engine
        {
            PresetRig rig (preset);
            for (int i = 0; i < ps::params::numLayers; ++i)
                if (rig.proc->getLayerEngine (i).isNotEmpty() && rig.get (ps::params::layerRole (i)) < (float) ps::params::Role::full)
                    ++res.roleLayers;
        }
        if (res.roleLayers < 3 && ! preset.solo)
            fail ("fewer than 3 role layers");

        // Held chord at full velocity
        const int settle = settleSamples (preset.category), hold = 96000;
        std::vector<float> l, r;
        {
            PresetRig rig (preset);
            auto events = chordOn (preset.category);
            for (int n : chordNotes (preset.category))
                events.push_back ({ settle + hold, juce::MidiMessage::noteOff (1, n) });
            l = rig.render (settle + hold + 48000, events);
            r = rig.right;
        }
        if (! allFinite (l) || ! allFinite (r))
        {
            fail ("NaN or Inf");
            return res;
        }
        // Measured from the first note-on to the note-off: integrated (the gates leave out what has decayed away),
        // except FX at their loudest 3 s (short-term), since a riser or a hit is heard at its peak, not on average
        const size_t a = 0, b = (size_t) (settle + hold);
        res.lufs = preset.category == Category::fx ? ps::loudness::maxShortTermLufs (l.data(), r.data(), b, sampleRate)
                                                   : lufs (l, r, a, b);
        res.peak = truePeak (l, r);
        if (res.peak > -1.0 + 0.05)
            fail ("true peak " + std::to_string (res.peak) + " dBTP");
        if (std::abs (res.lufs - targetLufs (preset.category)) > 1.0)
            fail ("loudness " + std::to_string (res.lufs) + " LUFS (target " + std::to_string (targetLufs (preset.category)) + ")");
        {
            // The lows through an 8th-order low-pass at 100 Hz, so the mono-bass crossover's own skirt (stereo is
            // allowed from 120 Hz up) doesn't count
            std::array<ps::LinkwitzRiley4, 4> low;
            for (auto& f : low)
                f.setCutoff (100.0, sampleRate);
            double midP = 0.0, sideP = 0.0, lr = 0.0, ll = 0.0, rr = 0.0, sumL = 0.0, sumR = 0.0;
            for (size_t i = a; i < b; ++i)
            {
                const double xl = low[1].process (low[0].process (l[i])), xr = low[3].process (low[2].process (r[i]));
                midP += juce::square (0.5 * (xl + xr));
                sideP += juce::square (0.5 * (xl - xr));
                lr += (double) l[i] * r[i];
                ll += (double) l[i] * l[i];
                rr += (double) r[i] * r[i];
                sumL += l[i];
                sumR += r[i];
            }
            res.sideLow = 10.0 * std::log10 (std::max (sideP, 1.0e-20) / std::max (midP, 1.0e-20));
            res.correlation = lr / std::sqrt (std::max (ll * rr, 1.0e-30));
            res.dc = std::max (std::abs (sumL), std::abs (sumR)) / (double) (b - a);
        }
        if (res.sideLow > -40.0)
            fail ("side below 100 Hz at " + std::to_string (res.sideLow) + " dB");
        if (res.correlation < 0.2)
            fail ("correlation " + std::to_string (res.correlation));
        if (res.dc > 2.0e-3)
            fail ("DC offset " + std::to_string (res.dc));
        // Note-off: what the release adds above 8 kHz in its first 10 ms stays within what the held sound had
        {
            const auto hl = highBand (l);
            const float held = peak (hl, b - 4800, b), released = peak (hl, b, b + 480);
            if (released > 2.0f * held + 1.0e-3f)
                fail ("click at note-off (" + std::to_string (released / std::max (held, 1.0e-6f)) + "x)");
        }
        // Note-on: a single note's first 5 ms above 8 kHz stays within what its next 200 ms have
        {
            PresetRig rig (preset);
            const auto x = rig.render (24000, { { 0, on (preset.category == Category::bass ? 36 : 60, 1.0f) } });
            const auto hx = highBand (x);
            const float onset = peak (hx, 0, 240), after = peak (hx, 240, 9840);
            if (onset > 2.0f * after + 1.0e-3f)
                fail ("click at note-on (" + std::to_string (onset / std::max (after, 1.0e-6f)) + "x)");
        }

        return res;
    }

    // Macros: every chain knob and SUPER in turn, on the held chord (struck again for each knob), swept 0 -> 1 over
    // 2 s, held, then back to 0 over 0.5 s. The whole run stays under the true-peak ceiling. 0.5 s after each sweep
    // the chord is struck again at the knob's new position, and over the next 2 s its loudness is within 1 LU of an
    // untouched copy of the preset at the same moment (so a sound that evolves by itself, like a riser, isn't counted
    // against the knob, and a pluck is measured on its notes rather than its tail). The rigs are built here, on the calling
    // thread; run() only renders, so several presets can run at once.
    struct SweepRun
    {
        explicit SweepRun (const ps::presets::Preset& p) : preset (p), swept (p), control (p) {}

        void run()
        {
            const auto chord = chordOn (preset.category);
            swept.render (settleSamples (preset.category), chord);
            control.render (settleSamples (preset.category), chord);
            std::vector<juce::ParameterID> knobs;
            for (int k = 0; k < ps::params::numChainKnobs; ++k)
                knobs.push_back (ps::params::chainKnob (k));
            knobs.push_back (ps::params::super);
            for (size_t k = 0; k < knobs.size(); ++k)
            {
                const std::string name = k < (size_t) ps::params::numChainKnobs ? ps::params::chainKnobNames[k] : "SUPER";
                std::vector<float> l, r;
                auto keep = [&] (const std::vector<float>& x)
                {
                    l.insert (l.end(), x.begin(), x.end());
                    r.insert (r.end(), swept.right.begin(), swept.right.end());
                };
                auto move = [&] (float from, float to, int length, std::vector<std::pair<int, juce::MidiMessage>> events = {})
                {
                    for (int pos = 0; pos < length; pos += blockSize)
                    {
                        swept.set (knobs[k], from + (to - from) * (float) (pos + blockSize) / (float) length);
                        keep (swept.render (blockSize, pos == 0 ? events : std::vector<std::pair<int, juce::MidiMessage>> {}));
                        control.render (blockSize, pos == 0 ? events : std::vector<std::pair<int, juce::MidiMessage>> {});
                    }
                };
                move (0.0f, 1.0f, 96000, chord);
                keep (swept.render (24000));
                control.render (24000);
                const auto a = swept.render (96000, chord);
                const auto ar = swept.right;
                const auto c = control.render (96000, chord);
                const double change = lufs (a, ar, 0, a.size()) - lufs (c, control.right, 0, c.size());
                keep (a);
                move (1.0f, 0.0f, 24000);
                keep (swept.render (24000));
                control.render (24000);

                const double tp = truePeak (l, r);
                if (tp > res.worstPeak)
                    res.worstPeak = tp;
                if (std::isfinite (change) && std::abs (change) > std::abs (res.worstLu))
                {
                    res.worstLu = change;
                    res.worstKnob = name;
                }
                if (tp > -1.0 + 0.05)
                    res.failures.push_back (name + " sweep true peak " + std::to_string (tp));
                if (std::isfinite (change) && std::abs (change) > 1.0)
                    res.failures.push_back (name + " sweep moves loudness " + std::to_string (change) + " LU");
            }
        }

        const ps::presets::Preset& preset;
        PresetRig swept, control;
        struct { double worstPeak = -100.0, worstLu = 0.0; std::string worstKnob; std::vector<std::string> failures; } res;
    };
}

void testFactoryPresets()
{
    using namespace potatoReady;
    const auto& bank = ps::presets::factory();
    CHECK (bank.size() == 201 && bank[0].name == "Init", "the factory bank is Init plus 200 presets");
    std::map<std::string, int> names;
    for (const auto& x : bank)
        ++names[x.name.toStdString()];
    CHECK (std::all_of (names.begin(), names.end(), [] (auto& n) { return n.second == 1; }), "preset names are unique");

    const auto calibrate = juce::SystemStats::getEnvironmentVariable ("POTATO_PRESET_CALIBRATE", {});
    const bool sweeps = calibrate.isEmpty() && juce::SystemStats::getEnvironmentVariable ("POTATO_PRESET_NO_SWEEPS", {}).isEmpty();
    const auto only = juce::SystemStats::getEnvironmentVariable ("POTATO_PRESET_ONLY", {});
    juce::String trims = "// Generated by the Potato Ready calibration (PotatoSynthTests with POTATO_PRESET_CALIBRATE): { \"preset name\", master dB },\n";
    int passed = 0, checked = 0;
    std::vector<potatoReady::Result> results;
    std::vector<const ps::presets::Preset*> tested;
    for (const auto& preset : bank)
    {
        // POTATO_PRESET_ONLY: a comma-separated list of name fragments; calibrating keeps the others' trims
        bool wanted = only.isEmpty();
        for (const auto& part : juce::StringArray::fromTokens (only, ",", {}))
            wanted = wanted || preset.name.containsIgnoreCase (part.trim());
        if (! wanted)
        {
            if (calibrate.isNotEmpty() && ps::presets::trimDb (preset.name) != 0.0f)
                trims << "{ \"" << preset.name << "\", " << juce::String (ps::presets::trimDb (preset.name), 2) << "f },\n";
            continue;
        }
        ++checked;
        results.push_back (check (preset));
        tested.push_back (&preset);
    }
    if (sweeps)
    {
        // A batch of presets per round, one thread each
        const size_t threads = std::max (1u, std::thread::hardware_concurrency());
        for (size_t start = 0; start < tested.size(); start += threads)
        {
            std::vector<std::unique_ptr<potatoReady::SweepRun>> runs;
            for (size_t i = start; i < std::min (tested.size(), start + threads); ++i)
                runs.push_back (std::make_unique<potatoReady::SweepRun> (*tested[i]));
            std::vector<std::thread> workers;
            for (auto& run : runs)
                workers.emplace_back ([&run] { run->run(); });
            for (auto& w : workers)
                w.join();
            for (size_t i = 0; i < runs.size(); ++i)
            {
                auto& res = results[start + i];
                res.worstSweepPeak = runs[i]->res.worstPeak;
                res.worstSweepLu = runs[i]->res.worstLu;
                res.worstSweepKnob = runs[i]->res.worstKnob;
                res.failures.insert (res.failures.end(), runs[i]->res.failures.begin(), runs[i]->res.failures.end());
            }
        }
    }
    for (size_t i = 0; i < results.size(); ++i)
    {
        const auto& preset = *tested[i];
        const auto& res = results[i];
        std::cout << "preset: " << res.name << " | " << res.category << " | " << res.lufs << " LUFS | " << res.peak << " dBTP | side lows "
                  << res.sideLow << " dB | corr " << res.correlation << " | sweeps " << res.worstSweepPeak << " dBTP, "
                  << res.worstSweepLu << " LU (" << res.worstSweepKnob << ")\n";
        if (calibrate.isNotEmpty())
        {
            // Capped where the layers can't go higher, so a preset that can't get louder doesn't keep climbing
            const float trim = juce::jmin (18.0f, ps::presets::trimDb (preset.name) + (float) (targetLufs (preset.category) - res.lufs));
            trims << "{ \"" << preset.name << "\", " << juce::String (trim, 2) << "f },\n";
            continue;
        }
        if (res.failures.empty())
            ++passed;
        for (const auto& f : res.failures)
            CHECK (false, res.name + ": " + f);
    }
    if (calibrate.isNotEmpty())
        juce::File (calibrate).replaceWithText (trims);
    else
        std::cout << "info: " << passed << " of " << checked << " presets are Potato Ready\n";
}

// PRESSURE (its OTT, glue and clipper thresholds follow the playing's level, and the loudness trim compensates
// them) does the same to a chord struck after 3 s of silence as to the first one: no squash, no swell. (Both used
// to drift in the silence: the second chord came out 2.5 LU off and the first one swelled in from -9 dB.) Each strike
// is compared with PRESSURE off, since the stack itself plays the second strike a little differently (phase
// alignment fixes a note from its second play).
void testPressureAfterPause()
{
    const auto& bank = ps::presets::factory();
    std::array<std::array<double, 2>, 2> loudness {};   // [pressure off / on][first / second strike]
    std::array<std::array<double, 2>, 2> onsetDip {};   // the first 50 ms against the rest, same indices
    for (int on1 = 0; on1 < 2; ++on1)
    {
        potatoReady::PresetRig rig (bank[0]);
        rig.set (ps::params::chainKnob (2), (float) on1);
        rig.render (48000);
        std::vector<std::pair<int, juce::MidiMessage>> strike, release;
        for (int n : { 48, 55, 60, 64 })
        {
            strike.push_back ({ 0, on (n, 1.0f) });
            release.push_back ({ 0, off (n) });
        }
        for (int k = 0; k < 2; ++k)
        {
            const auto l = rig.render (24000, strike);
            loudness[(size_t) on1][(size_t) k] = potatoReady::lufs (l, rig.right, 0, l.size());
            double head = 0.0, rest = 0.0;
            for (size_t i = 0; i < l.size(); ++i)
                (i < 2400 ? head : rest) += (double) l[i] * l[i];
            onsetDip[(size_t) on1][(size_t) k] = 10.0 * std::log10 ((head / 2400.0) / (rest / (double) (l.size() - 2400)));
            rig.render (144000, release);
        }
    }
    const double first = loudness[1][0] - loudness[0][0], second = loudness[1][1] - loudness[0][1];
    // At full PRESSURE the trim makes up about 11 dB, so these allow what the knob sweeps allow
    CHECK (std::abs (second - first) < 1.0, "PRESSURE does the same to a chord after a 3 s pause as to the first ("
                                                + std::to_string (first) + " vs " + std::to_string (second) + " LU)");
    for (size_t k = 0; k < 2; ++k)
    {
        const double dip = onsetDip[1][k] - onsetDip[0][k];
        CHECK (dip > -3.0, "PRESSURE doesn't swell a chord in (strike " + std::to_string (k + 1) + ": first 50 ms "
                               + std::to_string (dip) + " dB against PRESSURE off)");
    }
}

// Loading a preset puts back every parameter it doesn't set, and Init sounds like a fresh instance
void testPresetLoading()
{
    const auto& bank = ps::presets::factory();
    Rig fresh (false), loaded (false);
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        const char* engines[] = { "sub", "dirt", "jp", "spectral", "", "", "", "" };
        fresh.proc->setLayerEngine (i, engines[i]);
    }
    loaded.proc->loadPreset (bank[5]);
    loaded.render (blockSize);
    loaded.proc->loadPreset (bank[0]);
    for (auto* rig : { &fresh, &loaded })
    {
        rig->render (blockSize);
        rig->collectRetired();
        rig->render (48000);   // outgoing engines fade out
    }
    bool same = true;
    for (auto* param : fresh.proc->getParameters())
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (param);
        auto* q = loaded.proc->apvts.getParameter (p->getParameterID());
        same = same && std::abs (p->getValue() - q->getValue()) < 1.0e-6f;
    }
    CHECK (same, "loading Init after another preset puts every parameter back to a fresh instance's");
    CHECK (loaded.proc->getPresetName() == "Init", "the loaded preset's name is kept");
    const auto a = fresh.render (24000, { { 0, on (60) } }), b = loaded.render (24000, { { 0, on (60) } });
    float diff = 0.0f, peak = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
    {
        diff = std::max (diff, std::abs (a[i] - b[i]));
        peak = std::max (peak, std::abs (a[i]));
    }
    std::cout << "info: Init vs fresh instance differ by " << juce::Decibels::gainToDecibels (diff / peak, -200.0f) << " dB\n";
    CHECK (diff <= peak * 1.0e-6f, "Init sounds like a fresh instance (within -120 dB)");
}

// BLOOM's pitch motion is a fixed wobble, never random: the same playing renders the same samples
void testBloomIsRepeatable()
{
    auto play = []
    {
        Rig rig;
        rig.set (ps::params::chainKnob (ps::chain::bloom), 1.0f);
        rig.render (48000);   // the knob glides in
        return rig.render (96000, { { 0, on (57) }, { 48000, on (64) } });
    };
    CHECK (play() == play(), "BLOOM: the stack's pitch motion repeats exactly (no random drift)");

    // ...and it restarts with every phrase: the same moment of a phrase after the stack falls silent has the
    // same pitch offset
    Rig rig;
    rig.set (ps::params::chainKnob (ps::chain::bloom), 1.0f);
    rig.render (48000);
    rig.render (36000, { { 0, on (57) } });
    const float first = rig.proc->getStack().layer (2).chainDrift();
    rig.render (48000 * 15, { { 0, off (57) } });   // the space's tail dies away
    rig.render (36000, { { 0, on (57) } });
    const float second = rig.proc->getStack().layer (2).chainDrift();
    CHECK (std::abs (first - second) < 1.0e-4f && std::abs (first) > 0.1f,
           "BLOOM: the wobble restarts with every phrase (" + std::to_string (first) + " ct, then " + std::to_string (second) + " ct)");
}

// Invariant 5: SCALE's added detune stops at the beat ceiling (CLARITY lowers it); the patch's own detune is
// never changed
void testBeatCeiling()
{
    ps::JpEngine reference;
    auto sentDetune = [] (float patchDetune, float scale, float clarity)
    {
        JpRig rig ({ { Jp::detune, patchDetune } });
        rig.set (ps::params::layerRole (2), (float) ps::params::Role::mid);
        rig.set (ps::params::chainKnob (ps::chain::scale), scale);
        rig.set (ps::params::chainKnob (ps::chain::clarity), clarity);
        rig.render (48000, { { 0, on (60) } });   // the knobs glide
        return rig.proc->getStack().layer (2).sentSlot (Jp::detune);
    };
    const float free = sentDetune (0.5f, 1.0f, 0.0f);
    CHECK (std::abs (free - 0.7f) < 1.0e-3f, "beat ceiling: SCALE adds its full detune below the ceiling (" + std::to_string (free) + ")");
    const float capped = sentDetune (0.9f, 1.0f, 0.0f);
    const float cents = reference.detuneSpacingCents (capped);
    CHECK (capped > 0.9f && cents <= 48.4f && cents > 47.0f,
           "beat ceiling: SCALE stops at 48 cents between neighbouring voices (" + std::to_string (cents) + " ct)");
    const float clear = sentDetune (0.85f, 1.0f, 1.0f);
    const float clearCents = reference.detuneSpacingCents (clear);
    CHECK (clearCents <= std::max (29.2f, reference.detuneSpacingCents (0.85f)) + 0.1f,
           "beat ceiling: CLARITY lowers the ceiling to 29 cents (" + std::to_string (clearCents) + " ct)");
    CHECK (std::abs (sentDetune (1.0f, 0.0f, 0.0f) - 1.0f) < 1.0e-6f && std::abs (sentDetune (1.0f, 1.0f, 0.0f) - 1.0f) < 1.0e-6f,
           "beat ceiling: the patch's own detune is never reduced");
}

// ---- Genome engine ---------------------------------------------------------------------------------------

using Genome = ps::GenomeEngine;

struct GenomeRig : Rig
{
    explicit GenomeRig (std::initializer_list<std::pair<int, float>> slots = {})
    {
        proc->setLayerEngine (2, "genome");
        render (blockSize);
        collectRetired();
        for (auto& [slot, value] : slots)
            setSlot (slot, value);
    }

    void setSlot (int slot, float value) { set (ps::params::layerSlot (2, slot), value); }
};

// A plain, harmonic Genome at one architecture: no detune, strike or index envelope
GenomeRig genomeAt (float architecture, std::initializer_list<std::pair<int, float>> more = {})
{
    GenomeRig rig ({ { Genome::architecture, architecture }, { Genome::detune, 0.0f }, { Genome::strike, 0.0f },
                     { Genome::fmEnv, 0.0f } });
    for (auto& [slot, value] : more)
        rig.setSlot (slot, value);
    return rig;
}

// Period by normalised autocorrelation (robust to several zero crossings per period), parabolic peak
double estimatePitchHz (const std::vector<float>& x, size_t from, size_t to, double fMin, double fMax)
{
    const int lo = (int) (sampleRate / fMax), hi = (int) (sampleRate / fMin) + 1;
    std::vector<double> c ((size_t) hi + 2, 0.0);
    for (int lag = lo - 1; lag <= hi + 1; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (size_t i = from; i + (size_t) lag < to; ++i)
        {
            xy += (double) x[i] * x[i + (size_t) lag];
            xx += (double) x[i] * x[i];
            yy += (double) x[i + (size_t) lag] * x[i + (size_t) lag];
        }
        c[(size_t) lag] = xy / std::sqrt (xx * yy + 1.0e-30);
    }
    int best = lo;
    for (int lag = lo; lag <= hi; ++lag)
        if (c[(size_t) lag] > c[(size_t) best] + 1.0e-9)
            best = lag;
    const double a = c[(size_t) best - 1], b = c[(size_t) best], d = c[(size_t) best + 1];
    const double offset = 0.5 * (a - d) / (a - 2.0 * b + d);
    return sampleRate / ((double) best + offset);
}

const float genomeStops[] = { 0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f };

std::string genomeName (float a)
{
    return Genome().info().slots[(size_t) Genome::architecture].toText (a).toStdString();
}

void testGenomeEngine()
{
    // Exact pitch at every design and every halfway blend, and a held note never moves
    for (float a : genomeStops)
    {
        auto rig = genomeAt (a);
        auto x = rig.render (72000, { { 0, on (57) } });
        double lo = 1.0e9, hi = 0.0;
        for (size_t start = 8000; start + 16000 <= x.size(); start += 16000)
        {
            const double f = estimatePitchHz (x, start, start + 16000, 150.0, 300.0);
            lo = std::min (lo, f);
            hi = std::max (hi, f);
        }
        CHECK (std::abs (lo - 220.0) < 0.1 && std::abs (hi - 220.0) < 0.1,
               "Genome " + genomeName (a) + ": A3 holds 220 Hz (" + std::to_string (lo) + " .. " + std::to_string (hi) + " Hz)");
    }

    // Deterministic: the same note renders the same samples every time
    {
        auto first = GenomeRig ({ { Genome::architecture, 0.3f } }).render (12000, { { 0, on (52) } });
        auto second = GenomeRig ({ { Genome::architecture, 0.3f } }).render (12000, { { 0, on (52) } });
        CHECK (first == second, "Genome: a note renders identically every time (no random drift or phase)");
    }

    // Shared phase: the 2x designs and the 1x harmonics line up to a fraction of a sample
    {
        auto va = genomeAt (0.0f).render (24000, { { 0, on (45) } });
        auto adds = genomeAt (0.75f).render (24000, { { 0, on (45) } });
        auto corrAt = [&] (int lag)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (size_t i = 8000; i < 20000; ++i)
            {
                xy += (double) va[i] * adds[(size_t) ((int) i + lag)];
                xx += (double) va[i] * va[i];
                yy += (double) adds[(size_t) ((int) i + lag)] * adds[(size_t) ((int) i + lag)];
            }
            return xy / std::sqrt (xx * yy);
        };
        int best = 0;
        for (int lag = -20; lag <= 20; ++lag)
            if (corrAt (lag) > corrAt (best))
                best = lag;
        const double a = corrAt (best - 1), b = corrAt (best), d = corrAt (best + 1);
        const double lag = best + 0.5 * (a - d) / (a - 2.0 * b + d);
        CHECK (std::abs (lag) < 0.25 && b > 0.95, "Genome: Jupiter and Additive share one phase (offset " + std::to_string (lag)
                                                  + " samples, correlation " + std::to_string (b) + ")");
    }

    // Band-limited at every design and blend, at 2.1 kHz with the FM index and resonance up
    for (float a : genomeStops)
    {
        auto rig = genomeAt (a, { { Genome::fmIndex, 0.7f }, { Genome::fmFeedback, 0.5f }, { Genome::pdRes, 0.4f } });
        auto x = rig.render (48000, { { 0, on (96, 1.0f) } });
        const float db = worstAliasDb (x, 24000, 440.0 * std::pow (2.0, (96 - 69) / 12.0), true);
        std::cout << "info: Genome " << genomeName (a) << " at 2.1 kHz, worst alias " << db << " dB\n";
        CHECK (db < -60.0f, "Genome " + genomeName (a) + " is band-limited (aliases below -60 dB of the strongest harmonic at 2.1 kHz)");
    }

    // The FM index cap: full index and feedback on a high note stay clean
    {
        auto rig = genomeAt (0.25f, { { Genome::fmIndex, 1.0f }, { Genome::fmFeedback, 1.0f }, { Genome::fmRatio, 1.0f } });
        auto x = rig.render (48000, { { 0, on (100, 1.0f) } });
        const double f0 = 440.0 * std::pow (2.0, (100 - 69) / 12.0);
        const float db = worstAliasDb (x, 24000, f0, true);
        std::cout << "info: Genome FM at full index, feedback and ratio 8 at 2.6 kHz, worst alias " << db << " dB\n";
        CHECK (db < -50.0f, "Genome: FM at full index stays under Carson's limit (aliases below -50 dB)");
    }

    // Sweeping the architecture end to end: no clicks, and the level stays within 1 dB
    for (int note : { 40, 57, 76 })
    {
        auto rig = genomeAt (0.0f);
        auto x = rig.render (24000, { { 0, on (note) } });
        const float steady = maxStep (std::vector<float> (x.begin() + 12000, x.end()));
        std::vector<float> sweep;
        constexpr int steps = 400;
        for (int i = 1; i <= steps; ++i)
        {
            rig.setSlot (Genome::architecture, (float) i / steps);
            auto y = rig.render (blockSize);
            sweep.insert (sweep.end(), y.begin(), y.end());
        }
        float worstStep = maxStep (sweep), lo = 1.0e9f, hi = -1.0e9f;
        for (size_t start = 0; start + 4800 <= sweep.size(); start += 2400)
        {
            const float db = juce::Decibels::gainToDecibels (rms (sweep, start, start + 4800));
            lo = std::min (lo, db);
            hi = std::max (hi, db);
        }
        const float ref = juce::Decibels::gainToDecibels (rms (x, 12000, 24000));
        const float spread = std::max (hi - ref, ref - lo);
        std::cout << "info: Genome sweep at note " << note << ": " << lo - ref << " .. " << hi - ref << " dB, max step "
                  << worstStep / steady << "x steady\n";
        CHECK (worstStep < steady * 2.0f, "Genome: sweeping the architecture never clicks (note " + std::to_string (note) + ")");
        CHECK (spread < 1.0f, "Genome: the architecture sweep keeps the level within 1 dB (note " + std::to_string (note) + ", "
                              + std::to_string (spread) + " dB)");
    }

    // Every slot to its extreme on low and high notes: finite and bounded (FM feedback, modes, resonance)
    {
        for (float a : genomeStops)
        {
            GenomeRig rig ({ { Genome::architecture, a } });
            for (int s = 1; s < Genome::numUsedSlots; ++s)
                rig.setSlot (s, 1.0f);
            auto x = rig.render (48000, { { 0, on (24, 1.0f) }, { 0, on (60, 1.0f) }, { 0, on (108, 1.0f) } });
            CHECK (allFinite (x) && peak (x) < 2.0f, "Genome " + genomeName (a) + ": every slot at its maximum stays finite and bounded ("
                                                     + std::to_string (peak (x)) + ")");
        }
    }

    // Body: the strike rings and decays at Body Decay; retriggering a ringing voice doesn't click
    {
        auto rig = genomeAt (1.0f, { { Genome::strike, 1.0f }, { Genome::bodyDecay, 0.5f } });
        auto x = rig.render (96000, { { 0, on (57) } });
        const float early = rms (x, 0, 2400), late = rms (x, 48000, 52800);
        CHECK (early > late * 1.3f, "Genome Body: the strike rings above the sustain (" + std::to_string (early / late) + "x)");
        auto y = rig.render (24000, { { 0, on (57) } });
        CHECK (maxStep (y) < 2.0f * maxStep (std::vector<float> (x.begin(), x.begin() + 24000)),
               "Genome Body: a new strike on a ringing voice doesn't click");
    }
}

void testGenomeSwitchesDoNotClick()
{
    const std::pair<int, float> moves[] = {
        { Genome::shape, 1.0f }, { Genome::detune, 1.0f }, { Genome::fmIndex, 1.0f }, { Genome::fmRatio, 0.6f },
        { Genome::fmFeedback, 1.0f }, { Genome::pdAmount, 0.0f }, { Genome::pdRes, 0.8f }, { Genome::tilt, 0.0f },
        { Genome::oddEven, 0.0f }, { Genome::bodyTone, 1.0f }, { Genome::tone, 0.3f } };
    // Body Tone retunes ringing modes, which legitimately raises their sample steps, so the click test for it
    // looks for broadband energy instead: an A2's modes stay under 3.6 kHz, so anything above 8 kHz in the move
    // (8th-order high-pass) would be a click
    {
        auto rig = genomeAt (1.0f, { { Genome::bodyDecay, 0.8f } });
        auto x = rig.render (12000, { { 0, on (45) } });
        rig.setSlot (Genome::bodyTone, 1.0f);
        auto y = rig.render (24000);
        x.insert (x.end(), y.begin(), y.end());
        std::vector<float> high (x);
        for (float q : { 0.5098f, 0.6013f, 0.9000f, 2.5629f })
        {
            juce::dsp::IIR::Filter<float> hp (juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 8000.0, q));
            for (auto& v : high)
                v = hp.processSample (v);
        }
        const float before = rms (high, 6000, 12000), during = rms (high, 12000, 14400), level = rms (x, 6000, 12000);
        const float floor = std::max (2.0f * before, 1.0e-4f * level);
        CHECK (during < floor, "Genome Body: changing Body Tone mid-note does not click (energy above 8 kHz "
                               + std::to_string (juce::Decibels::gainToDecibels (during / level)) + " dB)");
    }
    for (float a : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        for (auto [slot, value] : moves)
        {
            if (a == 1.0f && slot == Genome::bodyTone)
                continue;
            auto rig = genomeAt (a, { { Genome::bodyDecay, 0.8f } });
            auto x = rig.render (12000, { { 0, on (45) } });
            const float baseline = maxStep (std::vector<float> (x.begin() + 6000, x.end()));
            rig.setSlot (slot, value);
            auto y = rig.render (9600);
            const float steady = std::max (baseline, maxStep (std::vector<float> (y.begin() + 4800, y.end())));
            const float worst = maxStep (std::vector<float> (y.begin(), y.begin() + 2400));
            const auto name = Genome().info().slots[(size_t) slot].name.toStdString();
            CHECK (worst < steady * 1.5f, "Genome " + genomeName (a) + ": changing " + name + " mid-note does not click ("
                                          + std::to_string (worst / steady) + "x the steady step)");
        }
}

void benchmarkGenome()
{
    for (float a : { 0.125f, 0.625f, 0.875f })
    {
        Rig rig (false);
        for (int i = 0; i < 4; ++i)
            rig.proc->setLayerEngine (i, "genome");
        rig.render (blockSize);
        rig.collectRetired();
        for (int i = 0; i < 4; ++i)
            rig.set (ps::params::layerSlot (i, Genome::architecture), a);
        std::vector<std::pair<int, juce::MidiMessage>> chord;
        for (int n : { 36, 43, 48, 52, 55, 60, 64, 67 })
            chord.push_back ({ 0, on (n) });
        rig.render (blockSize, chord);
        double best = 1.0e9;
        for (int run = 0; run < 3; ++run)
        {
            const auto start = juce::Time::getHighResolutionTicks();
            rig.render ((int) sampleRate * 2);
            best = std::min (best, 50.0 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start));
        }
        std::cout << "info: 8-note chord on 4 Genome layers at " << genomeName (a) << " uses " << best << "% of one core\n";
        CHECK (best < 20.0, "Genome stack CPU is within budget (" + genomeName (a) + ")");
    }
}

// What the insert slots cost: the init stack's engines with both slots on all 4 layers on the heaviest types
void benchmarkInserts()
{
    auto measure = [] (bool withInserts)
    {
        Rig rig (false);
        const char* engines[] = { "sub", "dirt", "jp", "spectral" };
        for (int i = 0; i < 4; ++i)
            rig.proc->setLayerEngine (i, engines[i]);
        rig.render (blockSize);
        rig.collectRetired();
        if (withInserts)
            for (int i = 0; i < 4; ++i)
            {
                rig.set (ps::params::layerInsertType (i, 0), (float) ps::inserts::Type::freqShift);
                rig.set (ps::params::layerInsertType (i, 1), (float) ps::inserts::Type::formant);
                rig.set (ps::params::layerInsertAmount (i, 0), 100.0f);
                rig.set (ps::params::layerInsertAmount (i, 1), 100.0f);
            }
        std::vector<std::pair<int, juce::MidiMessage>> chord;
        for (int n : { 36, 43, 48, 52, 55, 60, 64, 67 })
            chord.push_back ({ 0, on (n) });
        rig.render (blockSize, chord);
        double best = 1.0e9;
        for (int run = 0; run < 3; ++run)
        {
            const auto start = juce::Time::getHighResolutionTicks();
            rig.render ((int) sampleRate * 2);
            best = std::min (best, 50.0 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start));
        }
        return best;
    };
    const double without = measure (false), with = measure (true);
    std::cout << "info: 8-note chord on the init stack: " << without << "% of one core, " << with << "% with 8 insert slots (Freq Shift + Formant)\n";
    CHECK (with < 20.0, "the init stack with 8 heavy insert slots stays within the 20 % CPU budget");
}

// ---- Arpeggiator and step sequencer -------------------------------------------------------------------

using Gen = ps::NoteGenerator;

struct GenEvent { int64_t at; bool on; int note; float velocity; };

// Runs the note generator in 256-sample blocks. Input events are at absolute samples; with ppqStart >= 0 the
// host transport plays from that beat at 120 BPM (and jumps where `jumps` says: sample -> new beat).
struct GenRun
{
    Gen gen;
    Gen::Settings s;
    std::vector<GenEvent> out;
    int64_t now = 0;
    double ppq = -1.0;

    GenRun() { gen.prepare (sampleRate); }

    void run (int64_t samples, std::vector<std::pair<int64_t, juce::MidiMessage>> in = {}, std::vector<std::pair<int64_t, double>> jumps = {})
    {
        juce::MidiBuffer inBuf, outBuf;
        const int64_t end = now + samples;
        while (now < end)
        {
            const int n = (int) std::min<int64_t> (blockSize, end - now);
            for (auto& [at, beat] : jumps)
                if (at >= now && at < now + n)
                    ppq = beat;
            inBuf.clear();
            for (auto& [at, m] : in)
                if (at >= now && at < now + n)
                    inBuf.addEvent (m, (int) (at - now));
            gen.process (inBuf, outBuf, n, 120.0, ppq, s);
            for (const auto meta : outBuf)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOnOrOff())
                    out.push_back ({ now + meta.samplePosition, m.isNoteOn(), m.getNoteNumber(), m.getFloatVelocity() });
            }
            now += n;
            if (ppq >= 0.0)
                ppq += n * 2.0 / sampleRate;
        }
    }

    std::vector<GenEvent> ons() const
    {
        std::vector<GenEvent> r;
        for (auto& e : out) if (e.on) r.push_back (e);
        return r;
    }

    std::vector<int> onNotes() const
    {
        std::vector<int> r;
        for (auto& e : out) if (e.on) r.push_back (e.note);
        return r;
    }

    // Every note on has its note off (none left sounding)
    bool balanced() const
    {
        std::array<int, 128> count {};
        for (auto& e : out)
            count[(size_t) e.note] += e.on ? 1 : -1;
        return std::all_of (count.begin(), count.end(), [] (int c) { return c == 0; });
    }
};

std::vector<std::pair<int64_t, juce::MidiMessage>> keys (int64_t at, std::initializer_list<int> notes, int64_t releaseAt = -1)
{
    std::vector<std::pair<int64_t, juce::MidiMessage>> v;
    for (int n : notes)
    {
        v.push_back ({ at, on (n) });
        if (releaseAt >= 0)
            v.push_back ({ releaseAt, off (n) });
    }
    return v;
}

std::string join (const std::vector<int>& v, size_t count)
{
    std::string r;
    for (size_t i = 0; i < std::min (count, v.size()); ++i)
        r += (i ? " " : "") + std::to_string (v[i]);
    return r;
}

void testArpeggiator()
{
    const int step = 6000;   // 1/16 at 120 BPM
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.run (48000, keys (1000, { 60, 64, 67 }, 30000));
        const auto ons = r.ons();
        bool timed = ons.size() >= 5;
        for (size_t i = 0; i < std::min<size_t> (5, ons.size()); ++i)
            timed = timed && ons[i].at == 1000 + (int64_t) i * step;
        CHECK (timed, "arp: Up plays from the key press, one note every 1/16 at 120 BPM, sample-exact");
        CHECK (join (r.onNotes(), 5) == "60 64 67 60 64", "arp: Up plays the keys low to high (" + join (r.onNotes(), 5) + ")");
        bool gated = true;
        for (auto& e : r.out)
            if (! e.on && e.at < 30000)
                gated = gated && (e.at - 1000) % step == 3600;
        CHECK (gated, "arp: notes last the 60 % gate");
        CHECK (ons.back().at < 30000 && r.balanced(), "arp: releasing the keys stops it, with no stuck notes");
    }

    auto order = [] (Gen::Direction d, int octaves, std::initializer_list<int> notes)
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.s.direction = d;
        r.s.octaves = octaves;
        r.run (6000 * 12 + 100, keys (0, notes));
        return join (r.onNotes(), 12);
    };
    CHECK (order (Gen::Direction::down, 1, { 60, 64, 67 }) == "67 64 60 67 64 60 67 64 60 67 64 60", "arp: Down");
    CHECK (order (Gen::Direction::upDown, 2, { 60, 64, 67 }) == "60 64 67 72 76 79 76 72 67 64 60 64",
           "arp: Up-Down over two octaves (" + order (Gen::Direction::upDown, 2, { 60, 64, 67 }) + ")");
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.s.direction = Gen::Direction::asPlayed;
        r.run (6000 * 6 + 100, { { 0, on (67) }, { 0, on (60) }, { 0, on (64) } });
        CHECK (join (r.onNotes(), 6) == "67 60 64 67 60 64", "arp: As Played keeps the playing order");
    }
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.s.direction = Gen::Direction::random;
        r.s.octaves = 2;
        r.run (6000 * 32, keys (0, { 60, 64, 67 }));
        const auto notes = r.onNotes();
        const bool inSet = std::all_of (notes.begin(), notes.end(), [] (int n) { return n == 60 || n == 64 || n == 67 || n == 72 || n == 76 || n == 79; });
        std::set<int> distinct (notes.begin(), notes.end());
        CHECK (inSet && distinct.size() >= 5, "arp: Random picks from the held keys over the octaves");
    }
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.s.direction = Gen::Direction::chord;
        r.s.octaves = 2;
        r.run (6000 * 2 + 100, keys (0, { 60, 64, 67 }));
        CHECK (join (r.onNotes(), 6) == "60 64 67 72 76 79", "arp: Chord plays every key on each step, octave by octave");
    }

    // Gate over 100 %: legato, each note ends after the next begins
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.s.gate = 1.5f;
        r.run (30000, keys (0, { 60, 64 }));
        int64_t firstOff = -1;
        for (auto& e : r.out)
            if (! e.on && e.note == 60 && firstOff < 0)
                firstOff = e.at;
        CHECK (firstOff == 9000, "arp: a 150 % gate overlaps the next note (" + std::to_string (firstOff) + ")");
    }

    // Swing: every second step half a step late at 100 %
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.s.swing = 1.0f;
        r.run (30000, keys (0, { 60 }));
        const auto ons = r.ons();
        CHECK (ons.size() >= 4 && ons[1].at == 9000 && ons[2].at == 12000 && ons[3].at == 21000, "arp: swing delays the off-beat steps");
    }

    // Latch: the pattern keeps playing after release, and a new key replaces it
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.s.latch = true;
        auto in = keys (0, { 60, 64 }, 10000);
        in.push_back ({ 60000, on (70) });
        in.push_back ({ 70000, off (70) });
        r.run (96000, in);
        bool latched = false, replaced = true;
        for (auto& e : r.ons())
        {
            latched = latched || (e.at > 20000 && e.at < 60000);
            if (e.at >= 60000)
                replaced = replaced && e.note == 70;
        }
        CHECK (latched && replaced, "arp: Latch holds the pattern until a new key replaces it");
    }

    // Host transport: steps land on the grid; a late press plays its step, a later one waits; a jump stops notes
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.ppq = 0.03;   // 720 samples after the beat: under a quarter step late
        r.run (20000, keys (0, { 60 }));
        const auto ons = r.ons();
        CHECK (! ons.empty() && ons[0].at == 0 && ons.size() >= 2 && ons[1].at == 6000 - 720, "arp: synced to the host grid; a slightly late key still plays its step");
    }
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.ppq = 0.1;   // 2400 samples late: waits for the next 1/16
        r.run (20000, keys (0, { 60 }));
        const auto ons = r.ons();
        CHECK (! ons.empty() && ons[0].at == 6000 - 2400, "arp: a key well after the step waits for the next one");
    }
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.s.gate = 1.0f;
        r.ppq = 0.0;
        r.run (60000, keys (0, { 60, 64 }, 50000), { { 20000, 8.0 } });
        bool cut = false;
        for (auto& e : r.out)
            cut = cut || (! e.on && e.at >= 19700 && e.at < 20300);
        CHECK (cut && r.balanced(), "arp: a transport jump stops the sounding notes and picks the grid up again");
    }

    // Switching off while keys are down: the generator's notes stop and the keys sound directly
    {
        GenRun r;
        r.s.mode = Gen::Mode::arp;
        r.run (10000, keys (0, { 60, 64 }));
        r.s.mode = Gen::Mode::off;
        r.run (10000);
        int direct = 0;
        for (auto& e : r.out)
            direct += (e.on && e.at >= 10000) ? 1 : 0;
        r.run (1000, { { r.now, off (60) }, { r.now, off (64) } });
        CHECK (direct == 2 && r.balanced(), "arp: switching off hands the held keys back, with no stuck notes");
    }
}

void testStepSequencer()
{
    const int step = 6000;
    auto pattern = [] (GenRun& r, std::initializer_list<Gen::Step> steps)
    {
        int i = 0;
        for (auto& st : steps)
            r.gen.setStep (i++, st);
        r.s.length = (int) steps.size();
    };
    {
        GenRun r;
        r.s.mode = Gen::Mode::seq;
        pattern (r, { { true, false, 0 }, { true, false, 12 }, { false, false, 0 }, { true, false, 7 } });
        r.run (step * 8 + 100, keys (0, { 48 }));
        const auto ons = r.ons();
        CHECK (join (r.onNotes(), 6) == "48 60 55 48 60 55" && ons.size() >= 3 && ons[2].at == 3 * step,
               "seq: the pattern plays from the held key, with a rest (" + join (r.onNotes(), 6) + ")");
    }
    {
        GenRun r;
        r.s.mode = Gen::Mode::seq;
        pattern (r, { { true, false, 0 }, { true, false, 12 } });
        r.run (step * 4 + 100, { { 0, on (48) }, { 2 * step - 100, on (53) } });
        CHECK (join (r.onNotes(), 4) == "48 60 53 65", "seq: a new key transposes the pattern (" + join (r.onNotes(), 4) + ")");
    }
    {
        GenRun r;
        r.s.mode = Gen::Mode::seq;
        pattern (r, { { true, false, 0 }, { true, true, 0 }, { true, false, 5 }, { true, false, 7 } });
        r.run (step * 4, keys (0, { 48 }));
        int64_t firstOff = -1;
        for (auto& e : r.out)
            if (! e.on && e.note == 48 && firstOff < 0)
                firstOff = e.at;
        CHECK (r.onNotes().size() == 3 && firstOff == step + 3600, "seq: a tie holds the note through the next step (" + join (r.onNotes(), 4) + ", off at " + std::to_string (firstOff) + ")");
    }
    {
        GenRun r;
        r.s.mode = Gen::Mode::seq;
        Gen::Step never { true, false, 0, 1, 1.0f, 0.0f }, half { true, false, 0, 1, 1.0f, 0.5f };
        pattern (r, { never, half });
        r.run (step * 64, keys (0, { 48 }));
        const auto n = r.ons().size();
        CHECK (n >= 8 && n <= 24, "seq: probability 0 never plays, 50 % plays about half the time (" + std::to_string (n) + " of 32)");
    }
    {
        GenRun r;
        r.s.mode = Gen::Mode::seq;
        pattern (r, { { true, false, 0, 3, 0.5f, 1.0f } });
        r.run (step, keys (0, { 48 }));
        const auto ons = r.ons();
        CHECK (ons.size() == 3 && ons[1].at == 2000 && ons[2].at == 4000 && std::abs (ons[0].velocity - 0.8f * 0.5f) < 0.01f && r.balanced(),
               "seq: a ratchet plays even hits at the step's velocity");
    }
    {
        GenRun r;
        r.s.mode = Gen::Mode::seq;
        r.s.hostTrigger = true;
        pattern (r, { { true, false, 0 }, { true, false, 3 } });
        r.ppq = 0.0;
        r.run (step * 2 + 100);
        CHECK (join (r.onNotes(), 2) == "60 63", "seq: Host trigger plays from C3 with the transport, no key needed");
    }
}

void testArpThroughTheSynth()
{
    namespace p = ps::params;
    Rig rig;
    rig.set (p::arpMode, 1.0f);
    rig.proc->setPatternStep (3, { false, false, 0 });
    auto x = rig.render (48000, { { 0, on (57) }, { 0, on (60) }, { 24000, off (57) }, { 24000, off (60) } });
    CHECK (peak (x, 2000, 24000) > 0.05f && peak (x, 24000 + 24000, 48000) < 1.0e-4f, "arp: plays the synth and stops after release");

    juce::MemoryBlock state;
    rig.proc->getStateInformation (state);
    Rig other;
    other.proc->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (! other.proc->getPatternStep (3).on && other.proc->getPatternStep (2).on && other.get (p::arpMode) > 0.5f,
           "the step pattern and arp settings come back with the state");
}

// ---- Mod step lanes -------------------------------------------------------------------------------------

void testModLanes()
{
    namespace p = ps::params;
    // Lane 1 on MASS at full depth, alternating 1/0 every 1/16 (6000 samples at 120 BPM) while a key is held
    {
        Rig rig;
        rig.set (p::laneTarget (0), 1.0f);
        rig.set (p::laneDepth (0), 100.0f);
        rig.set (p::laneLength (0), 2.0f);
        rig.proc->setLaneValue (0, 0, 1.0f);
        rig.proc->setLaneValue (0, 1, 0.0f);
        std::vector<float> at;
        rig.render (blockSize, { { 0, on (48) } });
        for (int b = 1; b < 4 * 6000 / blockSize; ++b)
        {
            rig.render (blockSize);
            at.push_back (rig.proc->getEffectiveKnob (0));
        }
        auto knobAt = [&] (int sample) { return at[(size_t) (sample / blockSize - 1)]; };
        CHECK (knobAt (5000) > 0.95f && knobAt (11000) < 0.05f && knobAt (17000) > 0.95f,
               "lane: MASS follows the steps in time (" + std::to_string (knobAt (5000)) + ", " + std::to_string (knobAt (11000)) + ")");
        float worst = 0.0f;
        for (size_t i = 1; i < at.size(); ++i)
            worst = std::max (worst, std::abs (at[i] - at[i - 1]));
        CHECK (worst < 0.5f, "lane: steps glide through the knob smoothing (largest block step " + std::to_string (worst) + ")");
        rig.render (blockSize, { { 0, off (48) } });
        rig.render (48 * blockSize);
        CHECK (rig.proc->getEffectiveKnob (0) < 0.01f, "lane: with no key and no transport the lane rests and the knob returns");
    }
    // Negative depth from a raised knob, and the XY pad
    {
        Rig rig;
        rig.set (p::chainKnob (6), 1.0f);
        rig.set (p::laneTarget (1), 7.0f);   // MORPH
        rig.set (p::laneDepth (1), -60.0f);
        rig.set (p::laneLength (1), 1.0f);
        rig.proc->setLaneValue (1, 0, 1.0f);
        rig.set (p::laneTarget (2), 10.0f);  // XY X
        rig.set (p::laneDepth (2), 40.0f);
        rig.set (p::laneLength (2), 1.0f);
        rig.proc->setLaneValue (2, 0, 1.0f);
        rig.render (blockSize * 40, { { 0, on (48) } });
        CHECK (std::abs (rig.proc->getEffectiveKnob (6) - 0.4f) < 0.02f && std::abs (rig.proc->getEffectivePadX() - 0.9f) < 0.02f,
               "lane: a negative depth pulls a knob down, and a lane moves the XY pad");
    }
    // Host transport: the step follows the beat, keys or not
    {
        Rig rig;
        FakeTransport transport;
        rig.proc->setPlayHead (&transport);
        rig.set (p::laneTarget (0), 2.0f);   // SCALE
        rig.set (p::laneDepth (0), 100.0f);
        rig.set (p::laneLength (0), 4.0f);
        rig.set (p::laneRate (0), 8.0f);     // 1/4 = 24000 samples
        for (int s = 0; s < 4; ++s)
            rig.proc->setLaneValue (0, s, s == 2 ? 1.0f : 0.0f);
        rig.render (24000 * 2 + 12000);
        const float third = rig.proc->getEffectiveKnob (1);
        rig.render (24000);
        const float fourth = rig.proc->getEffectiveKnob (1);
        rig.proc->setPlayHead (nullptr);
        CHECK (third > 0.95f && fourth < 0.05f, "lane: synced to the host's beats with no key held");
    }
    // State
    {
        Rig rig;
        rig.proc->setLaneValue (3, 5, 0.25f);
        juce::MemoryBlock state;
        rig.proc->getStateInformation (state);
        Rig other;
        other.proc->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (std::abs (other.proc->getLaneValue (3, 5) - 0.25f) < 1.0e-3f && std::abs (other.proc->getLaneValue (0, 15) - 1.0f) < 1.0e-3f,
               "the lane steps come back with the state");
    }
}

// ---- Layer filter and drive ---------------------------------------------------------------------------

float filterTypeValue (ps::params::FilterType t) { return (float) (int) t; }

void testLayerFilter()
{
    namespace p = ps::params;
    // Share of the energy above 2 kHz for a JP saw (A2) through layer 3's filter
    auto bright = [] (std::initializer_list<std::pair<juce::ParameterID, float>> settings, int note = 45)
    {
        JpRig rig ({ { Jp::unison, Jp::choiceValue (0, Jp::numUnison) } });
        for (auto& [id, v] : settings)
            rig.set (id, v);
        auto x = rig.render (24000, { { 0, on (note) } });
        return bandEnergy (x, 12000, 2000.0, 20000.0) / bandEnergy (x, 12000, 50.0, 20000.0);
    };
    const float open = bright ({});
    const float ladder = bright ({ { p::layerFilterType (2), filterTypeValue (p::FilterType::ladder24) }, { p::layerCutoff (2), 300.0f }, { p::layerKeyTrack (2), 0.0f } });
    const float lp12 = bright ({ { p::layerFilterType (2), filterTypeValue (p::FilterType::lowPass12) }, { p::layerCutoff (2), 300.0f }, { p::layerKeyTrack (2), 0.0f } });
    const float hp = bright ({ { p::layerFilterType (2), filterTypeValue (p::FilterType::highPass) }, { p::layerCutoff (2), 3000.0f }, { p::layerKeyTrack (2), 0.0f } });
    std::cout << "info: layer filter highs share: open " << open << ", ladder " << ladder << ", LP12 " << lp12 << ", HP " << hp << "\n";
    CHECK (ladder < open * 1.0e-3f, "layer ladder 24 at 300 Hz removes the highs (30 dB down)");
    CHECK (lp12 > ladder && lp12 < open * 0.1f, "layer LP 12 is gentler than the ladder");
    CHECK (hp > 4.0f * open, "layer HP keeps the highs and removes the lows");

    // Band-pass: most of the energy sits within an octave of the cutoff
    {
        JpRig rig ({ { Jp::unison, Jp::choiceValue (0, Jp::numUnison) } });
        rig.set (p::layerFilterType (2), filterTypeValue (p::FilterType::bandPass));
        rig.set (p::layerCutoff (2), 1000.0f);
        rig.set (p::layerKeyTrack (2), 0.0f);
        rig.set (p::layerResonance (2), 60.0f);
        auto x = rig.render (24000, { { 0, on (45) } });
        const float share = bandEnergy (x, 12000, 700.0, 1400.0) / bandEnergy (x, 12000, 50.0, 20000.0);
        CHECK (share > 0.5f, "layer BP keeps the band around its cutoff (" + std::to_string (share) + " of the energy)");
    }

    // Key track 100 %: the cutoff follows the note, so two octaves apart the tone keeps its balance
    auto keyed = [&] (float key, int note)
    {
        JpRig rig ({ { Jp::unison, Jp::choiceValue (0, Jp::numUnison) } });
        rig.set (p::layerFilterType (2), filterTypeValue (p::FilterType::ladder24));
        rig.set (p::layerCutoff (2), 400.0f);
        rig.set (p::layerKeyTrack (2), key);
        auto x = rig.render (24000, { { 0, on (note) } });
        const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        return bandEnergy (x, 12000, 3.5 * f0, 20000.0) / bandEnergy (x, 12000, 0.5 * f0, 20000.0);
    };
    const float trackedLow = keyed (100.0f, 45), trackedHigh = keyed (100.0f, 69);
    const float fixedLow = keyed (0.0f, 45), fixedHigh = keyed (0.0f, 69);
    const float trackedDb = std::abs (juce::Decibels::gainToDecibels (trackedHigh / trackedLow));
    const float fixedDb = std::abs (juce::Decibels::gainToDecibels (fixedHigh / fixedLow));
    std::cout << "info: key track: upper-harmonic share changes " << trackedDb << " dB tracked, " << fixedDb << " dB fixed over 2 octaves\n";
    CHECK (trackedDb < 3.0f && fixedDb > trackedDb + 10.0f, "key track 100 % keeps the tone across the keyboard");

    // Filter envelope: bright at the attack, dark once decayed to its zero sustain
    {
        JpRig rig ({ { Jp::unison, Jp::choiceValue (0, Jp::numUnison) } });
        rig.set (p::layerFilterType (2), filterTypeValue (p::FilterType::ladder24));
        rig.set (p::layerCutoff (2), 200.0f);
        rig.set (p::layerKeyTrack (2), 0.0f);
        rig.set (p::layerFilterEnv (2), 48.0f);
        rig.set (p::layerFilterDecay (2), 150.0f);
        auto x = rig.render (48000, { { 0, on (45) } });
        CHECK (bandEnergy (x, 0, 1000.0, 20000.0) > 30.0f * bandEnergy (x, 36000, 1000.0, 20000.0), "the layer filter envelope opens and closes the filter");
    }

    // Full resonance with full drive on every type stays bounded
    for (int type = 1; type <= 4; ++type)
    {
        JpRig rig;
        rig.set (p::layerFilterType (2), (float) type);
        rig.set (p::layerCutoff (2), 800.0f);
        rig.set (p::layerResonance (2), 100.0f);
        rig.set (p::layerDrive (2), 100.0f);
        auto x = rig.render (48000, { { 0, on (40, 1.0f) }, { 0, on (64, 1.0f) }, { 0, on (71, 1.0f) } });
        CHECK (allFinite (x) && peak (x) < 4.0f, "layer filter at full resonance and drive stays bounded (type " + std::to_string (type) + ")");
    }

    // Switching the type while a note sounds crossfades: no step bigger than the tone's own slope
    {
        Rig rig;
        rig.set (p::layerCutoff (2), 2000.0f);
        rig.set (p::layerKeyTrack (2), 0.0f);
        auto steady = rig.render (12000, { { 0, on (57) } });
        const float base = maxStep (std::vector<float> (steady.begin() + 6000, steady.end()));
        float worst = 0.0f;
        for (int type : { 1, 2, 4, 3, 0, 4, 1 })
        {
            rig.set (p::layerFilterType (2), (float) type);
            auto x = rig.render (4800);
            worst = std::max (worst, maxStep (x));
        }
        CHECK (worst < 1.6f * base, "switching the layer filter type does not click (" + std::to_string (worst / base) + "x the tone's slope)");
    }
}

void testLayerDrive()
{
    namespace p = ps::params;
    auto play = [] (float drive, int note, std::vector<float>* rightOut = nullptr)
    {
        Rig rig;
        rig.set (p::layerDrive (2), drive);
        auto x = rig.render (48000, { { 0, on (note, 1.0f) } });
        if (rightOut != nullptr)
            *rightOut = rig.right;
        return x;
    };
    const auto clean = play (0.0f, 57), driven = play (100.0f, 57);
    const float third = bandEnergy (driven, 24000, 600.0, 720.0) / bandEnergy (driven, 24000, 180.0, 260.0);
    const float cleanThird = bandEnergy (clean, 24000, 600.0, 720.0) / bandEnergy (clean, 24000, 180.0, 260.0);
    std::cout << "info: drive 100 % third harmonic " << juce::Decibels::gainToDecibels (third) * 0.5f << " dB (clean "
              << juce::Decibels::gainToDecibels (cleanThird) * 0.5f << " dB)\n";
    CHECK (third > 1.0e-3f && cleanThird < 1.0e-8f, "drive adds harmonics, drive 0 adds none");
    const float levelDb = juce::Decibels::gainToDecibels (rms (driven, 24000, 48000) / rms (clean, 24000, 48000));
    CHECK (std::abs (levelDb) < 3.0f, "full drive stays within 3 dB of the clean level (" + std::to_string (levelDb) + " dB)");

    const int note = 99;   // 2.49 kHz
    const auto high = play (100.0f, note);
    const float alias = worstAliasDb (high, 24000, 440.0 * std::pow (2.0, (note - 69) / 12.0));
    std::cout << "info: drive 100 % at 2.5 kHz, worst alias " << alias << " dB\n";
    CHECK (alias < -60.0f, "full drive at 2.5 kHz keeps aliases below -60 dB");

    // Turning the drive up and down while the note sounds steps nowhere: the slope while the drive moves stays
    // within the steeper of the tone's slopes before and after (saturation itself steepens the edges)
    Rig rig;
    auto steady = rig.render (12000, { { 0, on (57, 1.0f) } });
    float before = maxStep (std::vector<float> (steady.begin() + 9600, steady.end())), worst = 0.0f;
    for (float d : { 100.0f, 0.0f, 60.0f, 3.0f, 100.0f })
    {
        rig.set (p::layerDrive (2), d);
        auto x = rig.render (4800);
        const float after = maxStep (std::vector<float> (x.begin() + 2400, x.end()));
        worst = std::max (worst, maxStep (std::vector<float> (x.begin(), x.begin() + 600)) / std::max (before, after));
        before = after;
    }
    CHECK (worst < 1.1f, "jumping the drive does not click (" + std::to_string (worst) + "x the steadier slope)");
}

void testLayerChorus()
{
    namespace p = ps::params;
    // A single mono saw (JP, no unison, no spread)
    auto play = [] (float amount, std::initializer_list<int> notes, std::vector<float>& right)
    {
        JpRig rig ({ { Jp::unison, Jp::choiceValue (0, Jp::numUnison) }, { Jp::spread, 0.0f } });
        rig.set (p::layerChorus (2), amount);
        std::vector<std::pair<int, juce::MidiMessage>> events;
        for (int n : notes)
            events.push_back ({ 0, on (n) });
        auto x = rig.render (48000, events);
        right = rig.right;
        return x;
    };
    auto correlation = [] (const std::vector<float>& l, const std::vector<float>& r, size_t from)
    {
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = from; i < l.size(); ++i) { lr += (double) l[i] * r[i]; ll += (double) l[i] * l[i]; rr += (double) r[i] * r[i]; }
        return lr / std::sqrt (ll * rr);
    };
    std::vector<float> dryR, wetR, lowR;
    const auto dry = play (0.0f, { 69 }, dryR), wet = play (100.0f, { 69 }, wetR);
    const double c = correlation (wet, wetR, 12000);
    std::cout << "info: chorus 100 % on a mono saw: L/R correlation " << c << "\n";
    CHECK (correlation (dry, dryR, 12000) > 0.9999 && c < 0.9 && c > 0.0, "the chorus widens a mono layer and stays mono-compatible");
    const float levelDb = juce::Decibels::gainToDecibels ((rms (wet, 12000, 48000) + rms (wetR, 12000, 48000)) / (rms (dry, 12000, 48000) + rms (dryR, 12000, 48000)));
    CHECK (std::abs (levelDb) < 1.5f, "the chorus keeps the layer's level (" + std::to_string (levelDb) + " dB)");

    const auto low = play (100.0f, { 33 }, lowR);   // 55 Hz saw
    std::vector<float> side (low.size()), mid (low.size());
    for (size_t i = 0; i < low.size(); ++i) { side[i] = 0.5f * (low[i] - lowR[i]); mid[i] = 0.5f * (low[i] + lowR[i]); }
    CHECK (bandEnergy (side, 24000, 20.0, 150.0) < 1.0e-4f * bandEnergy (mid, 24000, 20.0, 150.0), "the chorus leaves the lows mono");
    std::vector<float> lowDryR;
    const auto lowDry = play (0.0f, { 33 }, lowDryR);
    const float lowDb = juce::Decibels::gainToDecibels (bandEnergy (mid, 24000, 40.0, 150.0) / bandEnergy (lowDry, 24000, 40.0, 150.0)) * 0.5f;
    CHECK (std::abs (lowDb) < 0.5f, "the chorus keeps the lows' level (" + std::to_string (lowDb) + " dB)");

    // Turning it on and off while a note sounds never clicks: the slope while it moves stays within the
    // steeper of the steady slopes before and after
    Rig rig;
    auto steady = rig.render (12000, { { 0, on (57) } });
    float before = maxStep (std::vector<float> (steady.begin() + 9600, steady.end())), worst = 0.0f;
    for (float a : { 100.0f, 0.0f, 50.0f, 100.0f })
    {
        rig.set (p::layerChorus (2), a);
        auto x = rig.render (96000);   // a whole LFO cycle at 0.4 Hz, past the ramp
        const float after = maxStep (std::vector<float> (x.begin() + 2400, x.end()));
        worst = std::max (worst, maxStep (std::vector<float> (x.begin(), x.begin() + 600)) / std::max (before, after));
        before = after;
    }
    CHECK (worst < 1.1f, "switching the chorus does not click (" + std::to_string (worst) + "x)");
}

// ---- Shared sends ---------------------------------------------------------------------------------------

void testSends()
{
    namespace p = ps::params;
    // Reverb: the tail after a release grows with Amount and lasts longer with Size
    auto tail = [] (float amount, float size)
    {
        Rig rig;
        rig.set (p::reverbAmount, amount);
        rig.set (p::reverbSize, size);
        auto x = rig.render (96000, { { 0, on (57) }, { 24000, off (57) } });
        return juce::Decibels::gainToDecibels (rms (x, 24000 + 24000, 24000 + 36000) / rms (x, 12000, 24000), -200.0f);
    };
    const float dry = tail (0.0f, 2.2f), wet = tail (60.0f, 2.2f), longer = tail (60.0f, 8.0f);
    std::cout << "info: reverb tail 0.5 s after release: dry " << dry << " dB, 60 % " << wet << " dB, 60 % at 8 s " << longer << " dB\n";
    CHECK (wet > dry + 20.0f && wet < -6.0f, "the reverb send rings on after the release");
    CHECK (longer > wet + 3.0f, "a larger reverb size rings longer");

    // Delay: a short blip at 120 BPM, 1/4 note: repeats every 0.5 s, the first in the left (ping-pong)
    {
        Rig rig;
        rig.set (p::delayAmount, 100.0f);
        rig.set (p::delayTime, 8.0f);   // 1/4
        rig.set (p::delayFeedback, 50.0f);
        rig.set (p::layerRelease (2), 5.0f);
        auto x = rig.render (96000, { { 0, on (69) }, { 1200, off (69) } });
        const int lat = rig.proc->getLatencySamples();
        const float blip = rms (x, (size_t) lat, (size_t) lat + 1200);
        const float gap = rms (x, (size_t) lat + 6000, (size_t) lat + 20000);
        const float echoL = rms (x, (size_t) lat + 24000, (size_t) lat + 25200), echoR = rms (rig.right, (size_t) lat + 24000, (size_t) lat + 25200);
        const float echo2R = rms (rig.right, (size_t) lat + 48000, (size_t) lat + 49200);
        std::cout << "info: delay echo 1 L " << juce::Decibels::gainToDecibels (echoL / blip) << " dB, R " << juce::Decibels::gainToDecibels (echoR / blip)
                  << " dB, echo 2 R " << juce::Decibels::gainToDecibels (echo2R / blip) << " dB, gap " << juce::Decibels::gainToDecibels (gap / blip, -200.0f) << " dB\n";
        CHECK (echoL > 0.2f * blip && echoL > 4.0f * echoR, "the delay repeats on the beat, first in the left");
        CHECK (echo2R > 0.05f * blip && gap < 0.02f * blip, "ping-pong: the second repeat crosses to the right, silence between");
    }

    // Full feedback stays bounded, and switching the time while it rings never clicks
    {
        Rig rig;
        rig.set (p::delayAmount, 100.0f);
        rig.set (p::delayFeedback, 95.0f);
        rig.set (p::reverbAmount, 100.0f);
        rig.set (p::reverbSize, 12.0f);
        auto steady = rig.render (48000, { { 0, on (57) } });
        float worst = 0.0f;
        for (float division : { 2.0f, 11.0f, 5.0f, 0.0f })
        {
            rig.set (p::delayTime, division);
            auto x = rig.render (24000);
            worst = std::max (worst, maxStep (x) / maxStep (std::vector<float> (steady.begin() + 24000, steady.end())));
        }
        auto x = rig.render (48000 * 8, { { 0, off (57) } });
        CHECK (allFinite (x) && peak (x) < 1.0f, "full delay feedback and the longest reverb stay bounded");
        CHECK (worst < 1.5f, "changing the delay time while it rings does not click (" + std::to_string (worst) + "x)");
    }

    // Mono lows: the sends add no side signal under 150 Hz
    {
        Rig rig;
        rig.set (p::delayAmount, 100.0f);
        rig.set (p::reverbAmount, 100.0f);
        auto x = rig.render (96000, { { 0, on (33) }, { 0, on (69) } });   // 55 Hz and 440 Hz
        std::vector<float> side (x.size()), mid (x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            side[i] = 0.5f * (x[i] - rig.right[i]);
            mid[i] = 0.5f * (x[i] + rig.right[i]);
        }
        const float ratio = bandEnergy (side, 48000, 20.0, 150.0) / bandEnergy (mid, 48000, 20.0, 150.0);
        CHECK (ratio < 1.0e-4f, "the sends keep the lows mono (side/mid under 150 Hz " + std::to_string (juce::Decibels::gainToDecibels (ratio) * 0.5f) + " dB)");
    }
}

// ---- Sub/body phase alignment ------------------------------------------------------------------------

void testPhaseAlign()
{
    namespace p = ps::params;
    // Sub engine on layer 1, a single JP saw (or the JP 7 supersaw) as the Body, Mid and Air muted.
    // Each note is played twice from silence: the first time is measured, the second is corrected.
    struct Result { float first, second, subOnly, bodyOnly; bool valid; };
    auto run = [] (int note, bool singleSaw, float strength, float subLevel = 0.0f, float bodyLevel = 0.0f)
    {
        StackRig rig;
        rig.proc->setLayerEngine (1, "jp");
        rig.render (blockSize);
        rig.collectRetired();
        if (singleSaw)
            rig.set (p::layerSlot (1, Jp::unison), Jp::choiceValue (0, Jp::numUnison));
        rig.set (p::phaseAlign, strength);
        rig.set (p::layerLevel (2), -60.0f);
        rig.set (p::layerLevel (3), -60.0f);
        rig.set (p::layerLevel (0), subLevel);
        rig.set (p::layerLevel (1), bodyLevel);
        rig.render (blockSize);
        auto x = rig.render (192000, { { 0, on (note) }, { 36000, off (note) }, { 96000, on (note) }, { 132000, off (note) } });
        const bool valid = rig.proc->getStack().layer (0).alignmentFor (note).valid;
        // The fundamental, where the two layers overlap (the body's harmonics have nothing to line up with)
        const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        return std::make_tuple (bandEnergy (x, 18000, 0.85 * f0, 1.15 * f0), bandEnergy (x, 96000 + 18000, 0.85 * f0, 1.15 * f0), valid);
    };
    for (int note : { 40, 43, 45 })
    {
        const auto [first, second, valid] = run (note, true, 100.0f);
        const float s = std::get<0> (run (note, true, 100.0f, 0.0f, -60.0f)) * 0.5f;   // solo: undo the sqrt-weights
        const float b = std::get<0> (run (note, true, 100.0f, -60.0f, 0.0f)) * 0.5f;
        const float best = juce::square (std::sqrt (s) + std::sqrt (b));
        const float firstDb = juce::Decibels::gainToDecibels (first / best) * 0.5f, secondDb = juce::Decibels::gainToDecibels (second / best) * 0.5f;
        std::cout << "info: note " << note << " sub+saw body at the fundamental: " << firstDb << " dB before, " << secondDb << " dB after alignment (vs fully in phase)\n";
        CHECK (valid && secondDb > -0.2f && secondDb > firstDb, "phase alignment brings the sub and body in phase at note " + std::to_string (note));
    }
    // Strength 0 changes nothing; a supersaw body (no stable phase) gets no correction
    {
        const auto [first, second, valid] = run (43, true, 0.0f);
        std::cout << "info: 0 %: first " << first << " second " << second << "\n";
        CHECK (std::abs (juce::Decibels::gainToDecibels (second / first)) < 0.05f, "phase align at 0 % leaves the sub as it is");
        const auto [f2, s2, v2] = run (43, false, 100.0f);
        CHECK (! v2, "a supersaw body is not phase-aligned (no stable phase relation)");
        juce::ignoreUnused (valid, f2, s2);
    }
}

void testOddBlockSizes()
{
    Rig rig;
    auto x = rig.render (20000, { { 5, on (50) }, { 9000, off (50) } }, 1031);   // larger than prepared, odd
    CHECK (allFinite (x) && peak (x) > 0.1f, "blocks larger than prepared still render");
    auto y = rig.render (2000, { { 3, on (55) } }, 1);
    CHECK (allFinite (y), "single-sample blocks render");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (juce::SystemStats::getEnvironmentVariable ("POTATO_BENCH_ONLY", {}).isNotEmpty())   // for profiling
    {
        benchmarkJp();
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("POTATO_ARP_ONLY", {}).isNotEmpty())
    {
        testArpeggiator();
        testHold();
    testCutoffSweep();
    testExpression();
        testStepSequencer();
        testArpThroughTheSynth();
        testModLanes();
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("POTATO_DIRT_ONLY", {}).isNotEmpty())
    {
        testDirtEngine();
        testDirtSwitchesDoNotClick();
        benchmarkDirt();
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("POTATO_PRESETS_ONLY", {}).isNotEmpty())
    {
        testPresetLoading();
        testPressureAfterPause();
        testFactoryPresets();
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("POTATO_MASTER_ONLY", {}).isNotEmpty())
    {
        testMasterChain();
        testSubDuck();
        testInserts();
        benchmarkInserts();
        testMixHealth();
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("POTATO_GENOME_ONLY", {}).isNotEmpty())
    {
        testGenomeEngine();
        testGenomeSwitchesDoNotClick();
        benchmarkGenome();
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("POTATO_SPECTRAL_ONLY", {}).isNotEmpty())
    {
        testSpectralEngine();
        benchmarkSpectral();
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("POTATO_LAYER_ONLY", {}).isNotEmpty())   // filter and drive
    {
        testLayerFilter();
        testLayerDrive();
        testSends();
        testLayerChorus();
        testPhaseAlign();
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("POTATO_KNOB_REPORT", {}).isNotEmpty())
    {
        knobReport();
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("POTATO_CHAIN_ONLY", {}).isNotEmpty())   // iterating on the knobs
    {
        testChainPaths();
        testBeatCeiling();
        testBloomIsRepeatable();
        testBassHarmonics();
        testOnsetDuck();
        testBloomSpace();
        testDropOnTheBar();
        testXyPad();
        testChainKnobInvariants();
        return failures == 0 ? 0 : 1;
    }

    testNoteOnPitchAndRelease();
    testSampleAccurateStart();
    testVoiceStealingAndChords();
    testSustainPedal();
    testHold();
    testCutoffSweep();
    testExpression();
    testLayerMuteAndEmpty();
    testEngineSwapWhileSounding();
    testLayersStack();
    testStateRoundTrip();
    testEngineSlots();
    testAttackDecaySustain();
    testReleaseTime();
    testRetriggerDuringRelease();
    testPan();
    testTune();
    testOldStateLoadsDefaults();
    testMonoLastNotePriority();
    testLegatoDoesNotRetrigger();
    testGlide();
    testPolyGlide();
    testVoiceModeSwitchWhileSounding();
    testRoleBands();
    testSubAndBodySumFlat();
    testAddingALayerKeepsThePower();
    testRoleChangeWhilePlaying();
    testMonoBass();
    testDefaultStackIsTrueSilentAfterNotes();
    testMeters();
    testTruePeakSafeOutput();
    testSafeOutputIsTransparentBelowTheKnee();
    testSafeOutputDoesNotAlias();
    testLatencyIsConstant();
    benchmarkStack();
    testOddBlockSizes();
    testJpPitchAndSlots();
    testJpAliasing();
    testJpSuperSawLevelAndWidth();
    testJpFilter();
    testJpOsc2();
    testJpSwitchesDoNotClick();
    testSubEngine();
    testWavetableEngine();
    testSpectralEngine();
    testDirtEngine();
    testDirtSwitchesDoNotClick();
    testGenomeEngine();
    testGenomeSwitchesDoNotClick();
    testBeatCeiling();
    testBloomIsRepeatable();
    testMasterChain();
    testSubDuck();
    testInserts();
    testMixHealth();
    testPresetLoading();
    testFactoryPresets();
    testArpeggiator();
    testStepSequencer();
    testArpThroughTheSynth();
    testModLanes();
    testLayerFilter();
    testLayerDrive();
    testSends();
    testLayerChorus();
    testPhaseAlign();
    testChainPaths();
    testBassHarmonics();
    testOnsetDuck();
    testBloomSpace();
    testDropOnTheBar();
    testXyPad();
    testChainKnobInvariants();
    benchmarkJp();
    benchmarkWavetable();
    benchmarkSpectral();
    benchmarkDirt();
    benchmarkGenome();
    benchmarkInserts();

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
