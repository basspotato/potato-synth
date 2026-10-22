#pragma once
#include "Engine.h"
#include "Glide.h"
#include <cmath>
#include <vector>

namespace ps
{
// Reference engine for the stack: one sine per voice. The layer's amp envelope shapes it.
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
        glideCoef = (float) (1.0 - std::exp (-1.0 / (0.003 * sr)));
    }

    void reset() override
    {
        for (auto& v : voices)
            v = {};
    }

    void noteOn (int voice, const NoteEvent& e) override
    {
        auto& v = voices[(size_t) voice];
        // A retriggered voice keeps its phase and glides to the new velocity, so it never clicks
        if (! v.active)
        {
            v.amp = e.velocity;
            v.pitch.jumpTo (e.glideSeconds > 0.0 && e.glideFrom >= 0.0f ? e.glideFrom : (float) e.note);
        }
        if (! e.legato || ! v.active)
            v.target = e.velocity;
        v.pitch.glideTo ((float) e.note, e.glideSeconds, sr);
        v.active = true;
        v.increment = incrementFor (v.pitch.getCurrent());
    }

    void noteOff (int) override {}
    void endVoice (int voice) override { voices[(size_t) voice] = {}; }

    void setTune (float semitones) override
    {
        if (std::abs (semitones - tune) < 1.0e-6f)
            return;
        tune = semitones;
        for (auto& v : voices)
            if (v.active)
                v.increment = incrementFor (v.pitch.getCurrent());
    }

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
            if (v.pitch.isGliding())
                v.increment = incrementFor (v.pitch.next());
            v.amp += (v.target - v.amp) * glideCoef;
            const float s = (float) std::sin (twoPi * v.phase) * v.amp * outputGain;
            left[i] += s;
            right[i] += s;
            v.phase += v.increment;
            v.phase -= std::floor (v.phase);
        }
    }

private:
    struct Voice
    {
        double phase = 0.0, increment = 0.0;
        float amp = 0.0f, target = 0.0f;
        PitchGlide pitch;
        bool active = false;
    };

    static constexpr float outputGain = 0.25f;   // about -12 dBFS per voice

    double incrementFor (float pitch) const
    {
        return 440.0 * std::exp2 ((pitch - 69.0 + octaveShift * 12 + (double) tune) / 12.0) / sr;
    }

    double sr = 48000.0;
    std::vector<Voice> voices;
    float glideCoef = 1.0f, tune = 0.0f;
    int octaveShift = 0;
};
}
