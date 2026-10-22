#pragma once
#include "RoleBand.h"
#include "Loudness.h"
#include "Filter.h"
#include "HarmonyEngine.h"
#include "Drive.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>

namespace ps
{
// The stack bus: processing on the summed layers. So far: the mono-bass stage, an LR4 split at the low
// crossover whose low band is summed to mono, so the lows hit the same in mono and stereo, and the width
// floor: the side signal is high-passed (LR4) at twice the crossover (about 250 Hz), so stereo width only
// starts where the ear can use it and no layer's spread leaks into the mono zone through the crossover's skirt.
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
        sideHigh.reset();
        for (auto& f : midAllpass) f.reset();
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
                const double hl = highs[0].process (left[i]), hr = highs[1].process (right[i]);
                // The mid goes through the matching LR4 all-pass (low + high sum), so mid and side keep the same
                // phase above the width floor and a hard-panned sound stays hard-panned
                const double m = 0.5 * (hl + hr);
                const double mid = lowMono + midAllpass[0].process (m) + midAllpass[1].process (m);
                const double side = sideHigh.process (0.5 * (hl - hr));
                const auto l = (float) (mid + side);
                const auto r = (float) (mid - side);
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
        sideHigh.setCutoff (2.0 * cutoff.current, cutoff.sampleRate);
        for (auto& f : midAllpass) f.setCutoff (2.0 * cutoff.current, cutoff.sampleRate);
    }

    GlidingCutoff cutoff;
    TailTracker tail;
    std::array<LinkwitzRiley4, 2> lows { LinkwitzRiley4 (LinkwitzRiley4::Type::lowPass), LinkwitzRiley4 (LinkwitzRiley4::Type::lowPass) };
    std::array<LinkwitzRiley4, 2> highs { LinkwitzRiley4 (LinkwitzRiley4::Type::highPass), LinkwitzRiley4 (LinkwitzRiley4::Type::highPass) };
    LinkwitzRiley4 sideHigh { LinkwitzRiley4::Type::highPass };
    std::array<LinkwitzRiley4, 2> midAllpass { LinkwitzRiley4 (LinkwitzRiley4::Type::lowPass), LinkwitzRiley4 (LinkwitzRiley4::Type::highPass) };
    bool wasOff = true;
};

// MASS's energy focus: a bell on the tactile band (60 Hz, Q 0.8) on the summed stack, after the mono-bass
// stage, so the lows it lifts are already mono. A ZDF SVF bell (Simper): y = x + k (A^2 - 1) band, with
// k = 1 / (Q A) and A = 10^(dB / 40), so the gain at 60 Hz is exactly the knob's dB and stays smooth while
// it moves.
class LowFocus
{
public:
    static constexpr float centreHz = 60.0f, q = 0.8f;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        g = tptGain (centreHz, sr);
        smoothCoef = 1.0f - std::exp (-(float) subBlock / (0.03f * sr));
        reset();
    }

    void reset()
    {
        for (auto& f : filters)
            f.reset();
        currentDb = 0.0f;
    }

    // Returns false (and leaves the signal alone) when the focus is flat and settled
    bool process (float* left, float* right, int numSamples, float targetDb) noexcept
    {
        if (std::abs (targetDb) < 1.0e-3f && std::abs (currentDb) < 1.0e-3f)
        {
            if (active)
            {
                reset();
                active = false;
            }
            return false;
        }
        active = true;
        for (int start = 0; start < numSamples; start += subBlock)
        {
            currentDb += (targetDb - currentDb) * smoothCoef;
            const float a = std::pow (10.0f, currentDb / 40.0f), k = 1.0f / (q * a), m1 = k * (a * a - 1.0f);
            const auto c = ZdfSvf::Coeffs::make (g, k);
            const int end = std::min (numSamples, start + subBlock);
            for (int i = start; i < end; ++i)
            {
                left[i] += m1 * filters[0].process (left[i], c).band;
                right[i] += m1 * filters[1].process (right[i], c).band;
            }
        }
        return true;
    }

private:
    static constexpr int subBlock = 16;
    std::array<ZdfSvf, 2> filters {};
    float sr = 48000.0f, g = 0.0f, smoothCoef = 0.1f, currentDb = 0.0f;
    bool active = false;
};

