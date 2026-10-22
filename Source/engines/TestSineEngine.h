#pragma once
#include "Engine.h"
#include <cmath>
#include <vector>

namespace ps
{
// Reference engine for the stack: one sine per voice with click-free one-pole envelopes.
// Used to verify voice handling, layering and gain staging before the real engines arrive.
class TestSineEngine final : public Engine
{
public:
    const EngineInfo& info() const override
    {
        static const EngineInfo i { "test.sine", "Test Sine", 16, { { "Octave", 0.5f } } };
        return i;
    }

    void prepare (double sampleRate, int, int numVoices) override
    {
        sr = sampleRate;
        voices.assign ((size_t) numVoices, {});
        attackCoef  = coefFor (0.003);
        releaseCoef = coefFor (0.060);
        stealCoef   = coefFor (0.002);
    }

    void reset() override
    {
        for (auto& v : voices)
            v = {};
    }

    void noteOn (int voice, int midiNote, float velocity) override
    {
        auto& v = voices[(size_t) voice];
        // Phase and current amplitude carry over, so a retriggered voice glides instead of clicking
        v.increment = 440.0 * std::pow (2.0, (midiNote - 69 + octaveShift * 12) / 12.0) / sr;
        v.target = velocity;
        v.released = false;
        v.coef = attackCoef;
        v.active = true;
    }

    void noteOff (int voice) override  { setRelease (voice, releaseCoef); }
    void steal (int voice) override    { setRelease (voice, stealCoef); }
    bool isActive (int voice) const override { return voices[(size_t) voice].active; }

    void setSlot (int slot, float value) override
    {
        if (slot == 0)
            octaveShift = juce::roundToInt (value * 4.0f) - 2;
    }

    void render (int voice, float* left, float* right, int numSamples) override
    {
        auto& v = voices[(size_t) voice];
        if (! v.active)
            return;

        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        for (int i = 0; i < numSamples; ++i)
        {
            v.amp += (v.target - v.amp) * v.coef;
            const float s = (float) std::sin (twoPi * v.phase) * v.amp * outputGain;
            left[i] += s;
            right[i] += s;
            v.phase += v.increment;
            v.phase -= std::floor (v.phase);
        }

        if (v.released && v.amp < 1.0e-5f)
            v = {};
    }

private:
    struct Voice
    {
        double phase = 0.0, increment = 0.0;
        float amp = 0.0f, target = 0.0f, coef = 0.0f;
        bool active = false, released = false;
    };

    static constexpr float outputGain = 0.25f;   // about -12 dBFS per voice

    float coefFor (double seconds) const { return (float) (1.0 - std::exp (-1.0 / (seconds * sr))); }

    void setRelease (int voice, float coef)
    {
        auto& v = voices[(size_t) voice];
        v.target = 0.0f;
        v.coef = coef;
        v.released = true;
    }

    double sr = 48000.0;
    std::vector<Voice> voices;
    float attackCoef = 0.0f, releaseCoef = 0.0f, stealCoef = 0.0f;
    int octaveShift = 0;
};
}
