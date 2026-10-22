#pragma once
#include "RoleBand.h"
#include "Loudness.h"
#include <juce_dsp/juce_dsp.h>

namespace ps
{
// The stack bus: processing on the summed layers. So far: the mono-bass stage, an LR4 split at the low
// crossover whose low band is summed to mono, so the lows hit the same in mono and stereo.
// Later stages (ducking, OTT, glue, width, space, auto-gain, true-peak) slot in after it.
class StackBus
{
public:
    void prepare (double sampleRate, double lowCrossoverHz, bool monoBassOn)
    {
        cutoff.prepare (sampleRate, lowCrossoverHz);
        cutoff.setTarget (lowCrossoverHz, monoBassOn, true);
        updateCoefficients();
        reset();
    }

    void reset()
    {
        for (auto& f : lows)  f.reset();
        for (auto& f : highs) f.reset();
    }

    void process (float* left, float* right, int numSamples, double lowCrossoverHz, bool monoBassOn)
    {
        cutoff.setTarget (lowCrossoverHz, monoBassOn, false);
        if (cutoff.isSilentlyOff() || tail.canSkip (left, right, numSamples))
        {
            wasOff = true;
            return;
        }
        if (std::exchange (wasOff, false))
            reset();

        for (int start = 0; start < numSamples; start += GlidingCutoff::subBlock)
        {
            if (cutoff.advance())
                updateCoefficients();
            const int end = std::min (numSamples, start + GlidingCutoff::subBlock);
            for (int i = start; i < end; ++i)
            {
                const float g = cutoff.nextGain();
                const double lowMono = 0.5 * (lows[0].process (left[i]) + lows[1].process (right[i]));
                const auto l = (float) (lowMono + highs[0].process (left[i]));
                const auto r = (float) (lowMono + highs[1].process (right[i]));
                left[i] += g * (l - left[i]);
                right[i] += g * (r - right[i]);
            }
        }
        tail.measure (left, right, numSamples);
    }

private:
    void updateCoefficients()
    {
        for (auto& f : lows)  f.setCutoff (cutoff.current, cutoff.sampleRate);
        for (auto& f : highs) f.setCutoff (cutoff.current, cutoff.sampleRate);
    }

    GlidingCutoff cutoff;
    TailTracker tail;
    std::array<LinkwitzRiley4, 2> lows { LinkwitzRiley4 (LinkwitzRiley4::Type::lowPass), LinkwitzRiley4 (LinkwitzRiley4::Type::lowPass) };
    std::array<LinkwitzRiley4, 2> highs { LinkwitzRiley4 (LinkwitzRiley4::Type::highPass), LinkwitzRiley4 (LinkwitzRiley4::Type::highPass) };
    bool wasOff = true;
};

// The last stage before the host: a 4x oversampled soft clipper that keeps inter-sample peaks under
// -1 dBTP. Below the knee (4 dB under the ceiling) it is linear, so normal levels pass untouched; above it
// a tanh curve bends smoothly into the ceiling. Oversampling keeps the clipper's harmonics from aliasing.
// The oversampler always runs, so the reported latency is constant; turning "safe output" off only fades
// the curve out.
class TruePeakClipper
{
public:
    static constexpr float ceilingDb = -1.3f;   // 0.3 dB of margin for the decimation filter's overshoot
    static constexpr float kneeDb = ceilingDb - 4.0f;

    void prepare (double sampleRate, int maxBlockSize)
    {
        maxBlock = juce::jmax (1, maxBlockSize);
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampler->initProcessing ((size_t) maxBlock);
        gainStep = (float) (1.0 / (0.02 * sampleRate * 4.0));
        gain = gainTarget;
    }

    void reset() { if (oversampler != nullptr) oversampler->reset(); }

    int getLatencySamples() const
    {
        return oversampler != nullptr ? juce::roundToInt (oversampler->getLatencyInSamples()) : 0;
    }

    void process (juce::AudioBuffer<float>& buffer, bool enabled)
    {
        gainTarget = enabled ? 1.0f : 0.0f;
        const int numSamples = buffer.getNumSamples();
        for (int start = 0; start < numSamples; start += maxBlock)
        {
            const int n = juce::jmin (maxBlock, numSamples - start);
            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) start, (size_t) n);
            auto up = oversampler->processSamplesUp (block);
            auto* l = up.getChannelPointer (0);
            auto* r = up.getChannelPointer (1);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                if (gain < gainTarget)      gain = std::min (gainTarget, gain + gainStep);
                else if (gain > gainTarget) gain = std::max (gainTarget, gain - gainStep);
                l[i] += gain * (shape (l[i]) - l[i]);
                r[i] += gain * (shape (r[i]) - r[i]);
            }
            oversampler->processSamplesDown (block);
        }
    }

    // Linear up to the knee, then a tanh bend that approaches the ceiling with a continuous slope
    static float shape (float x) noexcept
    {
        static const float knee = juce::Decibels::decibelsToGain (kneeDb);
        static const float span = juce::Decibels::decibelsToGain (ceilingDb) - knee;
        const float a = std::abs (x);
        if (a <= knee)
            return x;
        return std::copysign (knee + span * std::tanh ((a - knee) / span), x);
    }

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int maxBlock = 512;
    float gain = 1.0f, gainTarget = 1.0f, gainStep = 1.0f;
};