// MASS's bass harmonics: the stack's lows made audible on speakers that can't play them (the missing
// fundamental: the ear rebuilds a low pitch from its 2nd and 3rd harmonics). The mono low band (below 120 Hz)
// is normalised by its envelope and fed through Chebyshev polynomials, T2 and T3, which turn a sine into
// exactly its 2nd and 3rd harmonic, so nothing above 3 x 120 Hz is made and nothing can alias. The result goes
// back to the envelope's level, through a 90-400 Hz band-pass, and is added in mono, so the lows stay mono.
// It runs among the knob stages, so the loudness trim takes the level it adds back out of the whole stack.
class BassHarmonics
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        gLow = tptGain (120.0f, sr);
        gHigh = tptGain (90.0f, sr);
        gTop = tptGain (400.0f, sr);
        attack = 1.0f - std::exp (-1.0f / (0.002f * sr));
        release = 1.0f - std::exp (-1.0f / (0.06f * sr));
        smoothCoef = 1.0f - std::exp (-1.0f / (0.03f * sr));
        reset();
    }

    void reset()
    {
        for (auto* f : { &low1, &low2, &high, &top })
            f->reset();
        envelope = current = 0.0f;
    }

    // amount 0..1: the harmonics' level against the low band they come from. Returns false when off and settled.
    bool process (float* left, float* right, int numSamples, float amount) noexcept
    {
        if (amount < 1.0e-4f && current < 1.0e-4f)
        {
            if (active)
            {
                reset();
                active = false;
            }
            return false;
        }
        active = true;
        constexpr float k = 1.4142f;   // Butterworth
        for (int i = 0; i < numSamples; ++i)
        {
            current += (amount - current) * smoothCoef;
            const float mid = 0.5f * (left[i] + right[i]);
            const float lowBand = low2.process (low1.process (mid, gLow, k).low, gLow, k).low;
            const float a = std::abs (lowBand);
            envelope += (a - envelope) * (a > envelope ? attack : release);
            const float u = juce::jlimit (-1.0f, 1.0f, lowBand / std::max (envelope, 1.0e-6f));
            const float t2 = 2.0f * u * u - 1.0f, t3 = u * (4.0f * u * u - 3.0f);
            const float h = envelope * (0.6f * t2 + 0.4f * t3);
            const float shaped = top.process (high.process (h, gHigh, k).high, gTop, k).low;
            const float add = 2.0f * current * shaped;
            left[i] += add;
            right[i] += add;
        }
        return true;
    }

private:
    ZdfSvf low1, low2, high, top;
    float sr = 48000.0f, gLow = 0.0f, gHigh = 0.0f, gTop = 0.0f;
    float attack = 0.01f, release = 0.001f, smoothCoef = 0.001f, envelope = 0.0f, current = 0.0f;
    bool active = false;
};

// PRESSURE: loud and dense like a finished master, but clean. Distributed gain reduction: the knob asks for
// G(u) = 12 u^1.3 dB of total reduction, shared out so no single stage pumps or distorts:
//   OTT 30 % (3 bands, up + down), glue 20 % (2:1) here, and saturation 25 % (parallel ADAA tanh) plus soft
//   clipping 25 % inside TruePeakClipper's 4x oversampled loop (pressureSaturate), so they never alias and
//   add no latency of their own.
// The dynamics are stereo-linked (one gain for both channels) and the saturation and clipping act on the mid
// only, so nothing PRESSURE does can create side signal in the mono lows. The Harmony Engine trims the result back to the patch's loudness: PRESSURE raises density, not
// level.
class Pressure
{
public:
    static constexpr float crossoverLowHz = 88.0f, crossoverHighHz = 2500.0f;

