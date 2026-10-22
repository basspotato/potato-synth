#pragma once
#include <juce_core/juce_core.h>
#include "Simd.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

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

// 2^x to about 3e-6 relative (0.005 cents as a pitch): exponent bits plus a degree-5 Taylor series on
// [-0.5, 0.5]. For per-sample cutoffs and pitches, where std::exp2 dominates the cost.
inline float fastExp2 (float x) noexcept
{
    x = juce::jlimit (-126.0f, 126.0f, x);
    const float whole = std::floor (x + 0.5f), f = x - whole;
    const float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    const int32_t bits = ((int32_t) whole + 127) << 23;
    float scale;
    std::memcpy (&scale, &bits, sizeof (scale));
    return p * scale;
}

// TPT integrator gain for a cutoff: g = tan(pi fc / fs), with fc kept below Nyquist. tan from its [5/4] Pade
// approximant: under 3e-4 relative error up to 0.49 fs (half a cent of cutoff), several times cheaper.
inline float tptGain (float cutoffHz, float sampleRate) noexcept
{
    const float fc = juce::jlimit (5.0f, sampleRate * 0.49f, cutoffHz);
    const float x = juce::MathConstants<float>::pi * fc / sampleRate, x2 = x * x;
    return x * (945.0f - x2 * (105.0f - x2)) / (945.0f - x2 * (420.0f - 15.0f * x2));
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
    struct Coeffs
    {
        float g, k, a1;
        static Coeffs make (float g, float k) noexcept { return { g, k, 1.0f / (1.0f + g * (g + k)) }; }
    };

    Outputs process (float x, float g, float k) noexcept { return process (x, Coeffs::make (g, k)); }

    Outputs process (float x, const Coeffs& c) noexcept
    {
        const float g = c.g, k = c.k, a1 = c.a1;
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
    struct Coeffs
    {
        float G, beta, G4, k, norm;
        static Coeffs make (float g, float k) noexcept
        {
            const float beta = 1.0f / (1.0f + g), G = g * beta, G4 = G * G * G * G;
            return { G, beta, G4, k, 1.0f / (1.0f + k * G4) };
        }
    };

    float process (float x, float g, float k) noexcept { return process (x, Coeffs::make (g, k)); }

    float process (float x, const Coeffs& c) noexcept
    {
        const float G = c.G, k = c.k;
        const float S = c.beta * (G * (G * (G * s[0] + s[1]) + s[2]) + s[3]);
        const float y4 = (c.G4 * x + S) * c.norm;
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

// 2:1 decimator: a 63-tap Kaiser-windowed half-band low-pass, flat to 20 kHz and about 80 dB down from
// 28 kHz at a 96 kHz input. Half-band: every other tap is zero, so it splits into a 32-tap dot product over
// the odd input samples plus the centre tap on the even ones (both contiguous, so it vectorises).
// Delay: 15.5 output samples.
class HalfbandDecimator
{
public:
    static constexpr int sideTaps = 16, oddTaps = 2 * sideTaps;

    // a then b: two consecutive input samples; returns one output sample
    float process (float a, float b) noexcept
    {
        odd[(size_t) oddWrite] = odd[(size_t) (oddWrite + oddTaps)] = b;
        oddWrite = oddWrite + 1 == oddTaps ? 0 : oddWrite + 1;
        const float centre = even[(size_t) evenPos];
        even[(size_t) evenPos] = a;
        evenPos = evenPos + 1 == sideTaps - 1 ? 0 : evenPos + 1;

        // odd[oddWrite ..] holds the last 32 odd samples, oldest first
        using simd::F;
        const float* x = &odd[(size_t) oddWrite];
        const auto& c = weights();
        F acc (0.0f);
        for (int i = 0; i < oddTaps; i += F::width)
            acc = acc + F::load (x + i) * F::load (&c[(size_t) i]);
        return 0.5f * centre + simd::sum (acc);
    }

    void reset() noexcept { odd.fill (0.0f); even.fill (0.0f); oddWrite = evenPos = 0; }

private:
    // Taps on the odd samples, oldest first: c15 .. c0, c0 .. c15
    static const std::array<float, oddTaps>& weights() noexcept
    {
        static const auto table = []
        {
            std::array<double, sideTaps> c {};
            auto bessel0 = [] (double x)
            {
                double sum = 1.0, term = 1.0;
                for (int k = 1; k < 30; ++k)
                {
                    term *= (x * 0.5 / k) * (x * 0.5 / k);
                    sum += term;
                }
                return sum;
            };
            constexpr double beta = 8.0;
            double side = 0.0;
            for (int i = 0; i < sideTaps; ++i)
            {
                const double m = 2 * i + 1, r = m / (2 * sideTaps);
                const double sinc = std::sin (juce::MathConstants<double>::halfPi * m) / (juce::MathConstants<double>::halfPi * m);
                c[(size_t) i] = 0.5 * sinc * bessel0 (beta * std::sqrt (1.0 - r * r)) / bessel0 (beta);
                side += 2.0 * c[(size_t) i];
            }
            std::array<float, oddTaps> w {};
            for (int i = 0; i < sideTaps; ++i)   // unity gain at DC with the centre tap at exactly 0.5
            {
                const auto k = (float) (c[(size_t) i] * 0.5 / side);
                w[(size_t) (sideTaps - 1 - i)] = k;
                w[(size_t) (sideTaps + i)] = k;
            }
            return w;
        }();
        return table;
    }

    std::array<float, 2 * oddTaps> odd {};
    std::array<float, sideTaps - 1> even {};   // the centre tap lands 15 even samples back
    int oddWrite = 0, evenPos = 0;
};
}
