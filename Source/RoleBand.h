#pragma once
#include "Filter.h"
#include <array>

namespace ps
{
// A cutoff that glides in the log domain (equal speed per octave) and a 0..1 enable gain that ramps,
// both updated in small sub-blocks so filters can move while audio runs without zipper noise or clicks.
struct GlidingCutoff
{
    static constexpr int subBlock = 16;

    void prepare (double sr, double startHz)
    {
        sampleRate = sr;
        current = target = startHz;
        glideCoef = 1.0 - std::exp (-(double) subBlock / (0.03 * sr));   // ~30 ms
        gainStep = (float) (1.0 / (0.02 * sr));                           // 20 ms fade
    }

    void setTarget (double hz, bool enabled, bool snap)
    {
        if (hz > 0.0)
        {
            target = hz;
            settled = false;
        }
        gainTarget = enabled ? 1.0f : 0.0f;
        if (snap)
        {
            current = target;
            gain = gainTarget;
        }
    }

    // Advances one sub-block; returns true when the cutoff changed and the filters need new coefficients.
    bool advance()
    {
        if (settled)
            return false;
        if (std::abs (current - target) < 1.0e-3 * target)
        {
            current = target;
            settled = true;
            return true;
        }
        current *= std::pow (target / current, glideCoef);
        return true;
    }

    float nextGain() noexcept
    {
        if (gain < gainTarget)      gain = std::min (gainTarget, gain + gainStep);
        else if (gain > gainTarget) gain = std::max (gainTarget, gain - gainStep);
        return gain;
    }

    bool isSilentlyOff() const noexcept { return gain <= 0.0f && gainTarget <= 0.0f; }

    double sampleRate = 48000.0, current = 1000.0, target = 1000.0, glideCoef = 1.0;
    float gain = 0.0f, gainTarget = 0.0f, gainStep = 1.0f;
    bool settled = true;
};

// Tracks whether a filter's input has gone silent and its tail has died away, so it can stop running and
// output true zeros (an IIR tail otherwise decays forever through tiny values).
struct TailTracker
{
    // Returns true when this block can be skipped: the input is all zeros and the last output was below -140 dBFS.
    bool canSkip (const float* left, const float* right, int numSamples) const noexcept
    {
        if (lastPeak > 1.0e-7f)
            return false;
        for (int i = 0; i < numSamples; ++i)
            if (std::abs (left[i]) > 0.0f || std::abs (right[i]) > 0.0f)
                return false;
        return true;
    }

    void measure (const float* left, const float* right, int numSamples) noexcept
    {
        float p = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            p = std::max (p, std::max (std::abs (left[i]), std::abs (right[i])));
        lastPeak = p;
    }

    float lastPeak = 0.0f;
};

// The role band-limit of one layer: an LR4 high-pass and an LR4 low-pass, each fading in and out
// as the layer's role changes. A stage that is fully off costs nothing.
class RoleBandLimit
{
public:
    void prepare (double sampleRate)
    {
        hp.cutoff.prepare (sampleRate, 20.0);
        lp.cutoff.prepare (sampleRate, 20000.0);
        for (auto* s : { &hp, &lp })
            s->updateCoefficients();
        reset();
    }

    void reset()
    {
        for (auto* s : { &hp, &lp })
            s->reset();
    }

    // Cutoffs in Hz; 0 turns a stage off. `snap` jumps straight there (used in prepare).
    void setBand (double highPassHz, double lowPassHz, bool snap)
    {
        hp.cutoff.setTarget (highPassHz, highPassHz > 0.0, snap);
        lp.cutoff.setTarget (lowPassHz, lowPassHz > 0.0, snap);
        if (snap)
            for (auto* s : { &hp, &lp })
                s->updateCoefficients();
    }

    void process (float* left, float* right, int numSamples)
    {
        for (auto* s : { &hp, &lp })
            s->process (left, right, numSamples);
    }

private:
    struct Stage
    {
        explicit Stage (LinkwitzRiley4::Type t) : filters { LinkwitzRiley4 (t), LinkwitzRiley4 (t) } {}

        void reset()
        {
            for (auto& f : filters)
                f.reset();
        }

        void updateCoefficients()
        {
            for (auto& f : filters)
                f.setCutoff (cutoff.current, cutoff.sampleRate);
        }

        void process (float* left, float* right, int numSamples)
        {
            if (cutoff.isSilentlyOff() || tail.canSkip (left, right, numSamples))
            {
                wasOff = true;
                return;
            }
            if (std::exchange (wasOff, false))
                reset();   // fading back in or waking from silence: start from rest, not from stale state

            for (int start = 0; start < numSamples; start += GlidingCutoff::subBlock)
            {
                if (cutoff.advance())
                    updateCoefficients();
                const int end = std::min (numSamples, start + GlidingCutoff::subBlock);
                for (int i = start; i < end; ++i)
                {
                    const float g = cutoff.nextGain();
                    const auto l = (float) filters[0].process (left[i]);
                    const auto r = (float) filters[1].process (right[i]);
                    left[i] += g * (l - left[i]);
                    right[i] += g * (r - right[i]);
                }
            }
            tail.measure (left, right, numSamples);
        }

        GlidingCutoff cutoff;
        TailTracker tail;
        std::array<LinkwitzRiley4, 2> filters;
        bool wasOff = true;
    };

    Stage hp { LinkwitzRiley4::Type::highPass }, lp { LinkwitzRiley4::Type::lowPass };
};
}
