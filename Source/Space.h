#pragma once
#include "Filter.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

namespace ps
{
// The shared space: an 8-line feedback delay network reverb (Jot's design). A Hadamard matrix mixes the lines,
// which is unitary, so the loop's energy is set only by each line's gain g_i = 10^(-3 d_i / (RT60 fs)): every
// line decays at the same rate whatever its length, and the spectral radius stays under 1 by construction
// (capped at 0.98 at the longest decay). A one-pole low-pass in each line makes the highs decay faster
// (RT60 above 4 kHz = damping x RT60). The line lengths are mutually prime and slowly modulated (+-0.25 ms,
// rates spaced by the golden ratio) so the tail never rings metallic or repeats.
//
// Mono lows: the send is high-passed at 200 Hz, and the return's side is high-passed again (LR4 at 250 Hz),
// so the reverb puts no stereo energy below the mono-bass crossover. Ducking: the return dips while the dry
// signal is loud and blooms in the gaps, so the space never smears the attacks.
class Space
{
public:
    static constexpr int numLines = 8;

    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = (float) sampleRate;
        static constexpr float lengthsMs[numLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.9f, 59.3f, 67.1f, 73.3f };
        int maxLen = 1;
        for (int i = 0; i < numLines; ++i)
        {
            length[(size_t) i] = lengthsMs[i] * 0.001f * sr;
            maxLen = std::max (maxLen, (int) length[(size_t) i] + (int) (modDepthMs * 0.001f * sr) + 4);
        }
        int size = 1;
        while (size < maxLen)
            size <<= 1;
        for (auto& line : lines)
            line.assign ((size_t) size, 0.0f);
        mask = size - 1;
        preDelaySamples = (int) (0.02f * sr);
        int preSize = 1;
        while (preSize < preDelaySamples + 1)
            preSize <<= 1;
        for (auto& p : preDelay)
            p.assign ((size_t) preSize, 0.0f);
        preMask = preSize - 1;
        dampG = tptGain (4000.0f, sr);
        dampG /= 1.0f + dampG;
        for (int i = 0; i < numLines; ++i)
            modRate[(size_t) i] = 0.11f * std::pow (1.618034f, (float) (i % 4)) * (i < 4 ? 1.0f : 1.3f);
        for (auto& f : sendHigh)
            f.setCutoff (200.0, sampleRate);
        returnMidHigh.setCutoff (150.0, sampleRate);
        returnSideHigh.setCutoff (250.0, sampleRate);
        envAttack = 1.0f - std::exp (-1.0f / (0.01f * sr));
        envRelease = 1.0f - std::exp (-1.0f / (0.25f * sr));
        juce::ignoreUnused (maxBlockSize);
        reset();
    }

