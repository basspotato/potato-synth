#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include "Simd.h"

namespace ps
{
// Unison layouts: per unison voice a frequency ratio, a gain and a pan position.
//
// JP: the JP-8000/8080 Super Saw as reverse-engineered from its DSP (39C3, 2025). Seven oscillators with
// the asymmetric integer detune table {0, +128, -128, +816, -824, +1408, -1440}, scaled by one detune
// amount; oscillator 0 at full level and the six others scaled linearly by Mix. The detune knob follows
// Szabo's measured curve (flat through the middle, steep at the top), so the maximum is the hardware's
// +-11 % spread.
//
// Modern: 8, 12 or 16 voices spread symmetrically with a power curve that packs voices toward the centre
// (denser, smoother beating), the two middle voices at full level and the rest at Mix.
//
// Both normalise the summed power, so Mix and voice count change the width, not the loudness.
struct UnisonLayout
{
    static constexpr int maxVoices = 16;
    enum class Mode { off, jp, modern8, modern12, modern16 };

    int count = 1;
    std::array<float, maxVoices> ratio {}, gain {}, pan {};

    // detune and mix are 0..1 knob positions, spread is 0..1 stereo width
    void set (Mode mode, float detune, float mix, float spread) noexcept
    {
        ratio.fill (1.0f);
        gain.fill (0.0f);
        pan.fill (0.0f);
        const float amount = 0.11002313f * detuneCurve (juce::jlimit (0.0f, 1.0f, detune));
        mix = juce::jlimit (0.0f, 1.0f, mix);
        spread = juce::jlimit (0.0f, 1.0f, spread);

        if (mode == Mode::off)
        {
            count = 1;
            gain[0] = 1.0f;
            return;
        }

        if (mode == Mode::jp)
        {
            static constexpr float table[] = { 0.0f, 128.0f, -128.0f, 816.0f, -824.0f, 1408.0f, -1440.0f };
            count = 7;
            for (int i = 0; i < count; ++i)
            {
                const float offset = table[i] / 1440.0f;
                ratio[(size_t) i] = 1.0f + amount * offset;
                gain[(size_t) i] = i == 0 ? 1.0f : mix;
                // Pairs alternate sides so sharp and flat voices are not split left/right
                const float side = ((i + 1) / 2) % 2 == 1 ? 1.0f : -1.0f;
                pan[(size_t) i] = i == 0 ? 0.0f : spread * std::abs (offset) * side * (offset > 0.0f ? 1.0f : -1.0f);
            }
        }
        else
        {
            count = mode == Mode::modern8 ? 8 : mode == Mode::modern12 ? 12 : 16;
            for (int i = 0; i < count; ++i)
            {
                const float pos = (float) i / (float) (count - 1) * 2.0f - 1.0f;   // -1..1, none exactly 0
                const float shaped = std::copysign (std::pow (std::abs (pos), 1.5f), pos);
                ratio[(size_t) i] = 1.0f + amount * shaped;
                const bool middle = i == count / 2 - 1 || i == count / 2;
                gain[(size_t) i] = middle ? 1.0f : mix;
                pan[(size_t) i] = spread * std::abs (pos) * (i % 2 == 0 ? -1.0f : 1.0f);
            }
        }

        float power = 0.0f;
        for (int i = 0; i < count; ++i)
            power += gain[(size_t) i] * gain[(size_t) i];
        const float norm = 1.0f / std::sqrt (power);
        for (int i = 0; i < count; ++i)
            gain[(size_t) i] *= norm;
    }

    // Szabo (2010): measured JP-8000 detune knob response, 0..1 in, 0..1 out
    static float detuneCurve (float x) noexcept
    {
        static constexpr double c[] = { 10028.7312891634, -50818.8652045924, 111363.4808729368, -138150.6761080548,
                                        106649.6679158292, -53046.9642751875, 17019.9518580080, -3425.0836591318,
                                        404.2703938388, -24.1878824391, 0.6717417634, 0.0030115596 };
        double y = 0.0;
        for (double k : c)
            y = y * x + k;
        return (float) juce::jlimit (0.0, 1.0, y);
    }
};

// ---- Band-limited unison oscillator bank ----------------------------------------------------------------
//
// Up to 16 oscillators at once, 8 per AVX2 vector (one at a time without AVX2). Each sample, every lane
// advances its phase, makes a PolyBLEP saw or pulse or PolyBLAMP triangle, ramps its gain and pans into
// the stereo sum. Branch-free, so all lanes run the same instructions.
namespace blep
{
    // Two-sample polynomial correction for a downward step of 2 at phase 0 (subtract it from a naive saw)
    template <typename T>
    inline T step (T t, T dt, T invDt)
    {
        using namespace simd;
        const T a = t * invDt, b = (t - T (1.0f)) * invDt;
        const T after = a + a - a * a - T (1.0f);
        const T before = b * b + b + b + T (1.0f);
        return select (t < dt, after, select (t > T (1.0f) - dt, before, T (0.0f)));
    }

