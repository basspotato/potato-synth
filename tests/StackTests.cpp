// Headless tests for the layer stack, run end to end through the plugin processor.
// Build with -DPOTATO_DEV_TOOLS=ON, run with ctest or ./build/PotatoSynthTests.
#include "PluginProcessor.h"
#include "Loudness.h"
#include "EngineRegistry.h"
#include "ChainKnobs.h"
#include <cmath>
#include <iostream>

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

// Strongest spectral line that is not a harmonic of f0 (below 20 kHz), relative to the fundamental, in dB.
// Reads 16384 samples from `from`.
float worstAliasDb (const std::vector<float>& x, size_t from, double f0)
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
        if (std::abs (f - f0) < 40.0)
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

// The full default-role stack with an engine on every layer: Sub, Wavetable body, JP mid, Wavetable air
struct StackRig : Rig
{
    StackRig() : Rig (false)
    {
        const char* engines[] = { "sub", "wavetable", "jp", "wavetable" };
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

    auto knobId = [] (int k) { return k < ps::params::numChainKnobs ? ps::params::chainKnob (k) : ps::params::super; };
    for (int k = 0; k <= ps::params::numChainKnobs; ++k)
    {
        const std::string name = k < ps::params::numChainKnobs ? ps::params::chainKnobNames[k] : "SUPER";
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

    testNoteOnPitchAndRelease();
    testSampleAccurateStart();
    testVoiceStealingAndChords();
    testSustainPedal();
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
    testChainPaths();
    testOnsetDuck();
    testChainKnobInvariants();
    benchmarkJp();
    benchmarkWavetable();

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
