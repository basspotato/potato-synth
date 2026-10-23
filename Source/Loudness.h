#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// Loudness and true-peak measurement after ITU-R BS.1770-4 / EBU R128. Used offline (tests, preset
// checks) and later by the Mix Health meters. Not real-time: the integrated meter keeps a block history.
namespace ps::loudness
{
// Direct form I biquad in double precision
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    double process (double x) noexcept
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// The K-weighting pre-filter (high shelf + high-pass), derived for any sample rate
inline std::array<Biquad, 2> kWeighting (double sampleRate)
{
    const double pi = juce::MathConstants<double>::pi;
    std::array<Biquad, 2> f;
    {
        const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan (pi * f0 / sampleRate), vh = std::pow (10.0, gainDb / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416), a0 = 1.0 + k / q + k * k;
        f[0].b0 = (vh + vb * k / q + k * k) / a0;
        f[0].b1 = 2.0 * (k * k - vh) / a0;
        f[0].b2 = (vh - vb * k / q + k * k) / a0;
        f[0].a1 = 2.0 * (k * k - 1.0) / a0;
        f[0].a2 = (1.0 - k / q + k * k) / a0;
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan (pi * f0 / sampleRate), a0 = 1.0 + k / q + k * k;
        f[1].b0 = 1.0; f[1].b1 = -2.0; f[1].b2 = 1.0;
        f[1].a1 = 2.0 * (k * k - 1.0) / a0;
        f[1].a2 = (1.0 - k / q + k * k) / a0;
    }
    return f;
}

// Integrated loudness (LUFS) of a stereo signal with the absolute (-70 LUFS) and relative (-10 LU) gates.
// Returns -inf for silence.
inline double integratedLufs (const float* left, const float* right, size_t numSamples, double sampleRate)
{
    auto fl = kWeighting (sampleRate), fr = kWeighting (sampleRate);
    std::vector<double> power (numSamples);
    for (size_t i = 0; i < numSamples; ++i)
    {
        const double l = fl[1].process (fl[0].process (left[i]));
        const double r = fr[1].process (fr[0].process (right[i]));
        power[i] = l * l + r * r;
    }

    const size_t block = (size_t) std::lround (0.4 * sampleRate), hop = block / 4;
    std::vector<double> blocks;
    for (size_t start = 0; start + block <= numSamples; start += hop)
    {
        double sum = 0.0;
        for (size_t i = start; i < start + block; ++i)
            sum += power[i];
        blocks.push_back (sum / (double) block);
    }

    auto lufs = [] (double meanPower) { return -0.691 + 10.0 * std::log10 (meanPower); };
    auto gatedMean = [&] (double gateLufs)
    {
        double sum = 0.0;
        int n = 0;
        for (double b : blocks)
            if (b > 0.0 && lufs (b) > gateLufs) { sum += b; ++n; }
        return n > 0 ? sum / n : 0.0;
    };

    const double absolute = gatedMean (-70.0);
    if (absolute <= 0.0)
        return -std::numeric_limits<double>::infinity();
    const double relative = gatedMean (lufs (absolute) - 10.0);
    return relative > 0.0 ? lufs (relative) : -std::numeric_limits<double>::infinity();
}

// The loudest short-term loudness (LUFS, 3 s window, 100 ms hop) of a stereo signal: how loud it sounds at its
// loudest, which matches a one-shot hit and a held chord fairly. A signal under 3 s is one window. -inf for silence.
inline double maxShortTermLufs (const float* left, const float* right, size_t numSamples, double sampleRate)
{
    auto fl = kWeighting (sampleRate), fr = kWeighting (sampleRate);
    std::vector<double> sum (numSamples + 1, 0.0);
    for (size_t i = 0; i < numSamples; ++i)
    {
        const double l = fl[1].process (fl[0].process (left[i]));
        const double r = fr[1].process (fr[0].process (right[i]));
        sum[i + 1] = sum[i] + l * l + r * r;
    }
    const size_t window = std::min (numSamples, (size_t) std::lround (3.0 * sampleRate));
    const size_t hop = std::max ((size_t) 1, (size_t) std::lround (0.1 * sampleRate));
    double best = 0.0;
    for (size_t start = 0; window > 0 && start + window <= numSamples; start += hop)
        best = std::max (best, (sum[start + window] - sum[start]) / (double) window);
    return best > 0.0 ? -0.691 + 10.0 * std::log10 (best) : -std::numeric_limits<double>::infinity();
}

// 4x interpolation kernel for true-peak estimation: 48-tap Blackman-windowed sinc, 12 taps per phase.
// Phase p of input history x[n], x[n-1], ... is sum_k x[n-k] * kernel[k * 4 + p]; it estimates the
// signal at about n - 5.875 + p / 4.
namespace interpolation
{
    constexpr int factor = 4, tapsPerPhase = 12, taps = factor * tapsPerPhase;

    inline const std::array<double, taps>& kernel()
    {
        static const auto table = []
        {
        std::array<double, taps> h {};
        const double centre = (taps - 1) * 0.5, pi = juce::MathConstants<double>::pi;
        for (int n = 0; n < taps; ++n)
        {
            const double t = (n - centre) / factor;
            const double sinc = std::abs (t) < 1.0e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
            const double w = 0.42 - 0.5 * std::cos (2.0 * pi * n / (taps - 1)) + 0.08 * std::cos (4.0 * pi * n / (taps - 1));
            h[(size_t) n] = sinc * w;
        }
        return h;
        }();
        return table;
    }
}

// True peak (dBTP): the signal is upsampled 4x, as BS.1770-4 suggests.
inline double truePeakDb (const float* samples, size_t numSamples)
{
    using namespace interpolation;
    const auto& kernel = interpolation::kernel();

    double peak = 0.0;
    for (size_t i = 0; i < numSamples; ++i)
    {
        peak = std::max (peak, (double) std::abs (samples[i]));
        // Only where the interpolator has full support: zero-padding a cut-off excerpt would create
        // overshoot that isn't in the signal
        if (i + tapsPerPhase / 2 >= numSamples || i + 1 < (size_t) tapsPerPhase / 2)
            continue;
        for (int phase = 1; phase < factor; ++phase)
        {
            double y = 0.0;
            for (int k = 0; k < tapsPerPhase; ++k)
                y += samples[i - (size_t) k + tapsPerPhase / 2] * kernel[(size_t) (k * factor + phase)];
            peak = std::max (peak, std::abs (y));
        }
    }
    return 20.0 * std::log10 (std::max (peak, 1.0e-12));
}
}
