#pragma once
#include "RoleBand.h"

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
}