    static float totalReductionDb (float u) noexcept { return 12.0f * std::pow (u, 1.3f); }

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int b = 0; b < 2; ++b)
            {
                splitLow[(size_t) b][(size_t) ch].setCutoff (b == 0 ? crossoverLowHz : crossoverHighHz, sampleRate);
                splitHigh[(size_t) b][(size_t) ch].setCutoff (b == 0 ? crossoverLowHz : crossoverHighHz, sampleRate);
            }
            for (auto& f : lowAllpass[(size_t) ch])
                f.setCutoff (crossoverHighHz, sampleRate);
        }
        reset();
    }

    void reset()
    {
        for (auto& b : splitLow) for (auto& f : b) f.reset();
        for (auto& b : splitHigh) for (auto& f : b) f.reset();
        for (auto& c : lowAllpass) for (auto& f : c) f.reset();
        bandEnvelope.fill (0.0f);
        glueDetector = glueShortTerm = 0.0f;
        glueGainDb = 0.0f;
        amount = 0.0f;
    }

    // GROOVE: lock = 0..1 moves the OTT releases toward a 1/16 note and the glue's toward a 1/8 note, so the
    // dynamics breathe in time
    void setTempoLock (float lock, double secondsPerBeat) noexcept
    {
        tempoLock = juce::jlimit (0.0f, 1.0f, lock);
        beatSeconds = (float) juce::jlimit (0.1, 4.0, secondsPerBeat);
    }

    // u: the PRESSURE knob 0..1, glided over 30 ms. Returns false when the stage is fully out and settled.
    bool process (float* left, float* right, int numSamples, float u) noexcept
    {
        const float target = juce::jlimit (0.0f, 1.0f, u);
        trackLevel (left, right, numSamples);
        amount += (target - amount) * (1.0f - std::exp (-(float) numSamples / (0.03f * sr)));
        if (target < 1.0e-4f && amount < 1.0e-4f)
        {
            if (wasActive)
                reset();
            wasActive = false;
            return false;
        }
        wasActive = true;
        const float g = totalReductionDb (amount);
        ott (left, right, numSamples, 0.55f * g / 12.0f);
        glue (left, right, numSamples, 0.2f * g);
        return true;
    }

    // The glided amount, for the saturation and clipping shares in TruePeakClipper
    // Every threshold follows the signal's short-term level (400 ms RMS), so PRESSURE does the same to a quiet
    // patch as to a loud one. 0 dB at the level the stages were tuned on (a -10 LUFS stack); at most -30/+6 dB.
    float levelShiftDb() const noexcept
    {
        if (! followLevel)
            return 0.0f;
        return juce::jlimit (-30.0f, 6.0f, 10.0f * std::log10 (levelPower + 1.0e-12f) - referenceDb);
    }

    float getAmount() const noexcept { return amount; }

    // The patch's own master chain (an instance of its own, outside the knob stages; call prepareMaster). OTT at
    // depth 0..1 with fixed thresholds (it squeezes quiet and loud notes together, as a mastering OTT does), its
    // times scaled and its upward lift scaled by upward (0..1); then glue at glueDb of reduction with its own
    // release. Each amount glides over 30 ms, and the chain crossfades in and out from the dry signal over 20 ms
    // (its band split changes the phase, so a hard switch would click). Bypassed and reset once out.
    void prepareMaster (double sampleRate, int maxBlock)
    {
        prepare (sampleRate);
        followLevel = false;
        dryL.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
        dryR.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
    }

    bool processMaster (float* left, float* right, int numSamples, float ottDepth, float timeScale, float upward,
                        float glueDb, float glueReleaseSeconds) noexcept
    {
        const bool wanted = ottDepth > 0.0f || glueDb > 0.0f;
        if (! wanted && masterFade <= 0.0f)
        {
            if (wasActive)
            {
                reset();
                masterOtt = masterGlue = 0.0f;
            }
            wasActive = false;
            return false;
        }
        wasActive = true;
        const float fadeStep = 1.0f / (0.02f * sr);
        for (int pos = 0; pos < numSamples; pos += (int) dryL.size())
        {
            const int n = juce::jmin ((int) dryL.size(), numSamples - pos);
            float* l = left + pos;
            float* r = right + pos;
            std::copy_n (l, n, dryL.data());
            std::copy_n (r, n, dryR.data());
            trackLevel (l, r, n);
            const float coef = 1.0f - std::exp (-(float) n / (0.03f * sr));
            masterOtt += (juce::jlimit (0.0f, 1.0f, ottDepth) - masterOtt) * coef;
            masterGlue += (juce::jlimit (0.0f, 12.0f, glueDb) - masterGlue) * coef;
            ott (l, r, n, masterOtt, juce::jlimit (0.25f, 4.0f, timeScale), juce::jlimit (0.0f, 1.0f, upward));
            glue (l, r, n, masterGlue, glueReleaseSeconds);
            for (int i = 0; i < n; ++i)
            {
                masterFade = juce::jlimit (0.0f, 1.0f, masterFade + (wanted ? fadeStep : -fadeStep));
                l[i] = dryL[(size_t) i] + (l[i] - dryL[(size_t) i]) * masterFade;
                r[i] = dryR[(size_t) i] + (r[i] - dryR[(size_t) i]) * masterFade;
            }
        }
        return true;
    }

