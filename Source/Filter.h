#pragma once
#include <juce_core/juce_core.h>
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
}
