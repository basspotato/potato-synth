#pragma once
#include "Loudness.h"
#include "Filter.h"
#include <atomic>
#include <array>
#include <cmath>

// Mix Health: what the patch does to a mix, measured on the plugin's final output, plus the listen modes.
//   True peak (dBTP, 4x interpolated as BS.1770-4), loudness (momentary 400 ms, short-term 3 s, integrated with
//   the EBU R128 gates), L/R correlation (300 ms), and how much side signal reaches the lows (below the low
//   crossover, which the mono-bass stage should keep near zero). Masking: bands (200 Hz..3.2 kHz, the carve
//   bank's) where two or more layers are within 6 dB of each other, so neither owns the band.
// It runs only while someone is looking (the editor turns it on), so a closed editor costs nothing.
// Readings are atomics written by the audio thread and read by the GUI.
namespace ps
{
class MixHealth
{
public:
    static constexpr int numMaskBands = 5;
    enum class Listen { normal, phone, mono };

    struct Readings
    {
        std::atomic<float> truePeakDb { -100.0f };       // the highest since the GUI last took it
        std::atomic<float> truePeakHoldDb { -100.0f };   // the highest since reset
        std::atomic<float> momentaryLufs { -100.0f }, shortTermLufs { -100.0f }, integratedLufs { -100.0f };
        std::atomic<float> correlation { 1.0f };
        std::atomic<float> sideLowsDb { -100.0f };       // side power under the low crossover, relative to the mid's
        std::array<std::atomic<uint32_t>, numMaskBands> maskedLayers {};   // per band: bit l set = layer l contends
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& k : kWeight)
            k = loudness::kWeighting (sampleRate);
        (void) loudness::interpolation::kernel();   // builds the table here, not on the audio thread
        corrCoef = 1.0 - std::exp (-1.0 / (0.3 * sampleRate));
        subBlockLength = (int) std::lround (0.1 * sampleRate);
        listenStep = 1.0f / (0.02f * (float) sampleRate);
        setCrossover (120.0);
        for (auto* bank : { &phoneLow, &phoneHigh })
            for (auto& f : *bank)
                f.reset();
        for (auto& f : phoneLow)
            f.setCutoff (400.0, sampleRate, 0.7071);
        for (auto& f : phoneHigh)
            f.setCutoff (6000.0, sampleRate, 0.7071);
        reset();
    }

    void setCrossover (double hz) noexcept
    {
        if (std::abs (hz - crossoverHz) < 0.01)
            return;
        crossoverHz = hz;
        for (auto& f : lows)
            f.setCutoff (hz, sr);
    }

    // Message thread
    void requestReset() noexcept { resetRequested = true; }
    void setActive (bool on) noexcept { activeFlag = on; }
    bool isActive() const noexcept { return activeFlag.load(); }
    void setListen (Listen l) noexcept { listenMode = (int) l; }
    Listen getListen() const noexcept { return (Listen) listenMode.load(); }
    const Readings& readings() const noexcept { return values; }
    // Takes the true peak since the last call (the meter's falling bar), leaving the hold alone
    float takeTruePeakDb() noexcept { return values.truePeakDb.exchange (-100.0f); }

    // Audio thread: the per-layer band energies (CarveBank) for the masking check, once per block
    template <typename Energies, typename Sounding>
    void updateMasking (const Energies& energy, const Sounding& sounding, int numLayers) noexcept
    {
        if (! activeFlag.load (std::memory_order_relaxed))
            return;
        float total = 0.0f;
        for (int l = 0; l < numLayers; ++l)
            if (sounding[(size_t) l])
                for (int b = 0; b < numMaskBands; ++b)
                    total = std::max (total, energy[(size_t) l][(size_t) b]);
        for (int b = 0; b < numMaskBands; ++b)
        {
            float loudest = 0.0f;
            for (int l = 0; l < numLayers; ++l)
                if (sounding[(size_t) l])
                    loudest = std::max (loudest, energy[(size_t) l][(size_t) b]);
            uint32_t bits = 0;
            int count = 0;
            // A band only counts when it carries real energy (within 30 dB of the patch's strongest band)
            if (loudest > 1.0e-10f && loudest > total * 1.0e-3f)
                for (int l = 0; l < numLayers; ++l)
                    if (sounding[(size_t) l] && energy[(size_t) l][(size_t) b] >= 0.25f * loudest)
                    {
                        bits |= 1u << l;
                        ++count;
                    }
            values.maskedLayers[(size_t) b].store (count >= 2 ? bits : 0u, std::memory_order_relaxed);
        }
    }