private:
    float masterOtt = 0.0f, masterGlue = 0.0f, masterFade = 0.0f;
    bool followLevel = true;
    std::vector<float> dryL, dryR;
    // OTT: 3 bands (LR4 at 88 Hz and 2.5 kHz), each compressed downward above -22 dB (4:1) and upward below
    // -40 dB (3:1, at most +12 dB, never below -70 dB, so silence isn't pumped up), all thresholds moved by
    // levelShiftDb(). depth scales the gain in dB.
    void ott (float* left, float* right, int n, float depth, float timeScale = 1.0f, float upward = 1.0f) noexcept
    {
        const float shift = levelShiftDb();
        const float downDb = -22.0f + shift, upDb = -40.0f + shift, gateDb = -70.0f + shift;
        static constexpr float attackMs[3] = { 8.0f, 4.0f, 2.0f }, releaseMs[3] = { 120.0f, 80.0f, 50.0f };
        const float sixteenthMs = 250.0f * beatSeconds;
        float attack[3], release[3];
        for (int b = 0; b < 3; ++b)
        {
            attack[b] = 1.0f - std::exp (-1.0f / (timeScale * attackMs[b] * 0.001f * sr));
            const float ms = timeScale * releaseMs[b] + tempoLock * (sixteenthMs - timeScale * releaseMs[b]);
            release[b] = 1.0f - std::exp (-1.0f / (ms * 0.001f * sr));
        }
        for (int i = 0; i < n; ++i)
        {
            float band[3][2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto c = (size_t) ch;
                const double x = ch == 0 ? left[i] : right[i];
                const double low = splitLow[0][c].process (x), rest = splitHigh[0][c].process (x);
                // The low band also takes the 2.5 kHz split's phase (its LR4 all-pass), so the bands sum back flat
                band[0][ch] = (float) (lowAllpass[c][0].process (low) + lowAllpass[c][1].process (low));
                band[1][ch] = (float) splitLow[1][c].process (rest);
                band[2][ch] = (float) splitHigh[1][c].process (rest);
            }
            float outL = 0.0f, outR = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                const float level = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
                auto& env = bandEnvelope[(size_t) b];
                env += (level - env) * (level > env ? attack[b] : release[b]);
                const float db = 20.0f * std::log10 (env + 1.0e-9f);
                float gainDb = 0.0f;
                if (db > downDb)
                    gainDb = (downDb - db) * 0.75f;
                else if (db < upDb && db > gateDb)
                    gainDb = upward * std::min (12.0f, (upDb - db) * (2.0f / 3.0f));
                const float gain = std::exp2 (gainDb * depth * (1.0f / 6.0206f));
                outL += band[b][0] * gain;
                outR += band[b][1] * gain;
            }
            left[i] = outL;
            right[i] = outR;
        }
    }

    void trackLevel (const float* left, const float* right, int n) noexcept
    {
        float sum = 0.0f;
        for (int i = 0; i < n; ++i)
            sum += 0.5f * (left[i] * left[i] + right[i] * right[i]);
        levelPower += (sum / (float) juce::jmax (1, n) - levelPower) * (1.0f - std::exp (-(float) n / (0.4f * sr)));
    }

    // Glue: 2:1 with a 6 dB soft knee, 30 ms attack, 200 ms release, linked. Its threshold sits 2x the target
    // reduction under the short-term (400 ms) level, so it lands on its share of G(u) whatever the patch's level.
    void glue (float* left, float* right, int n, float targetDb, float baseReleaseSeconds = 0.2f) noexcept
    {
        const float rmsCoef = 1.0f - std::exp (-1.0f / (0.01f * sr)), shortCoef = 1.0f - std::exp (-1.0f / (0.4f * sr));
        const float attack = 1.0f - std::exp (-1.0f / (0.03f * sr));
        const float releaseSeconds = baseReleaseSeconds + tempoLock * (0.5f * beatSeconds - baseReleaseSeconds);
        const float release = 1.0f - std::exp (-1.0f / (releaseSeconds * sr));
        for (int i = 0; i < n; ++i)
        {
            const float p = 0.5f * (left[i] * left[i] + right[i] * right[i]);
            glueDetector += (p - glueDetector) * rmsCoef;
            glueShortTerm += (p - glueShortTerm) * shortCoef;
            const float level = 10.0f * std::log10 (glueDetector + 1.0e-12f);
            const float over = level - (10.0f * std::log10 (glueShortTerm + 1.0e-12f) - 2.0f * targetDb);
            float wanted = 0.0f;
            if (targetDb > 1.0e-3f && level > -80.0f)
            {
                if (over > 3.0f)
                    wanted = -0.5f * over;
                else if (over > -3.0f)
                    wanted = -0.5f * (over + 3.0f) * (over + 3.0f) / 12.0f;
            }
            glueGainDb += (wanted - glueGainDb) * (wanted < glueGainDb ? attack : release);
            const float gain = std::exp2 (glueGainDb * (1.0f / 6.0206f));
            left[i] *= gain;
            right[i] *= gain;
        }
    }

    static constexpr float referenceDb = -12.0f;
    float sr = 48000.0f, amount = 0.0f, tempoLock = 0.0f, beatSeconds = 0.5f, levelPower = 0.0f;
    bool wasActive = false;
    using LR = LinkwitzRiley4;
    using Pair = std::array<LR, 2>;
    std::array<Pair, 2> splitLow { Pair { LR (LR::Type::lowPass), LR (LR::Type::lowPass) }, Pair { LR (LR::Type::lowPass), LR (LR::Type::lowPass) } };
    std::array<Pair, 2> splitHigh { Pair { LR (LR::Type::highPass), LR (LR::Type::highPass) }, Pair { LR (LR::Type::highPass), LR (LR::Type::highPass) } };
    std::array<Pair, 2> lowAllpass { Pair { LR (LR::Type::lowPass), LR (LR::Type::highPass) }, Pair { LR (LR::Type::lowPass), LR (LR::Type::highPass) } };
    std::array<float, 3> bandEnvelope {};
    float glueDetector = 0.0f, glueShortTerm = 0.0f, glueGainDb = 0.0f;
};

// The patch's stereo width on the summed stack: the side signal scaled by 0..2 (glided over 30 ms). The bus has
// already high-passed the side at about 250 Hz (the width floor), so this widens or narrows only above it and the
// lows stay mono. The mid is untouched, so a mono fold-down sounds the same at any width. Exactly bypassed at 100 %.
class BusWidth
{
public:
    void prepare (double sampleRate) noexcept
    {
        coef = 1.0f - std::exp (-1.0f / (0.03f * (float) sampleRate));
        current = 1.0f;
    }

    void process (float* left, float* right, int numSamples, float width) noexcept
    {
        const float target = juce::jlimit (0.0f, 2.0f, width);
        if (std::abs (current - 1.0f) < 1.0e-6f && std::abs (target - 1.0f) < 1.0e-6f)
            return;
        for (int i = 0; i < numSamples; ++i)
        {
            current += (target - current) * coef;
            const float mid = 0.5f * (left[i] + right[i]), side = 0.5f * (left[i] - right[i]) * current;
            left[i] = mid + side;
            right[i] = mid - side;
        }
        if (std::abs (current - target) < 1.0e-5f)
            current = target;
    }

private:
    float coef = 0.001f, current = 1.0f;
};

