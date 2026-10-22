// Headless tests for the layer stack, run end to end through the plugin processor.
// Build with -DPOTATO_DEV_TOOLS=ON, run with ctest or ./build/PotatoSynthTests.
#include "PluginProcessor.h"
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

    Rig()
    {
        proc->setPlayConfigDetails (0, 2, sampleRate, blockSize);
        proc->prepareToPlay (sampleRate, blockSize);
    }

    // Renders numSamples, collecting the left channel. Events are (sample offset, message).
    std::vector<float> render (int numSamples, std::vector<std::pair<int, juce::MidiMessage>> events = {},
                               int block = blockSize)
    {
        std::vector<float> out;
        out.reserve ((size_t) numSamples);
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
                out.push_back (buf.getSample (0, i));
        }
        return out;
    }

    void collectRetired() { proc->collectRetiredEngines(); }
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

double estimateFrequency (const std::vector<float>& x, size_t from, size_t to)
{
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            ++crossings;
    return crossings * sampleRate / (double) (to - from);
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
    CHECK (std::abs (f - 440.0) < 1.0, "A4 plays at 440 Hz (measured " + juce::String (f, 2).toStdString() + ")");
    CHECK (peak (x, 24000 + 9600, 24000 + 14400) < peak (x, 4000, 24000) * 0.1f, "release falls 20 dB within 200 ms");
    CHECK (peak (x, 24000 + 43200, 72000) < 1.0e-4f, "note is fully gone 0.9 s after release");
    CHECK (maxStep (x) < 0.05f, "no clicks at note on or off");
}

void testSampleAccurateStart()
{
    Rig rig;
    auto x = rig.render (2048, { { 777, on (60) } });
    CHECK (peak (x, 0, 777) <= 0.0f && std::abs (x[778]) > 0.0f, "note starts on its exact sample");
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
    CHECK (peak (two, 4800, 9600) > peak (one, 4800, 9600) * 1.9f, "two layers both sound on one note");
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

    testNoteOnPitchAndRelease();
    testSampleAccurateStart();
    testVoiceStealingAndChords();
    testSustainPedal();
    testLayerMuteAndEmpty();
    testEngineSwapWhileSounding();
    testLayersStack();
    testStateRoundTrip();
    testOddBlockSizes();

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