    void reset()
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        for (auto& p : preDelay)
            std::fill (p.begin(), p.end(), 0.0f);
        for (auto& d : damp)
            d = 0.0f;
        for (auto& f : sendHigh) f.reset();
        returnMidHigh.reset();
        returnSideHigh.reset();
        writePos = prePos = 0;
        modPhase.fill (0.0f);
        wet = 0.0f;
        envelope = 0.0f;
        running = false;
    }

    // wetGain: the return's level (linear); rt60: seconds; damping: RT60 ratio at 8 kHz (0..1);
    // duck: 0..1, how far the return dips under the dry signal. Adds the return into left/right.
    // Returns false when silent and settled.
    bool process (float* left, float* right, int numSamples, float wetGain, float rt60, float damping, float duck) noexcept
    {
        if (wetGain < 1.0e-5f && wet < 1.0e-5f)
        {
            // Let the tail ring out inaudibly, then stop computing
            if (running)
                reset();
            return false;
        }
        running = true;
        rt60 = juce::jlimit (0.2f, 20.0f, rt60);
        std::array<float, numLines> g {}, lowGain {};
        for (int i = 0; i < numLines; ++i)
        {
            const float d = length[(size_t) i];
            g[(size_t) i] = std::min (0.98f, std::pow (10.0f, -3.0f * d / (rt60 * sr)));
            // The highs (above the damping split) decay at damping x RT60
            const float gHigh = std::min (0.98f, std::pow (10.0f, -3.0f * d / (juce::jmax (0.05f, damping) * rt60 * sr)));
            lowGain[(size_t) i] = gHigh / g[(size_t) i];
        }
        const float step = (wetGain - wet) / (float) numSamples;
        const float modDepth = modDepthMs * 0.001f * sr;
        const float twoPi = juce::MathConstants<float>::twoPi;
        std::array<float, numLines> modInc {};
        for (int i = 0; i < numLines; ++i)
            modInc[(size_t) i] = modRate[(size_t) i] / sr;

        for (int n = 0; n < numSamples; ++n)
        {
            const float xl = (float) sendHigh[0].process (left[n]).high, xr = (float) sendHigh[1].process (right[n]).high;
            preDelay[0][(size_t) prePos] = xl;
            preDelay[1][(size_t) prePos] = xr;
            const int readPre = (prePos - preDelaySamples) & preMask;
            const float inL = preDelay[0][(size_t) readPre], inR = preDelay[1][(size_t) readPre];
            prePos = (prePos + 1) & preMask;

            // Read every line at its modulated length (linear interpolation)
            std::array<float, numLines> y {};
            for (int i = 0; i < numLines; ++i)
            {
                auto& ph = modPhase[(size_t) i];
                ph += modInc[(size_t) i];
                if (ph >= 1.0f)
                    ph -= 1.0f;
                const float delay = length[(size_t) i] + modDepth * (1.0f + std::sin (twoPi * ph));
                const float pos = (float) writePos - delay;
                const int i0 = (int) std::floor (pos);
                const float frac = pos - (float) i0;
                const auto& line = lines[(size_t) i];
                const float a = line[(size_t) (i0 & mask)], b = line[(size_t) ((i0 + 1) & mask)];
                float v = a + frac * (b - a);
                // Damping: a one-pole TPT low-pass at 4 kHz splits the line; the part above it decays faster
                auto& z = damp[(size_t) i];
                const float vv = (v - z) * dampG;
                const float low = vv + z;
                z = low + vv;
                v = low + lowGain[(size_t) i] * (v - low);
                y[(size_t) i] = v * g[(size_t) i];
            }

            // Return: two orthogonal sign patterns of the lines, so L and R are decorrelated
            const float outL = 0.35f * (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]);
            const float outR = 0.35f * (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]);

            hadamard (y);
            for (int i = 0; i < numLines; ++i)
                lines[(size_t) i][(size_t) writePos] = y[(size_t) i] + ((i & 1) == 0 ? inL : inR) * 0.5f;
            writePos = (writePos + 1) & mask;

            // Duck the return under the dry signal
            const float level = std::max (std::abs (left[n]), std::abs (right[n]));
            envelope += (level - envelope) * (level > envelope ? envAttack : envRelease);
            const float duckGain = 1.0f / (1.0f + duck * 4.0f * envelope);

            wet += step;
            const float mid = (float) returnMidHigh.process (0.5f * (outL + outR)).high;
            const float side = (float) returnSideHigh.process (0.5f * (outL - outR));
            const float gw = wet * duckGain;
            left[n] += gw * (mid + side);
            right[n] += gw * (mid - side);
        }
        wet = wetGain;
        flushDenormals();
        return true;
    }

private:
    static constexpr float modDepthMs = 0.25f;

    // In-place 8-point fast Walsh-Hadamard, scaled by 1/sqrt(8) so it is unitary
    static void hadamard (std::array<float, numLines>& x) noexcept
    {
        for (int h = 1; h < numLines; h <<= 1)
            for (int i = 0; i < numLines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[(size_t) j], b = x[(size_t) (j + h)];
                    x[(size_t) j] = a + b;
                    x[(size_t) (j + h)] = a - b;
                }
        constexpr float scale = 0.35355339f;
        for (auto& v : x)
            v *= scale;
    }

    void flushDenormals() noexcept
    {
        for (auto& d : damp)
            if (std::abs (d) < 1.0e-20f)
                d = 0.0f;
    }

    float sr = 48000.0f;
    std::array<std::vector<float>, numLines> lines;
    std::array<std::vector<float>, 2> preDelay;
    std::array<float, numLines> length {}, damp {}, modRate {};
    std::array<float, numLines> modPhase {};
    int mask = 0, writePos = 0, preMask = 0, prePos = 0, preDelaySamples = 0;
    float dampG = 0.5f, wet = 0.0f, envelope = 0.0f, envAttack = 0.01f, envRelease = 0.001f;
    bool running = false;
    std::array<SvfFilter, 2> sendHigh {};
    SvfFilter returnMidHigh;
    LinkwitzRiley4 returnSideHigh { LinkwitzRiley4::Type::highPass };
};
}