// Catches what the clipper can't: its decimation filter can overshoot by up to about 1 dB on a hard-clipped
// wave. A look-ahead limiter on true-peak estimates (4x interpolated) guarantees the ceiling. Gain is the
// sliding minimum of the required gain, a release, then a box average over the look-ahead, so it
// reaches the required value exactly at the peak and never steps. Fixed latency: lookahead + 6 samples.
class TruePeakLimiter
{
public:
    static constexpr int lookahead = 64;                              // 1.3 ms at 48 kHz
    static constexpr int holdLength = lookahead + 4;                  // covers the interpolator's spread
    static constexpr int detectorDelay = 6;                           // interpolator centre, rounded
    static constexpr int delayLength = lookahead + detectorDelay;
    static constexpr float ceilingDb = -1.1f;

    void prepare (double sampleRate)
    {
        releaseCoef = (float) (1.0 - std::exp (-1.0 / (0.05 * sampleRate)));
        reset();
    }

    void reset()
    {
        for (auto& h : history) h.fill (0.0f);
        for (auto& d : delay) d.fill (0.0f);
        hold.fill (1.0f);
        box.fill (1.0f);
        boxSum = (double) lookahead;
        envelope = 1.0f;
        quietSamples = delayLength + 16;
    }

    static constexpr int getLatencySamples() { return delayLength; }

    void process (float* left, float* right, int numSamples, bool enabled)
    {
        static const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        const auto& kernel = loudness::interpolation::kernel();
        float* io[2] = { left, right };

        for (int i = 0; i < numSamples; ++i)
        {
            // Idle: once everything in flight has been silent, the output is silent too
            if (std::abs (left[i]) > 0.0f || std::abs (right[i]) > 0.0f)
                quietSamples = 0;
            else if (quietSamples > delayLength + 16 && envelope >= 1.0f)
                continue;
            else
                ++quietSamples;

            float truePeak = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& h = history[(size_t) ch];
                h[(size_t) historyPos] = io[ch][i];
                for (int p = 0; p < loudness::interpolation::factor; ++p)
                {
                    double y = 0.0;
                    for (int k = 0; k < loudness::interpolation::tapsPerPhase; ++k)
                        y += h[(size_t) ((historyPos - k + historySize) % historySize)] * kernel[(size_t) (k * 4 + p)];
                    truePeak = std::max (truePeak, (float) std::abs (y));
                }
                truePeak = std::max (truePeak, std::abs (h[(size_t) ((historyPos - detectorDelay + historySize) % historySize)]));
            }
            historyPos = (historyPos + 1) % historySize;

            const float required = enabled && truePeak > ceiling ? ceiling / truePeak : 1.0f;
            hold[(size_t) holdPos] = required;
            holdPos = (holdPos + 1) % holdLength;
            const float held = *std::min_element (hold.begin(), hold.end());

            envelope = held < envelope || held - envelope < 1.0e-6f ? held : envelope + (held - envelope) * releaseCoef;

            boxSum += envelope - box[(size_t) boxPos];
            box[(size_t) boxPos] = envelope;
            boxPos = (boxPos + 1) % lookahead;
            const auto gain = (float) (boxSum / lookahead);

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& d = delay[(size_t) ch];
                const float delayed = d[(size_t) delayPos];
                d[(size_t) delayPos] = io[ch][i];
                io[ch][i] = delayed * gain;
            }
            delayPos = (delayPos + 1) % delayLength;
        }
    }

private:
    static constexpr int historySize = 16;
    std::array<std::array<float, historySize>, 2> history {};
    std::array<std::array<float, delayLength>, 2> delay {};
    std::array<float, holdLength> hold {};
    std::array<float, lookahead> box {};
    int historyPos = 0, holdPos = 0, boxPos = 0, delayPos = 0, quietSamples = 0;
    double boxSum = lookahead;
    float envelope = 1.0f, releaseCoef = 0.001f;
};
}