    // Audio thread: measures the final output, then applies the listen mode (monitoring only, after the meters)
    void process (float* left, float* right, int numSamples) noexcept
    {
        if (activeFlag.load (std::memory_order_relaxed))
            measure (left, right, numSamples);
        else if (wasActive)
            reset();
        wasActive = activeFlag.load (std::memory_order_relaxed);
        applyListen (left, right, numSamples);
    }

private:
    void reset() noexcept
    {
        for (auto& ch : kWeight)
            for (auto& b : ch)
                b.x1 = b.x2 = b.y1 = b.y2 = 0.0;
        for (auto& h : history)
            h.fill (0.0f);
        historyPos = 0;
        for (auto& f : lows)
            f.reset();
        lr = ll = rr = 0.0;
        midLow = sideLow = 0.0;
        subBlocks.fill (0.0);
        subBlockCount = 0;
        subBlockSum = 0.0;
        subBlockFill = 0;
        histogramCount.fill (0);
        histogramPower.fill (0.0);
        values.truePeakDb = -100.0f;
        values.truePeakHoldDb = -100.0f;
        values.momentaryLufs = values.shortTermLufs = values.integratedLufs = -100.0f;
        values.correlation = 1.0f;
        values.sideLowsDb = -100.0f;
        for (auto& m : values.maskedLayers)
            m = 0u;
    }