// SHINE's spectral tilt: three first-order high shelves at 500 Hz, 2 kHz and 8 kHz, each lifting by the same
// dB, add up to a near-constant slope of about half that per octave between them; the result is normalised to
// unity at the 1 kHz pivot, so the mids stay put while the top brightens and the low mids recede. Linear and
// identical on both channels, so the image and the mono lows are untouched.
class Tilt
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (size_t k = 0; k < 3; ++k)
            g[k] = tptGain (corners[k], sr);
        reset();
    }

    void reset()
    {
        for (auto& ch : filters)
            for (auto& f : ch)
                f.reset();
        currentDb = 0.0f;
    }

    // dbPerOctave: the added slope; returns false when flat and settled
    bool process (float* left, float* right, int numSamples, float dbPerOctave) noexcept
    {
        const float targetDb = 2.0f * dbPerOctave;   // per shelf
        if (std::abs (targetDb) < 1.0e-3f && std::abs (currentDb) < 1.0e-3f)
        {
            if (active)
                reset();
            active = false;
            return false;
        }
        active = true;
        const float coef = 1.0f - std::exp (-(float) subBlock / (0.03f * sr));
        for (int start = 0; start < numSamples; start += subBlock)
        {
            currentDb += (targetDb - currentDb) * coef;
            const float a = juce::Decibels::decibelsToGain (currentDb) - 1.0f;
            // |H| at the pivot for the analog prototypes: |1 + a s/(s + wc)| at s = j w
            float pivot = 1.0f;
            for (float fc : corners)
            {
                const std::complex<float> s (0.0f, 1000.0f / fc);
                pivot *= std::abs (1.0f + a * s / (s + 1.0f));
            }
            const float norm = 1.0f / pivot;
            const int end = std::min (numSamples, start + subBlock);
            float* io[2] = { left, right };
            for (int ch = 0; ch < 2; ++ch)
                for (int i = start; i < end; ++i)
                {
                    float x = io[ch][i];
                    for (size_t k = 0; k < 3; ++k)
                        x += a * filters[(size_t) ch][k].process (x, g[k]);
                    io[ch][i] = x * norm;
                }
        }
        return true;
    }

private:
    static constexpr int subBlock = 16;
    static constexpr float corners[3] = { 500.0f, 2000.0f, 8000.0f };
    std::array<std::array<ZdfHighPass1, 3>, 2> filters {};
    std::array<float, 3> g {};
    float sr = 48000.0f, currentDb = 0.0f;
    bool active = false;
};

// CLARITY's de-harsh: a dynamic bell on the 2-5 kHz presence band (3.2 kHz, Q 1.25). It compares that band's
// short-term power with the whole signal's, so it reacts to harshness, not to level: when the band's share
// rises over a threshold (-6 dB at u = 0 down to -18 dB at u = 1) it cuts by 0.6 dB per dB over, up to 6u dB,
// with a 3 dB soft knee. Linear filters and one linked gain, so the image and the mono lows are untouched.
class DeHarsh
{
public:
    static constexpr float centreHz = 3200.0f, k = 0.8f;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        g = tptGain (centreHz, sr);
        attack = 1.0f - std::exp (-1.0f / (0.005f * sr));
        release = 1.0f - std::exp (-1.0f / (0.08f * sr));
        reset();
    }

    void reset()
    {
        for (auto& f : filters)
            f.reset();
        bandPower = fullPower = 0.0f;
        gain = 1.0f;
        amount = 0.0f;
    }

    bool process (float* left, float* right, int numSamples, float u) noexcept
    {
        const float target = juce::jlimit (0.0f, 1.0f, u);
        if (target < 1.0e-4f && amount < 1.0e-4f && std::abs (gain - 1.0f) < 1.0e-5f)
        {
            if (amount != 0.0f || bandPower != 0.0f)
                reset();
            return false;
        }
        for (int start = 0; start < numSamples; start += subBlock)
        {
            const int end = std::min (numSamples, start + subBlock);
            amount += (target - amount) * (1.0f - std::exp (-(float) subBlock / (0.03f * sr)));
            // The gain for this sub-block, from the detector as it stands
            const float shareDb = 10.0f * std::log10 ((bandPower + 1.0e-12f) / (fullPower + 1.0e-12f));
            const float over = shareDb - (-6.0f - 12.0f * amount);
            constexpr float knee = 3.0f;
            float cutDb = 0.0f;
            if (over > knee * 0.5f)
                cutDb = 0.6f * over;
            else if (over > -knee * 0.5f)
                cutDb = 0.6f * juce::square (over + knee * 0.5f) / (2.0f * knee);
            cutDb = std::min (cutDb, 6.0f * amount);
            const float targetGain = juce::Decibels::decibelsToGain (-cutDb);
            const float step = (targetGain - gain) / (float) (end - start);
            for (int i = start; i < end; ++i)
            {
                const float bl = k * filters[0].process (left[i], g, k).band;
                const float br = k * filters[1].process (right[i], g, k).band;
                const float bm = 0.5f * (bl + br), xm = 0.5f * (left[i] + right[i]);
                bandPower += (bm * bm - bandPower) * (bm * bm > bandPower ? attack : release);
                fullPower += (xm * xm - fullPower) * (xm * xm > fullPower ? attack : release);
                gain += step;
                left[i] += (gain - 1.0f) * bl;
                right[i] += (gain - 1.0f) * br;
            }
        }
        return true;
    }

private:
    static constexpr int subBlock = 16;
    std::array<ZdfSvf, 2> filters {};
    float sr = 48000.0f, g = 0.2f, attack = 0.01f, release = 0.001f;
    float bandPower = 0.0f, fullPower = 0.0f, gain = 1.0f, amount = 0.0f;
};

