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
        envelope = 0.0f;
        running = false;
        killing = 0;
    }

    // DROP: the tail is cut over 20 ms, then the network starts clean; the return comes back with new sound
    void killTail() noexcept { killing = (int) (0.02f * sr); }

    // Renders the return of left/right into returnL/returnR (at unity; the caller sets the send levels).
    // rt60: seconds; damping: RT60 ratio at 8 kHz (0..1); duck: 0..1, how far the return dips under the dry
    // signal. While `on` is false the network is cleared and nothing is computed; returns false then.
    bool render (const float* left, const float* right, float* returnL, float* returnR, int numSamples,
                 bool on, float rt60, float damping, float duck) noexcept
    {
        if (! on)
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

            const float mid = (float) returnMidHigh.process (0.5f * (outL + outR)).high;
            const float side = (float) returnSideHigh.process (0.5f * (outL - outR));
            float gw = duckGain;
            if (killing > 0)
            {
                gw *= (float) killing / (0.02f * sr);
                if (--killing == 0)
                    clearLines();
            }
            returnL[n] = gw * (mid + side);
            returnR[n] = gw * (mid - side);
        }
        flushDenormals();
        return true;
    }

private:
    static constexpr float modDepthMs = 0.25f;

    void clearLines() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        for (auto& p : preDelay)
            std::fill (p.begin(), p.end(), 0.0f);
        damp.fill (0.0f);
    }

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
    float dampG = 0.5f, envelope = 0.0f, envAttack = 0.01f, envRelease = 0.001f;
    bool running = false;
    int killing = 0;
    std::array<SvfFilter, 2> sendHigh {};
    SvfFilter returnMidHigh;
    LinkwitzRiley4 returnSideHigh { LinkwitzRiley4::Type::highPass };
};

// The second shared send: a tempo-synced stereo delay. Ping-pong sends the mid into the left line and crosses
// the feedback L -> R -> L; otherwise each channel repeats on itself. The send is high-passed (LR4 at 250 Hz)
// and the loop is band-limited (12 dB/oct high-pass at 250 Hz, low-pass at the Tone frequency) and gently
// saturated, so the repeats darken as they decay, never carry stereo energy into the mono lows, and stay
// bounded: the loop gain is at most 0.95 (spectral radius < 0.98). A new delay time crossfades between two
// read heads over 40 ms instead of sliding, so time changes neither click nor bend the pitch.
class SyncDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        int size = 1;
        while (size < (int) (maxSeconds * sr) + 4)
            size <<= 1;
        for (auto& l : lines)
            l.assign ((size_t) size, 0.0f);
        mask = size - 1;
        for (auto& f : sendHigh)
            f.setCutoff (250.0, sampleRate);
        envAttack = 1.0f - std::exp (-1.0f / (0.01f * sr));
        envRelease = 1.0f - std::exp (-1.0f / (0.25f * sr));
        fadeStep = 1.0f / (0.04f * sr);
        loopHighG = tptGain (250.0f, sr);
        reset();
    }

    void reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        for (auto& f : sendHigh) f.reset();
        for (auto& f : loopHigh) f.reset();
        for (auto& f : loopLow) f.reset();
        writePos = 0;
        envelope = 0.0f;
        fade = 1.0f;
        running = false;
        timeSamples = nextTimeSamples = -1.0f;
    }

    // Renders the return into returnL/returnR at unity. seconds: the delay time; feedback 0..0.95;
    // toneHz: the loop's high cut; duck 0..1 as in Space. Returns false (and computes nothing) while off.
    bool render (const float* left, const float* right, float* returnL, float* returnR, int numSamples,
                 bool on, float seconds, float feedback, float toneHz, bool pingPong, float duck) noexcept
    {
        if (! on)
        {
            if (running)
                reset();
            return false;
        }
        running = true;
        const float target = juce::jlimit (1.0f, maxSeconds * sr, seconds * sr);
        if (timeSamples < 0.0f)
            timeSamples = target;
        else if (std::abs (target - (fade < 1.0f ? nextTimeSamples : timeSamples)) > 0.5f && fade >= 1.0f)
        {
            nextTimeSamples = target;
            fade = 0.0f;
        }
        const float fb = juce::jlimit (0.0f, 0.95f, feedback);
        const float lowG = tptGain (toneHz, sr);
        constexpr float k = 1.41421356f;

        for (int n = 0; n < numSamples; ++n)
        {
            const float xl = (float) sendHigh[0].process (left[n]), xr = (float) sendHigh[1].process (right[n]);
            float y[2] = { tap (0, timeSamples), tap (1, timeSamples) };
            if (fade < 1.0f)
            {
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * fade);
                for (int ch = 0; ch < 2; ++ch)
                    y[ch] += w * (tap (ch, nextTimeSamples) - y[ch]);
                fade += fadeStep;
                if (fade >= 1.0f)
                {
                    fade = 1.0f;
                    timeSamples = nextTimeSamples;
                }
            }
            // The loop: band-limit, then a soft saturation that only bites on loud repeats
            float loop[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const float h = loopHigh[(size_t) ch].process (y[ch], loopHighG, k).high;
                loop[ch] = softClip (fb * loopLow[(size_t) ch].process (h, lowG, k).low);
            }
            if (pingPong)
            {
                lines[0][(size_t) writePos] = 0.5f * (xl + xr) + loop[1];
                lines[1][(size_t) writePos] = loop[0];
            }
            else
            {
                lines[0][(size_t) writePos] = xl + loop[0];
                lines[1][(size_t) writePos] = xr + loop[1];
            }
            writePos = (writePos + 1) & mask;

            const float level = std::max (std::abs (left[n]), std::abs (right[n]));
            envelope += (level - envelope) * (level > envelope ? envAttack : envRelease);
            const float g = 1.0f / (1.0f + duck * 4.0f * envelope);
            returnL[n] = g * y[0];
            returnR[n] = g * y[1];
        }
        for (auto* bank : { &loopHigh, &loopLow })
            for (auto& f : *bank)
            {
                if (std::abs (f.s1) < 1.0e-20f) f.s1 = 0.0f;
                if (std::abs (f.s2) < 1.0e-20f) f.s2 = 0.0f;
            }
        return true;
    }

private:
    static constexpr float maxSeconds = 4.0f;

    // Linear-interpolated read `delay` samples behind the write head
    float tap (int ch, float delay) const noexcept
    {
        const float pos = (float) writePos - delay;
        const int i0 = (int) std::floor (pos);
        const float frac = pos - (float) i0;
        const auto& line = lines[(size_t) ch];
        const float a = line[(size_t) (i0 & mask)], b = line[(size_t) ((i0 + 1) & mask)];
        return a + frac * (b - a);
    }

    float sr = 48000.0f;
    std::array<std::vector<float>, 2> lines;
    int mask = 0, writePos = 0;
    float timeSamples = -1.0f, nextTimeSamples = -1.0f, fade = 1.0f, fadeStep = 0.001f;
    float envelope = 0.0f, envAttack = 0.01f, envRelease = 0.001f, loopHighG = 0.0f;
    bool running = false;
    std::array<LinkwitzRiley4, 2> sendHigh { LinkwitzRiley4 { LinkwitzRiley4::Type::highPass }, LinkwitzRiley4 { LinkwitzRiley4::Type::highPass } };
    std::array<ZdfSvf, 2> loopHigh {}, loopLow {};
};
}
