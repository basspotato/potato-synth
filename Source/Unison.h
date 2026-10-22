#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

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
}