// BUILD's two bus stages. The rise: a 12 dB/oct high-pass that climbs from 20 Hz to 400 Hz with the build,
// thinning the low end so the drop lands with it. The Shepard riser: ten octave-spaced sines under a Gaussian
// envelope in log-frequency (centre 800 Hz, 1.2 octaves), all gliding up together at one octave per two bars;
// each one fades in at the bottom as it fades out at the top, so the pitch rises forever and never jumps.
// The riser plays at a fixed share of the stack's own short-term level, so it sits the same on any patch.
// Both are identical on the two channels and the riser carries nothing under 120 Hz worth hearing (its
// envelope is 4 octaves down there), so the lows stay mono.
class BuildStage
{
public:
    static constexpr int numPartials = 10;
    static constexpr float lowestHz = 20.0f, centreOctave = 5.32f, widthOctaves = 1.2f;   // 20 * 2^5.32 = 800 Hz

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& f : highPass)
            f.reset();
        glide = 0.0;
        phases.fill (0.0);
        amount = 0.0f;
        cutoffG = tptGain (lowestHz, sr);
    }

    // b: the build 0..1 (already ramped); levelPower: the stack's short-term power (mean of L^2 and R^2)
    bool process (float* left, float* right, int numSamples, float b, float levelPower, double bpm) noexcept
    {
        if (b < 1.0e-4f && amount < 1.0e-4f)
        {
            if (amount != 0.0f)
                reset();
            return false;
        }
        const float from = amount;
        amount = b;
        const double octavesPerSample = (juce::jlimit (20.0, 400.0, bpm) / 60.0) / 8.0 / sr;
        const float levelAmp = std::sqrt (juce::jmax (0.0f, levelPower));
        for (int start = 0; start < numSamples; start += subBlock)
        {
            const int end = std::min (numSamples, start + subBlock);
            const float a = from + (b - from) * (float) end / (float) numSamples;
            cutoffG = tptGain (lowestHz * std::pow (20.0f, a), sr);   // 20 Hz .. 400 Hz
            // Partial amplitudes and increments for this sub-block
            std::array<float, numPartials> amp {};
            std::array<double, numPartials> inc {};
            for (int k = 0; k < numPartials; ++k)
            {
                const double octave = (double) k + glide;
                const double f = lowestHz * std::exp2 (octave);
                const double z = (octave - centreOctave) / widthOctaves;
                amp[(size_t) k] = f < 0.45 * sr ? (float) std::exp (-0.5 * z * z) : 0.0f;
                inc[(size_t) k] = f / sr;
            }
            const float riserGain = 0.25f * a * a * levelAmp;
            for (int i = start; i < end; ++i)
            {
                float riser = 0.0f;
                for (int k = 0; k < numPartials; ++k)
                {
                    auto& ph = phases[(size_t) k];
                    ph += inc[(size_t) k];
                    if (ph >= 1.0)
                        ph -= 1.0;
                    riser += amp[(size_t) k] * (float) std::sin (juce::MathConstants<double>::twoPi * ph);
                }
                float* io[2] = { left, right };
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float x = highPass[(size_t) ch].process (io[ch][i], cutoffG, juce::MathConstants<float>::sqrt2).high;
                    io[ch][i] = x + riserGain * riser;
                }
            }
            glide += octavesPerSample * (end - start);
            if (glide >= 1.0)
            {
                // Every partial moves down one slot: the top one (silent) wraps to the bottom (silent too)
                glide -= 1.0;
                std::rotate (phases.rbegin(), phases.rbegin() + 1, phases.rend());
            }
        }
        return true;
    }

private:
    static constexpr int subBlock = 32;
    std::array<ZdfSvf, 2> highPass {};
    std::array<double, numPartials> phases {};
    double glide = 0.0;
    float sr = 48000.0f, amount = 0.0f, cutoffG = 0.001f;
};