    void measure (const float* left, const float* right, int numSamples) noexcept
    {
        if (resetRequested.exchange (false))
            reset();
        const auto& kernel = loudness::interpolation::kernel();
        using loudness::interpolation::factor;
        using loudness::interpolation::tapsPerPhase;
        double peak = 0.0;
        const float* in[2] = { left, right };
        for (int i = 0; i < numSamples; ++i)
        {
            // True peak: the sample itself and three interpolated points before it
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& h = history[(size_t) ch];
                h[(size_t) historyPos] = in[ch][i];
                peak = std::max (peak, (double) std::abs (in[ch][i]));
                for (int phase = 1; phase < factor; ++phase)
                {
                    double y = 0.0;
                    for (int k = 0; k < tapsPerPhase; ++k)
                        y += h[(size_t) ((historyPos - k + tapsPerPhase) % tapsPerPhase)] * kernel[(size_t) (k * factor + phase)];
                    peak = std::max (peak, std::abs (y));
                }
            }
            historyPos = (historyPos + 1) % tapsPerPhase;

            const double l = in[0][i], r = in[1][i];
            // Loudness: K-weighted power in 100 ms sub-blocks
            const double kl = kWeight[0][1].process (kWeight[0][0].process (l));
            const double kr = kWeight[1][1].process (kWeight[1][0].process (r));
            subBlockSum += kl * kl + kr * kr;
            if (++subBlockFill >= subBlockLength)
                closeSubBlock();

            lr += (l * r - lr) * corrCoef;
            ll += (l * l - ll) * corrCoef;
            rr += (r * r - rr) * corrCoef;
            const double lowL = lows[0].process (l), lowR = lows[1].process (r);
            midLow += (juce::square (0.5 * (lowL + lowR)) - midLow) * corrCoef;
            sideLow += (juce::square (0.5 * (lowL - lowR)) - sideLow) * corrCoef;
        }
        const float peakDb = (float) (20.0 * std::log10 (std::max (peak, 1.0e-5)));
        if (peakDb > values.truePeakDb.load (std::memory_order_relaxed))
            values.truePeakDb.store (peakDb, std::memory_order_relaxed);
        if (peakDb > values.truePeakHoldDb.load (std::memory_order_relaxed))
            values.truePeakHoldDb.store (peakDb, std::memory_order_relaxed);
        const double norm = std::sqrt (ll * rr);
        values.correlation.store (norm > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, lr / norm) : 1.0f, std::memory_order_relaxed);
        values.sideLowsDb.store (midLow > 1.0e-12 ? (float) juce::jmax (-100.0, 10.0 * std::log10 (std::max (sideLow, 1.0e-20) / midLow)) : -100.0f,
                                 std::memory_order_relaxed);
    }

    static double lufs (double meanPower) noexcept { return meanPower > 0.0 ? -0.691 + 10.0 * std::log10 (meanPower) : -100.0; }

    void closeSubBlock() noexcept
    {
        subBlocks[(size_t) (subBlockCount % numSubBlocks)] = subBlockSum / (double) subBlockFill;
        ++subBlockCount;
        subBlockSum = 0.0;
        subBlockFill = 0;
        auto meanOf = [this] (int count)
        {
            const int n = (int) std::min<int64_t> (count, subBlockCount);
            double sum = 0.0;
            for (int k = 1; k <= n; ++k)
                sum += subBlocks[(size_t) ((subBlockCount - k) % numSubBlocks)];
            return n > 0 ? sum / (double) count : 0.0;
        };
        const double momentary = meanOf (4);
        values.momentaryLufs.store ((float) lufs (momentary), std::memory_order_relaxed);
        values.shortTermLufs.store ((float) lufs (meanOf (numSubBlocks)), std::memory_order_relaxed);

        // Integrated: every 400 ms block (75 % overlap, as R128) above -70 LUFS goes into a 0.1 LU histogram, so
        // the relative gate can be applied at any time without keeping the history
        if (subBlockCount < 4)
            return;
        const double m = lufs (momentary);
        if (m > -70.0)
        {
            const auto bin = (size_t) juce::jlimit (0, numBins - 1, (int) ((m + 70.0) * 10.0));
            ++histogramCount[bin];
            histogramPower[bin] += momentary;
        }
        auto gatedMean = [this] (double gate)
        {
            double sum = 0.0;
            int64_t n = 0;
            const int first = juce::jlimit (0, numBins, (int) std::ceil ((gate + 70.0) * 10.0));
            for (int b = first; b < numBins; ++b)
            {
                sum += histogramPower[(size_t) b];
                n += histogramCount[(size_t) b];
            }
            return n > 0 ? sum / (double) n : 0.0;
        };
        const double absolute = gatedMean (-70.0);
        values.integratedLufs.store (absolute > 0.0 ? (float) lufs (gatedMean (lufs (absolute) - 10.0)) : -100.0f, std::memory_order_relaxed);
    }

    // Phone: mono through a small speaker's band (24 dB/oct under 400 Hz, 12 dB/oct over 6 kHz). Mono club: the
    // mono sum. Modes crossfade over 20 ms.
    void applyListen (float* left, float* right, int numSamples) noexcept
    {
        const int want = listenMode.load (std::memory_order_relaxed);
        if (want == currentListen && listenFade >= 1.0f && want == (int) Listen::normal)
            return;
        for (int i = 0; i < numSamples; ++i)
        {
            if (want != currentListen)
            {
                listenFade -= listenStep;
                if (listenFade <= 0.0f)
                {
                    listenFade = 0.0f;
                    currentListen = want;
                    for (auto* bank : { &phoneLow, &phoneHigh })
                        for (auto& f : *bank)
                            f.reset();
                }
            }
            else
                listenFade = std::min (1.0f, listenFade + listenStep);
            const float l = left[i], r = right[i];
            float ml = l, mr = r;
            if (currentListen != (int) Listen::normal)
            {
                double m = 0.5 * ((double) l + r);
                if (currentListen == (int) Listen::phone)
                {
                    m = phoneLow[1].process (phoneLow[0].process (m).high).high;
                    m = phoneHigh[0].process (m).low;
                }
                ml = mr = (float) m;
            }
            const float g = listenFade * listenFade * (3.0f - 2.0f * listenFade);
            if (currentListen == (int) Listen::normal)
            {
                // Fading back from a listen mode: the mode's output has already faded to the plain signal
                left[i] = l;
                right[i] = r;
            }
            else
            {
                left[i] = l + g * (ml - l);
                right[i] = r + g * (mr - r);
            }
        }
    }

    double sr = 48000.0, crossoverHz = 0.0, corrCoef = 0.0;
    std::array<std::array<loudness::Biquad, 2>, 2> kWeight {};
    std::array<std::array<float, loudness::interpolation::tapsPerPhase>, 2> history {};
    int historyPos = 0;
    std::array<LinkwitzRiley4, 2> lows {};
    double lr = 0.0, ll = 0.0, rr = 0.0, midLow = 0.0, sideLow = 0.0;
    static constexpr int numSubBlocks = 30, numBins = 800;   // 3 s of 100 ms sub-blocks; -70..+10 LUFS
    std::array<double, numSubBlocks> subBlocks {};
    int64_t subBlockCount = 0;
    double subBlockSum = 0.0;
    int subBlockFill = 0, subBlockLength = 4800;
    std::array<int64_t, numBins> histogramCount {};
    std::array<double, numBins> histogramPower {};
    std::array<SvfFilter, 2> phoneLow {}, phoneHigh {};
    std::atomic<bool> activeFlag { false }, resetRequested { false };
    bool wasActive = false;
    std::atomic<int> listenMode { 0 };
    int currentListen = 0;
    float listenFade = 1.0f, listenStep = 0.001f;
    Readings values;
};
}