    // Integrated step: correction for a slope change at phase 0
    template <typename T>
    inline T ramp (T t, T dt, T invDt)
    {
        using namespace simd;
        const T a = t * invDt - T (1.0f), b = (t - T (1.0f)) * invDt + T (1.0f);
        const T third (1.0f / 3.0f);
        return select (t < dt, T (0.0f) - a * a * a * third, select (t > T (1.0f) - dt, b * b * b * third, T (0.0f)));
    }

    template <typename T>
    inline T wrap (T t) { using namespace simd; return select (t < T (0.0f), t + T (1.0f), t); }

    enum Wave { saw, pulse, triangle };

    // Band-limited wave at phase t with increment dt; pw is the pulse width
    template <int W, typename T>
    inline T wave (T t, T dt, T pw)
    {
        using namespace simd;
        const T invDt = reciprocal (dt);
        if constexpr (W == pulse)
            return select (t < pw, T (1.0f), T (-1.0f)) + step (t, dt, invDt) - step (wrap (t - pw), dt, invDt);
        else if constexpr (W == triangle)
            return T (4.0f) * simd::abs (t - T (0.5f)) - T (1.0f)
                 + T (8.0f) * dt * (ramp (wrap (t - T (0.5f)), dt, invDt) - ramp (t, dt, invDt));
        else
            return T (2.0f) * t - T (1.0f) - step (t, dt, invDt);
    }
}

struct UnisonBank
{
    static constexpr int maxBlock = 32;   // samples per render call

    alignas (32) std::array<float, UnisonLayout::maxVoices> phase {}, gain {}, ratio {}, panL {}, panR {};

    // Adds `numSamples` of the bank into outL/outR. increments[i] is the centre increment (cycles per sample)
    // at sample i; each lane's gain ramps linearly to targetGain over the call.
    template <int W>
    void render (const float* increments, int lanes, const float* targetGain, float pw,
                 float* outL, float* outR, int numSamples) noexcept
    {
        using simd::F;
        for (int k = 0; k < lanes; k += F::width)
        {
            F t = F::load (&phase[(size_t) k]), g = F::load (&gain[(size_t) k]);
            const F r = F::load (&ratio[(size_t) k]), pl = F::load (&panL[(size_t) k]), pr = F::load (&panR[(size_t) k]);
            const F gStep = (F::load (&targetGain[(size_t) k]) - g) * F (1.0f / (float) numSamples);
            std::array<F, maxBlock> left, right;
            for (int i = 0; i < numSamples; ++i)
            {
                const F dt = simd::min (r * F (std::max (increments[i], 1.0e-6f)), F (0.45f));
                t = t + dt;
                t = t - simd::select (t >= F (1.0f), F (1.0f), F (0.0f));
                g = g + gStep;
                const F s = blep::wave<W> (t, dt, F (pw)) * g;
                left[(size_t) i] = s * pl;
                right[(size_t) i] = s * pr;
            }
            // Lanes summed into the outputs, eight samples at a time
            int i = 0;
            for (; i + 8 <= numSamples; i += 8)
            {
                simd::addSums8 (&left[(size_t) i], outL + i);
                simd::addSums8 (&right[(size_t) i], outR + i);
            }
            for (; i < numSamples; ++i)
            {
                outL[i] += simd::sum (left[(size_t) i]);
                outR[i] += simd::sum (right[(size_t) i]);
            }
            t.store (&phase[(size_t) k]);
            F::load (&targetGain[(size_t) k]).store (&gain[(size_t) k]);
        }
    }
};
}