// GROOVE's tempo pump, the sidechain duck of a four-on-the-floor kick: on every beat the gain falls over 4 ms
// and recovers, g = 1 - D env(p) with p the phase in the beat. The recovery morphs from a half cosine (smooth)
// to a normalised exponential exp(-t / tau), tau = k * beat (hard), shifted so it reaches 0 exactly at the
// next beat, so the curve is continuous everywhere. Follows the host's beat position while it plays and
// free-runs at its tempo otherwise. One gain for both channels: linear, so the mono lows stay mono.
// The power it takes is known exactly (mean of g^2 over a beat), so the loudness trim compensates without
// measuring, and never chases the pump itself.
class TempoPump
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        smoothCoef = 1.0f - std::exp (-1.0f / (0.0015f * sr));
        reset();
    }

    void reset()
    {
        phase = 0.0;
        gain = 1.0f;
        meanSquare = 1.0f;
        settled = true;
    }

    // depthDb: the dip at its deepest; hardness 0..1: cosine to exponential; k: tau in beats.
    // ppq: the host's beat position at the block start, or a negative value when the transport is stopped.
    bool process (float* left, float* right, int numSamples, float depthDb, float hardness, float k,
                  double bpm, double ppq) noexcept
    {
        const float depth = 1.0f - juce::Decibels::decibelsToGain (-juce::jmax (0.0f, depthDb));
        const double beatsPerSample = juce::jlimit (20.0, 400.0, bpm) / 60.0 / sr;
        if (ppq >= 0.0)
            phase = ppq - std::floor (ppq);
        if (depth < 1.0e-5f && settled)
        {
            phase = std::fmod (phase + beatsPerSample * numSamples, 1.0);
            meanSquare = 1.0f;
            return false;
        }
        const Shape sh = makeShape (hardness, k, (float) (1.0 / (beatsPerSample * sr)));
        float sumSquare = 0.0f;
        constexpr int points = 128;
        for (int i = 0; i < points; ++i)
            sumSquare += juce::square (1.0f - depth * envelope ((i + 0.5f) / points, sh));
        meanSquare = sumSquare / points;

        settled = depth < 1.0e-5f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float target = 1.0f - depth * envelope ((float) phase, sh);
            gain += (target - gain) * smoothCoef;   // smooths a jump in the host's position
            settled = settled && std::abs (gain - 1.0f) < 1.0e-5f;
            left[i] *= gain;
            right[i] *= gain;
            phase += beatsPerSample;
            if (phase >= 1.0)
                phase -= 1.0;
        }
        return true;
    }

    // The fraction of power the pump keeps, mean g^2 over a beat (1 when off)
    float powerRatio() const noexcept { return meanSquare; }

