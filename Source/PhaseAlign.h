#pragma once
#include "Filter.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

// Sub/body phase alignment (brief: "automatic polarity and sample-offset correction at the crossover").
//
// The Sub and Body layers overlap around the low crossover, and two engines rarely put their fundamentals
// in phase there: a saw body sits 45-70 degrees off a sine sub, which loses up to 3 dB of the punch band and
// moves with the note. The correction is a polarity and a delay of the Sub layer (0 to half a period of the
// sub note, which with the polarity covers every phase), found by cross-correlating the two layers in the
// crossover band.
//
// It never moves while the sub sounds: each sub note that starts from silence is measured once (40 ms in,
// over 85 ms), the result is stored for that note, and the next time the note starts from silence its
// correction is applied at once, which is click-free because the delay line is silent then. A body without
// a stable phase relation (detuned unison, noise) correlates weakly and gets no correction. Any change to
// the two layers' sound clears what was learned.
namespace ps::align
{
// The crossover band of one layer, mono, band-passed (two SVF band-passes at the crossover) and decimated
// to about 6 kHz into a ring the stack reads.
class Probe
{
public:
    static constexpr int ringSize = 1024;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        decimation = juce::jmax (1, juce::roundToInt (sampleRate / 6000.0));
        reset();
    }

    void reset()
    {
        for (auto& f : bands)
            f.reset();
        ring.fill (0.0f);
    }

    void setCrossover (double hz)
    {
        if (std::abs (hz - crossoverHz) < 0.01)
            return;
        crossoverHz = hz;
        for (auto& f : bands)
            f.setCutoff (hz, sr, 0.7);
    }

    // clock: the stack's running sample count at the first sample, shared by every layer, so the rings of
    // two layers line up sample for sample
    void process (const float* left, const float* right, int numSamples, int64_t clock) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            double x = 0.5 * (left[i] + right[i]);
            for (auto& f : bands)
                x = f.process (x).band;
            const int64_t t = clock + i;
            if (t % decimation == 0)
                ring[(size_t) ((t / decimation) & (ringSize - 1))] = (float) x;
        }
    }

    float at (int64_t decimatedTime) const noexcept { return ring[(size_t) (decimatedTime & (ringSize - 1))]; }
    int getDecimation() const noexcept { return decimation; }

private:
    double sr = 48000.0, crossoverHz = -1.0;
    int decimation = 8;
    std::array<SvfFilter, 2> bands;
    std::array<float, ringSize> ring {};
};

struct Correction
{
    bool valid = false;
    float sign = 1.0f, delaySamples = 0.0f;   // at the full sample rate
};

// The best Sub lag (0..maxLag decimated samples, sub-sample by parabolic interpolation) over the decimated
// window [end - window, end), and the normalised correlation there (its sign is the polarity)
struct Peak { double r = 0.0, lag = 0.0; };

inline Peak findPeak (const Probe& sub, const Probe& body, int64_t end, int window, int maxLag)
{
    const int64_t start = end - window;
    double eb = 0.0;
    for (int64_t t = start; t < end; ++t)
        eb += (double) body.at (t) * body.at (t);
    if (eb < 1.0e-12)
        return {};

    std::array<double, Probe::ringSize> r {};   // r[k] at lag k - 1, so the edges have neighbours
    int bestK = -1;
    for (int k = 0; k <= maxLag + 2; ++k)
    {
        const int lag = k - 1;
        double sb = 0.0, es = 0.0;
        for (int64_t t = start; t < end; ++t)
        {
            const double s = sub.at (t - lag);
            sb += s * body.at (t);
            es += s * s;
        }
        r[(size_t) k] = es > 0.0 ? sb / std::sqrt (es * eb) : 0.0;
        if (lag >= 0 && lag <= maxLag && (bestK < 0 || std::abs (r[(size_t) k]) > std::abs (r[(size_t) bestK])))
            bestK = k;
    }
    const double best = r[(size_t) bestK], sign = best < 0.0 ? -1.0 : 1.0;
    const double a = sign * r[(size_t) bestK - 1], b = sign * best, c = sign * r[(size_t) bestK + 1];
    const double denom = a - 2.0 * b + c;
    const double offset = std::abs (denom) > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denom) : 0.0;
    return { best, juce::jmax (0.0, (double) (bestK - 1) + offset) };
}

