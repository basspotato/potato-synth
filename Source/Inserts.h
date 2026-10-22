#pragma once
#include "Filter.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

namespace ps
{
// A layer's chorus insert: two modulated taps per channel (7 and 11 ms, swung by up to +-3 ms), their sine
// LFOs a quarter cycle apart between the channels, so the wet signal is wide while each tap only bends the
// pitch by a few cents. Cubic Hermite reads keep the highs from dulling as the taps move. The wet path is
// high-passed (LR4 at 250 Hz) before it joins the dry, so the chorus never puts stereo energy into the mono
// lows. Above 250 Hz the sum is scaled by 1 / sqrt(1 + a^2) (wet and dry are decorrelated, so their powers
// add) to keep the level; the lows pass at unity: out = dry - (1 - norm) HP(dry) + norm a HP(wet), which is
// exactly the dry signal at Amount 0. At Amount 0 it only keeps its delay lines filled.
class Chorus
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        int size = 1;
        while (size < (int) (0.03f * sr) + 8)
            size <<= 1;
        for (auto& l : lines)
            l.assign ((size_t) size, 0.0f);
        mask = size - 1;
        for (auto* bank : { &wetHigh, &dryHigh })
            for (auto& f : *bank)
                f.setCutoff (250.0, sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        for (auto* bank : { &wetHigh, &dryHigh })
            for (auto& f : *bank)
                f.reset();
        writePos = 0;
        amount = 0.0f;
        phase = 0.0f;
    }

    // a: 0..1 (ramped from the last block), rateHz: LFO rate, depth: 0..1
    void process (float* left, float* right, int numSamples, float a, float rateHz, float depth) noexcept
    {
        if (a <= 0.0f && amount <= 0.0f)
        {
            // Off: only keep the lines filled, so turning it on never reads a step from silence into the signal
            for (int n = 0; n < numSamples; ++n)
            {
                lines[0][(size_t) writePos] = left[n];
                lines[1][(size_t) writePos] = right[n];
                writePos = (writePos + 1) & mask;
            }
            return;
        }
        const float from = amount;
        amount = a;
        const float inc = juce::jlimit (0.01f, 10.0f, rateHz) / sr;
        const float swing = 0.003f * sr * juce::jlimit (0.0f, 1.0f, depth);
        const float base[2] = { 0.007f * sr, 0.011f * sr };
        const float twoPi = juce::MathConstants<float>::twoPi;
        float* io[2] = { left, right };
        for (int n = 0; n < numSamples; ++n)
        {
            const float t = (float) (n + 1) / (float) numSamples;
            const float wetGain = from + (a - from) * t;
            const float norm = 1.0f / std::sqrt (1.0f + wetGain * wetGain);
            lines[0][(size_t) writePos] = left[n];
            lines[1][(size_t) writePos] = right[n];
            for (int ch = 0; ch < 2; ++ch)
            {
                float wet = 0.0f;
                for (int v = 0; v < 2; ++v)
                {
                    const float p = phase + 0.25f * (float) ch + 0.5f * (float) v;
                    wet += tap (ch, base[v] + swing * (1.0f + std::sin (twoPi * p)));
                }
                const float high = (float) wetHigh[(size_t) ch].process (0.5f * wet);
                const float dryHi = (float) dryHigh[(size_t) ch].process (io[ch][n]);
                io[ch][n] += norm * wetGain * high - (1.0f - norm) * dryHi;
            }
            writePos = (writePos + 1) & mask;
            phase += inc;
            if (phase >= 1.0f)
                phase -= 1.0f;
        }
        if (amount <= 0.0f)
            for (auto* bank : { &wetHigh, &dryHigh })   // fully off: the next turn-on starts its filters clean
                for (auto& f : *bank)
                    f.reset();
    }

private:
    float tap (int ch, float delay) const noexcept
    {
        const float pos = (float) writePos - delay;
        const int i1 = (int) std::floor (pos);
        const float f = pos - (float) i1;
        const auto& l = lines[(size_t) ch];
        const float y0 = l[(size_t) ((i1 - 1) & mask)], y1 = l[(size_t) (i1 & mask)];
        const float y2 = l[(size_t) ((i1 + 1) & mask)], y3 = l[(size_t) ((i1 + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    float sr = 48000.0f;
    std::array<std::vector<float>, 2> lines;
    int mask = 0, writePos = 0;
    float amount = 0.0f, phase = 0.0f;
    std::array<LinkwitzRiley4, 2> wetHigh { LinkwitzRiley4 { LinkwitzRiley4::Type::highPass }, LinkwitzRiley4 { LinkwitzRiley4::Type::highPass } };
    std::array<LinkwitzRiley4, 2> dryHigh { LinkwitzRiley4 { LinkwitzRiley4::Type::highPass }, LinkwitzRiley4 { LinkwitzRiley4::Type::highPass } };
};
}
