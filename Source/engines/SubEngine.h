#pragma once
#include "Engine.h"
#include "Glide.h"
#include "Filter.h"
#include "Drive.h"
#include "Unison.h"
#include <array>
#include <cmath>
#include <vector>

namespace ps
{
// Sub: the low end of the stack. One phase-locked oscillator per voice whose Shape morphs sine -> triangle ->
// square (PolyBLEP/BLAMP, each at the sine's loudness), plus a sine an octave down, a pitch "punch" envelope
// for 808-style drops, ADAA drive with an Even control (asymmetry for 2nd-harmonic warmth), a Tone low-pass
// and a 12 dB Low Cut (which also removes the DC that Even leaves).
//
// Translate adds phase-locked harmonics 2-5 so the sub still reads on small speakers that can't play its
// fundamental: the ear rebuilds the missing fundamental from them. The sum is power-normalised, so turning it
// up never raises the level.
//
// Phase: Retrig starts every new note at 0 degrees, so a sub always hits the same way (and starts from zero,
// so it never clicks); Free keeps running from where the voice left off. A note on a sounding voice always
// glides on from the current phase.
class SubEngine final : public Engine
{
public:
    enum Slot { shape, subOctave, translate, drive, even, tone, lowCut, phase, punch, punchDecay, numUsedSlots };

    const EngineInfo& info() const override
    {
        static const EngineInfo i { "sub", "Sub", 16, makeSlots() };
        return i;
    }

    void prepare (double sampleRate, int, int numVoices) override
    {
        sr = (float) sampleRate;
        voices.assign ((size_t) numVoices, {});
    }

    void reset() override
    {
        for (auto& v : voices)
            resetVoice (v);
    }

    void noteOn (int index, const NoteEvent& e) override
    {
        auto& v = voices[(size_t) index];
        if (! v.active)
        {
            resetVoice (v);
            v.pitch.jumpTo (e.glideSeconds > 0.0 && e.glideFrom >= 0.0f ? e.glideFrom : (float) e.note);
            if (phaseMode() == 1)
                v.t = v.tSub = 0.0f;
            v.smooth = target;
            v.active = true;
        }
        if (! e.legato)
            v.punch = 1.0f;   // the punch restarts with each attack
        v.pitch.glideTo ((float) e.note, e.glideSeconds, sr);
    }

    void noteOff (int) override {}
    void endVoice (int index) override { resetVoice (voices[(size_t) index]); }
    void setTune (float semitones) override { tune = semitones; }

    void setSlot (int slot, float value) override
    {
        if (slot >= 0 && slot < numUsedSlots)
            target[(size_t) slot] = juce::jlimit (0.0f, 1.0f, value);
    }

    void render (int index, float* left, float* right, int numSamples) override
    {
        auto& v = voices[(size_t) index];
        if (! v.active)
            return;
        for (int pos = 0; pos < numSamples; pos += subBlock)
            renderSubBlock (v, left + pos, right + pos, juce::jmin (subBlock, numSamples - pos));
    }

    // ---- Slot mappings -----------------------------------------------------------------------------------
    static float toneHz (float v) noexcept        { return 40.0f * std::pow (500.0f, v); }
    static float lowCutHz (float v) noexcept      { return 5.0f + 35.0f * v; }
    static float lowCutValue (float hz) noexcept  { return (hz - 5.0f) / 35.0f; }
    static float punchSemis (float v) noexcept    { return v * 36.0f; }
    static float punchMs (float v) noexcept       { return 5.0f + 495.0f * v * v; }
    static float punchValue (float ms) noexcept   { return std::sqrt ((ms - 5.0f) / 495.0f); }

private:
    static constexpr int subBlock = 16;
    static constexpr float voiceLevel = 0.25f;   // a sine at -15 dBFS RMS, like the other engines

    struct Voice
    {
        bool active = false;
        PitchGlide pitch;
        float t = 0.0f, tSub = 0.0f, punch = 0.0f;
        std::array<float, numUsedSlots> smooth {};
        ZdfSvf toneFilter;
        ZdfSvf lowCutFilter;
        AdaaTanh saturator;
    };

    static std::vector<SlotInfo> makeSlots()
    {
        auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        std::vector<SlotInfo> s ((size_t) numUsedSlots);
        s[shape]      = { "Shape", 0.0f, [] (float v)
                          {
                              if (v < 0.01f) return juce::String ("Sine");
                              if (v > 0.99f) return juce::String ("Square");
                              if (std::abs (v - 0.5f) < 0.01f) return juce::String ("Triangle");
                              return v < 0.5f ? "Sine > Tri " + juce::String (juce::roundToInt (v * 200.0f)) + " %"
                                              : "Tri > Sqr " + juce::String (juce::roundToInt ((v - 0.5f) * 200.0f)) + " %";
                          } };
        s[subOctave]  = { "Sub -1 Oct", 0.0f, percent };
        s[translate]  = { "Translate", 0.0f, percent };
        s[drive]      = { "Drive", 0.0f, percent };
        s[even]       = { "Even", 0.0f, percent };
        s[tone]       = { "Tone", 1.0f, [] (float v)
                          {
                              const float f = toneHz (v);
                              return f < 1000.0f ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f * 0.001f, 1) + " kHz";
                          } };
        s[lowCut]     = { "Low Cut", lowCutValue (20.0f), [] (float v) { return juce::String (juce::roundToInt (lowCutHz (v))) + " Hz"; } };
        s[phase]      = { "Phase", 0.75f, [] (float v) { return juce::String (v < 0.5f ? "Free" : "Retrig"); } };
        s[punch]      = { "Punch", 0.0f, [] (float v) { return "+" + juce::String (punchSemis (v), 1) + " st"; } };
        s[punchDecay] = { "Punch Time", punchValue (60.0f), [] (float v) { return juce::String (juce::roundToInt (punchMs (v))) + " ms"; } };
        return s;
    }