// Finds the Sub delay (0..half a period of the sub note, or of the crossover if that is lower) and polarity
// that best line it up with the Body over the decimated window ending at `end`. The relation must be stable:
// the two halves of the window must agree on polarity and lag (within 0.1 of a period) with a correlation of
// at least 0.25 each, so a body that drifts against the sub (detuned unison, noise) gets no correction.
inline Correction solve (const Probe& sub, const Probe& body, int64_t end, int window, double crossoverHz, double subHz, double sampleRate)
{
    constexpr double minCorrelation = 0.25, maxDisagreement = 0.1;
    const int decimation = sub.getDecimation();
    const double halfPeriod = 0.5 * sampleRate / juce::jmin (crossoverHz, juce::jmax (20.0, subHz)) / decimation;
    const int maxLag = juce::jlimit (1, Probe::ringSize - window - 4, (int) std::ceil (halfPeriod));
    const auto first = findPeak (sub, body, end - window / 2, window / 2, maxLag);
    const auto second = findPeak (sub, body, end, window / 2, maxLag);
    const auto whole = findPeak (sub, body, end, window, maxLag);
    const bool stable = std::abs (first.r) >= minCorrelation && std::abs (second.r) >= minCorrelation
                     && (first.r < 0.0) == (second.r < 0.0) && (whole.r < 0.0) == (first.r < 0.0)
                     && std::abs (first.lag - second.lag) <= maxDisagreement * 2.0 * halfPeriod;
    if (! stable)
        return {};
    return { true, whole.r < 0.0 ? -1.0f : 1.0f, (float) (whole.lag * decimation) };
}

// The Sub layer's correction: a stereo fractional delay (cubic Hermite) with a polarity. It is switched
// only while silent, so it never needs to crossfade.
class Delay
{
public:
    void prepare (double sampleRate)
    {
        int size = 1;
        while (size < (int) (0.026 * sampleRate) + 8)   // half a period at 20 Hz, the lowest sub note
            size <<= 1;
        for (auto& l : lines)
            l.assign ((size_t) size, 0.0f);
        mask = size - 1;
        maxDelay = (float) (size - 8);
        reset();
    }

    void reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        writePos = 0;
        sign = 1.0f;
        delay = 0.0f;
    }

    // Only while the layer is silent: the line is cleared so nothing old plays through the new tap
    void set (float newSign, float newDelay) noexcept
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        sign = newSign;
        delay = juce::jlimit (0.0f, maxDelay, newDelay);
    }

    bool isNeutral() const noexcept { return sign > 0.0f && delay <= 0.0f; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (isNeutral())
            return;
        float* io[2] = { left, right };
        for (int n = 0; n < numSamples; ++n)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& l = lines[(size_t) ch];
                l[(size_t) writePos] = io[ch][n];
                const float pos = (float) writePos - delay;
                const int i1 = (int) std::floor (pos);
                const float f = pos - (float) i1;
                const float y1 = l[(size_t) (i1 & mask)], y2 = l[(size_t) ((i1 + 1) & mask)];
                if (delay < 2.0f)
                {
                    // The Hermite read would need samples not written yet; linear is exact enough this low
                    io[ch][n] = sign * (y1 + f * (y2 - y1));
                    continue;
                }
                const float y0 = l[(size_t) ((i1 - 1) & mask)], y3 = l[(size_t) ((i1 + 2) & mask)];
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                io[ch][n] = sign * (((c3 * f + c2) * f + c1) * f + y1);
            }
            writePos = (writePos + 1) & mask;
        }
    }

private:
    std::array<std::vector<float>, 2> lines;
    int mask = 0, writePos = 0;
    float sign = 1.0f, delay = 0.0f, maxDelay = 0.0f;
};
}
