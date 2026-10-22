#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace ps
{
// Zero-delay-feedback state-variable filter (Zavalishin's TPT form). Stable under fast cutoff changes,
// so cutoffs can move while audio runs. One instance processes one channel.
class SvfFilter
{
public:
    void setCutoff (double cutoffHz, double sampleRate, double q = juce::MathConstants<double>::sqrt2 * 0.5) noexcept
    {
        const double fc = juce::jlimit (1.0, sampleRate * 0.49, cutoffHz);
        const double g = std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0; }

    struct Outputs { double low, band, high; };

    Outputs process (double x) noexcept
    {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }

private:
    double k = juce::MathConstants<double>::sqrt2, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;
};

// 4th-order Linkwitz-Riley low-pass or high-pass: two cascaded Butterworth stages. An LR4 low-pass and
// high-pass at the same frequency sum flat and in phase, which is what lets stacked role bands add up cleanly.
class LinkwitzRiley4
{
public:
    enum class Type { lowPass, highPass };

    explicit LinkwitzRiley4 (Type t = Type::lowPass) : type (t) {}

    void setCutoff (double cutoffHz, double sampleRate) noexcept
    {
        first.setCutoff (cutoffHz, sampleRate);
        second.setCutoff (cutoffHz, sampleRate);
    }

    void reset() noexcept { first.reset(); second.reset(); }

    double process (double x) noexcept
    {
        const auto a = first.process (x);
        const auto b = second.process (type == Type::lowPass ? a.low : a.high);
        return type == Type::lowPass ? b.low : b.high;
    }

private:
    Type type;
    SvfFilter first, second;
};

// ---- Per-voice filters (float, coefficients may change every sample) ---------------------------------

// TPT integrator gain for a cutoff: g = tan(pi fc / fs), with fc kept below Nyquist
inline float tptGain (float cutoffHz, float sampleRate) noexcept
{
    const float fc = juce::jlimit (5.0f, sampleRate * 0.49f, cutoffHz);
    return std::tan (juce::MathConstants<float>::pi * fc / sampleRate);
}

// Smooth, odd soft clipper close to tanh for |x| < 3 and flat beyond (a rational Pade fit)
inline float softClip (float x) noexcept
{
    x = juce::jlimit (-3.0f, 3.0f, x);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// 2-pole state-variable filter with low, band and high outputs. k = 1/Q.
struct ZdfSvf
{
    struct Outputs { float low, band, high; };

    Outputs process (float x, float g, float k) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float v3 = x - s2;
        const float v1 = a1 * (s1 + g * v3);
        const float v2 = s2 + g * v1;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return { v2, v1, x - k * v1 - v2 };
    }

    void reset() noexcept { s1 = s2 = 0.0f; }
    float s1 = 0.0f, s2 = 0.0f;
};

// 4-pole low-pass ladder (four one-pole TPT stages in a feedback loop, the OTA cascade behind most 24 dB
// VA filters). The feedback loop is solved without delay, then the stage input is soft-clipped, so high
// resonance saturates instead of running away. k = 0..4 (4 = self-oscillation).
struct ZdfLadder
{
    float process (float x, float g, float k) noexcept
    {
        const float G = g / (1.0f + g), beta = 1.0f / (1.0f + g);
        const float S = beta * (G * (G * (G * s[0] + s[1]) + s[2]) + s[3]);
        const float G4 = G * G * G * G;
        const float y4 = (G4 * x + S) / (1.0f + k * G4);
        float u = softClip (x - k * y4);
        for (auto& state : s)
        {
            const float v = (u - state) * G;
            const float y = v + state;
            state = y + v;
            u = y;
        }
        return u;
    }

    void reset() noexcept { s.fill (0.0f); }
    std::array<float, 4> s {};
};

// One-pole TPT high-pass, 6 dB/oct
struct ZdfHighPass1
{
    float process (float x, float g) noexcept
    {
        const float v = (x - s) * (g / (1.0f + g));
        const float low = v + s;
        s = low + v;
        return x - low;
    }

    void reset() noexcept { s = 0.0f; }
    float s = 0.0f;
};
}