    static std::array<float, numUsedSlots> defaults()
    {
        std::array<float, numUsedSlots> d {};
        const auto slots = makeSlots();
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = slots[i].defaultValue;
        return d;
    }

    int phaseMode() const noexcept { return target[phase] < 0.5f ? 0 : 1; }

    void resetVoice (Voice& v) noexcept
    {
        v.active = false;
        v.punch = 0.0f;
        v.toneFilter.reset();
        v.lowCutFilter.reset();
        v.saturator.reset();
    }

    // Sine, triangle and square share a phase (all rise through zero at t = 0) and the sine's RMS
    static float morph (float t, float dt, float s) noexcept
    {
        const float sine = std::sin (juce::MathConstants<float>::twoPi * t);
        if (s <= 0.0f)
            return sine;
        const float tri = blep::wave<blep::triangle> (blep::wrap (t - 0.25f), dt, 0.5f) * 1.2247449f;
        if (s <= 0.5f)
            return sine + (tri - sine) * (s * 2.0f);
        const float square = blep::wave<blep::pulse> (t, dt, 0.5f) * 0.70710678f;
        return tri + (square - tri) * ((s - 0.5f) * 2.0f);
    }

    void renderSubBlock (Voice& v, float* outL, float* outR, int n) noexcept
    {
        const float coef = 1.0f - std::exp (-(float) n / (0.02f * sr));
        const auto previous = v.smooth;
        for (size_t i = 0; i < v.smooth.size(); ++i)
            v.smooth[i] += (target[i] - v.smooth[i]) * coef;

        const float punchDepth = punchSemis (v.smooth[punch]);
        const float punchCoef = std::exp (-1.0f / (punchMs (v.smooth[punchDecay]) * 0.001f * sr));
        const auto toneCoeffs = ZdfSvf::Coeffs::make (tptGain (toneHz (v.smooth[tone]), sr), juce::MathConstants<float>::sqrt2);
        const auto lowCutCoeffs = ZdfSvf::Coeffs::make (tptGain (lowCutHz (v.smooth[lowCut]), sr), juce::MathConstants<float>::sqrt2);
        const bool driven = v.smooth[drive] > 1.0e-4f || target[drive] > 0.0f;

        for (int j = 0; j < n; ++j)
        {
            const float frac = (float) (j + 1) / (float) n;
            auto lerp = [&] (Slot s) { return previous[s] + (v.smooth[s] - previous[s]) * frac; };

            const float pitch = (v.pitch.isGliding() ? v.pitch.next() : v.pitch.getCurrent()) + tune + v.punch * punchDepth;
            v.punch *= punchCoef;
            const float dt = juce::jmin (0.45f, 440.0f * fastExp2 ((pitch - 69.0f) / 12.0f) / sr);
            v.t += dt;
            v.t -= v.t >= 1.0f ? 1.0f : 0.0f;
            v.tSub += 0.5f * dt;
            v.tSub -= v.tSub >= 1.0f ? 1.0f : 0.0f;

            float x = morph (v.t, dt, lerp (shape));
            const float subLevel = lerp (subOctave);
            x = (x + subLevel * std::sin (juce::MathConstants<float>::twoPi * v.tSub)) / std::sqrt (1.0f + subLevel * subLevel);

            const float tr = lerp (translate);
            if (tr > 1.0e-4f)
            {
                // sin(h theta) by the Chebyshev recurrence; a harmonic fades out before it reaches 20 kHz
                const float theta = juce::MathConstants<float>::twoPi * v.t, c = std::cos (theta);
                float before = 0.0f, current = std::sin (theta), added = 0.0f, power = 0.0f;
                for (int h = 2; h <= 5; ++h)
                {
                    const float next = 2.0f * c * current - before;
                    before = current;
                    current = next;
                    const float a = tr * translateWeight[h - 2] * juce::jlimit (0.0f, 1.0f, (20000.0f - (float) h * dt * sr) / 2000.0f);
                    added += a * current;
                    power += a * a;
                }
                x = (x + added) / std::sqrt (1.0f + power);
            }

            if (driven)
            {
                // Bias before the curve makes it asymmetric (even harmonics); the DC it leaves is blocked below
                const float d = lerp (drive), bias = 0.5f * lerp (even);
                const float pre = 1.0f + 15.0f * d * d, post = 1.0f / std::pow (pre, 0.6f), wet = juce::jmin (1.0f, d * 20.0f);
                const float sat = v.saturator.process (pre * x + bias) - std::tanh (bias);
                x += wet * (post * sat - x);
            }
            x = v.toneFilter.process (x, toneCoeffs).low;
            x = v.lowCutFilter.process (x, lowCutCoeffs).high * voiceLevel;

            outL[j] += x;
            outR[j] += x;
        }
    }

    // Harmonics 2-5 fall off like a soft saw, so they read as one tone with the fundamental
    static constexpr float translateWeight[4] = { 0.6f, 0.4f, 0.25f, 0.15f };

    float sr = 48000.0f, tune = 0.0f;
    std::array<float, numUsedSlots> target = defaults();
    std::vector<Voice> voices;
};
}