private:
    struct Shape { float attack, hardness, invTau, floor; };

    static Shape makeShape (float hardness, float k, float beatSeconds) noexcept
    {
        const float attack = juce::jmin (0.1f, 0.004f / beatSeconds);
        const float invTau = 1.0f / juce::jmax (0.02f, k);   // in beats
        return { attack, juce::jlimit (0.0f, 1.0f, hardness), invTau, std::exp (-(1.0f - attack) * invTau) };
    }

    static float envelope (float p, const Shape& s) noexcept
    {
        if (p < s.attack)
            return 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * p / s.attack));
        const float t = p - s.attack;
        const float smooth = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * t / (1.0f - s.attack)));
        const float hard = (std::exp (-t * s.invTau) - s.floor) / (1.0f - s.floor);
        return smooth + s.hardness * (hard - smooth);
    }

    float sr = 48000.0f, smoothCoef = 0.03f, gain = 1.0f, meanSquare = 1.0f;
    double phase = 0.0;
    bool settled = true;
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
        sectionIn.prepare (sampleRate * 4.0);
        sectionOut.prepare (sampleRate * 4.0);
        upRate = (float) (sampleRate * 4.0);
        exciterSourceG = tptGain (3000.0f, upRate);
        exciterOutputG = tptGain (6000.0f, upRate);
        exciterEnvCoef = 1.0f - std::exp (-1.0f / (0.005f * upRate));
    }

    void reset()
    {
        if (oversampler != nullptr)
            oversampler->reset();
        pressureState = {};
        exciters = {};
        sectionIn.reset();
        sectionOut.reset();
    }

    // P_in / P_out of the knob-driven stages in here (PRESSURE's saturation and clipping, SHINE's exciter),
    // K-weighted, for the loudness trim; 1 while they are idle
    double knobStageRatio() const noexcept { return wasActive ? harmony::stageRatio (sectionIn, sectionOut) : 1.0; }

    int getLatencySamples() const
    {
        return oversampler != nullptr ? juce::roundToInt (oversampler->getLatencyInSamples()) : 0;
    }

    // pressure: PRESSURE's glided amount (0..1); its saturation and clipping shares run here, oversampled,
    // their drive set against the mid's short-term level here, so a quiet patch is driven as hard as a loud one.
    // excite: SHINE's exciter mix (0..1).
    // trim: the Harmony Engine's loudness trim, applied after these stages and before the safe-output curve.
    // The stages so see the signal untrimmed: what they take doesn't depend on the trim, so the trim is a
    // straight measurement, not a loop that has to converge, and the safe curve still has the last word.
    void process (juce::AudioBuffer<float>& buffer, bool enabled, float pressure = 0.0f, float excite = 0.0f, float trim = 1.0f)
    {
        gainTarget = enabled ? 1.0f : 0.0f;
        const float g = Pressure::totalReductionDb (pressure);
        const bool pressing = pressure > 1.0e-4f;
        if (pressing && ! wasPressing)
        {
            // Seed the level from this block, so the drive never starts from silence
            double sum = 0.0;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                sum += juce::square (0.5 * (buffer.getSample (0, i) + buffer.getSample (1, i)));
            midPower = (float) (sum / juce::jmax (1, buffer.getNumSamples()));
        }
        wasPressing = pressing;
        const float shiftDb = juce::jlimit (-30.0f, 12.0f, 10.0f * std::log10 (midPower + 1.0e-12f) - clipReferenceDb);
        const float satDrive = juce::Decibels::decibelsToGain (0.75f * g - shiftDb), satWet = juce::jmin (0.8f, pressure);
        const float clipDrive = juce::Decibels::decibelsToGain (0.375f * g - shiftDb);
        const bool exciting = excite > 1.0e-4f || exciteMix > 1.0e-4f;
        const bool active = pressing || exciting;
        if (active != wasActive)
        {
            sectionIn.reset();
            sectionOut.reset();
            wasActive = active;
        }
        if (! pressing)
            pressureState = {};
        if (! exciting)
            exciters = {};
        const int numSamples = buffer.getNumSamples();
        for (int start = 0; start < numSamples; start += maxBlock)
        {
            const int n = juce::jmin (maxBlock, numSamples - start);
            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) start, (size_t) n);
            auto up = oversampler->processSamplesUp (block);
            auto* l = up.getChannelPointer (0);
            auto* r = up.getChannelPointer (1);
            const int upN = (int) up.getNumSamples();
            if (active)
                sectionIn.process (l, r, upN);
            if (exciting)
            {
                const float from = exciteMix, step = (excite - exciteMix) / (float) upN;
                for (int i = 0; i < upN; ++i)
                {
                    const float mix = from + step * (float) (i + 1);
                    l[i] += mix * exciters[0].process (l[i], *this);
                    r[i] += mix * exciters[1].process (r[i], *this);
                }
                exciteMix = excite;
            }
            if (pressing)
            {
                // Mid only: a nonlinearity on L and R would intermodulate the side with the mid and leave side
                // energy in the mono lows; the side passes untouched
                double sum = 0.0;
                for (int i = 0; i < upN; ++i)
                {
                    const float m = 0.5f * (l[i] + r[i]);
                    sum += (double) m * m;
                    const float mid = pressureSaturate (m, satDrive, satWet, clipDrive, pressureState);
                    const float side = 0.5f * (l[i] - r[i]);
                    l[i] = mid + side;
                    r[i] = mid - side;
                }
                midPower += ((float) (sum / upN) - midPower) * (1.0f - std::exp (-(float) upN / (0.4f * upRate)));
            }
            if (active)
                sectionOut.process (l, r, upN);
            {
                const float from = trimGain, step = (trim - trimGain) / (float) upN;
                if (std::abs (step) > 0.0f || std::abs (trimGain - 1.0f) > 1.0e-6f)
                    for (int i = 0; i < upN; ++i)
                    {
                        const float t = from + step * (float) (i + 1);
                        l[i] *= t;
                        r[i] *= t;
                    }
                trimGain = trim;
            }
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                if (gain < gainTarget)      gain = std::min (gainTarget, gain + gainStep);
                else if (gain > gainTarget) gain = std::max (gainTarget, gain - gainStep);
                l[i] += gain * (shape (l[i]) - l[i]);
                r[i] += gain * (shape (r[i]) - r[i]);
            }
            oversampler->processSamplesDown (block);
            if (active)
            {
                sectionIn.update();
                sectionOut.update();
            }
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
    struct PressureState { AdaaTanh saturator, clipper; };

    // SHINE's exciter: the band above 3 kHz, normalised by its own envelope, through the Chebyshev polynomials
    // T2 and T3 (a unit sine becomes exactly its 2nd and 3rd harmonic), scaled back by the envelope, then
    // high-passed at 6 kHz. A cubic only triples the bandwidth (24 kHz becomes 72 kHz), which stays under the
    // 96 kHz Nyquist of this 4x loop, so the exciter adds air without any aliasing.
    struct Exciter
    {
        ZdfSvf source, output;
        float envelope = 0.0f;

        float process (float x, const TruePeakClipper& c) noexcept
        {
            const float h = source.process (x, c.exciterSourceG, juce::MathConstants<float>::sqrt2).high;
            envelope += (2.0f * h * h - envelope) * c.exciterEnvCoef;
            const float amplitude = std::sqrt (envelope) + 1.0e-6f;
            const float u = juce::jlimit (-1.5f, 1.5f, h / amplitude);
            const float harmonics = amplitude * (0.7f * (2.0f * u * u - 1.0f) + 0.5f * (4.0f * u * u * u - 3.0f * u));
            return output.process (harmonics, c.exciterOutputG, juce::MathConstants<float>::sqrt2).high;
        }
    };

    // PRESSURE's saturation (parallel tanh, level-matched to the dry for small signals) and soft clipper (drive
    // in, the same gain out, linear below -6 dBFS then a tanh bend into 0 dBFS), so they shave peaks without
    // moving the level
    static float pressureSaturate (float x, float satDrive, float satWet, float clipDrive, PressureState& st) noexcept
    {
        const float sat = st.saturator.process (x * satDrive) / satDrive;
        const float y = (x + satWet * (sat - x)) * clipDrive;
        constexpr float knee = 0.5f, span = 0.5f;
        const float a = std::abs (y);
        const float bent = knee + span * st.clipper.process (juce::jmax (0.0f, a - knee) / span);
        return (a <= knee ? y : std::copysign (bent, y)) / clipDrive;
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    PressureState pressureState {};
    std::array<Exciter, 2> exciters {};
    float exciterSourceG = 0.1f, exciterOutputG = 0.2f, exciterEnvCoef = 0.01f, exciteMix = 0.0f;
    harmony::KPowerMeter sectionIn, sectionOut;   // at the oversampled rate
    bool wasActive = false, wasPressing = false;
    static constexpr float clipReferenceDb = -11.0f;   // the mid's power the in-loop stages are tuned at
    float midPower = 0.0f, trimGain = 1.0f, upRate = 192000.0f;
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
