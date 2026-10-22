#pragma once
#include "Engine.h"
#include "Glide.h"
#include "Filter.h"
#include "Simd.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ps
{
// Genome: one voice whose architecture morphs continuously through five synth designs. Architecture (MORPH's
// slot) walks Jupiter VA -> FM -> CZ phase distortion -> additive -> modal body; between two designs both run
// and crossfade, everywhere else only one runs.
//
//   Jupiter: a saw/square core plus two oscillators detuned either side (they fade in with Detune).
//   FM: a carrier at the core pitch, modulated by an operator at FM Ratio (with its own decaying index) and by a
//   self-feedback operator (DX two-sample averaging). The index is capped from Carson's rule so the sidebands
//   never reach the frequencies that would fold back audibly, and feedback stays below its chaotic range.
//   Phase Dist: CZ-style cosine read through a two-slope phase (C1 everywhere, the knee kept >= 2.5 samples),
//   blending into the windowed resonance wave, whose corner at the wrap gets a 4-point BLAMP.
//   Additive: 32 harmonics with a spectral tilt and an odd/even balance, faded out before 0.45 fs.
//   Body: 8 resonant modes (strings to a free bar) struck at note-on and driven by the additive signal.
//
// Everything is locked to one master phase: the core, the carrier, the phase-distortion phase and every
// harmonic derive from it, so a crossfade never combs or cancels and a held note never wanders. A crossfade's
// power is restored by a per-voice gain from the two designs' measured powers (30 ms, 0.5 to 2), so the level
// stays steady through the whole sweep. Jupiter, FM and Phase Dist run at 2x through one half-band decimator;
// additive and body run at 1x, read at the decimator's delay so they stay aligned with the 2x designs.
//
// v1 is mono. Stereo, a spectral bridge between designs and unison are later.
class GenomeEngine final : public Engine
{
public:
    enum Slot
    {
        architecture, shape, detune, fmIndex, fmRatio, fmFeedback, fmEnv, fmDecay, pdAmount, pdRes, tilt, oddEven,
        bodyDecay, bodyTone, strike, tone, numUsedSlots
    };

    enum Node { jupiter, fm, phaseDist, additive, body, numNodes };

    static constexpr int numPartials = 32, numModes = 8;

    const EngineInfo& info() const override
    {
        static const EngineInfo i { "genome", "Genome", 16, makeSlots() };
        return i;
    }

    void prepare (double sampleRate, int, int numVoices) override
    {
        sr = (float) sampleRate;
        voices.assign ((size_t) numVoices, {});
    }

    void reset() override
    {
        for (auto& v : voices)
            resetVoice (v);
    }

    void noteOn (int index, const NoteEvent& e) override
    {
        auto& v = voices[(size_t) index];
        const bool fresh = ! v.active;
        if (fresh)
        {
            resetVoice (v);
            v.pitch.jumpTo (e.glideSeconds > 0.0 && e.glideFrom >= 0.0f ? e.glideFrom : (float) e.note);
            v.smooth = target;
            v.fixedPhase = e.fixedPhase;
            v.active = true;
        }
        if (fresh || ! e.legato)
        {
            v.velocity = e.velocity;
            v.fmEnvAttack = true;
            v.strikePending = true;
        }
        v.pitch.glideTo ((float) e.note, e.glideSeconds, sr);
    }

    void noteOff (int) override {}
    void endVoice (int index) override { resetVoice (voices[(size_t) index]); }
    void setTune (float semitones) override { tune = semitones; }

    void setSlot (int slot, float value) override
    {
        if (slot >= 0 && slot < numUsedSlots)
            target[(size_t) slot] = juce::jlimit (0.0f, 1.0f, value);
    }

    void render (int index, float* left, float* right, int numSamples) override
    {
        auto& v = voices[(size_t) index];
        if (! v.active)
            return;
        v.pitchBend = voiceBend (index);
        for (int pos = 0; pos < numSamples; pos += subBlock)
            renderSubBlock (v, left + pos, right + pos, juce::jmin (subBlock, numSamples - pos));
    }

    // ---- Slot mappings (shared with the slot text and the tests) ------------------------------------------
    static float detuneCents (float v) noexcept   { return 25.0f * v; }
    static float fmIndexOf (float v) noexcept     { return 6.0f * v * v; }          // radians
    static float fmRatioOf (float v) noexcept     { return 0.5f * std::pow (16.0f, v); }
    static float feedbackBeta (float v) noexcept  { return 1.3f * v; }              // DX-style, stable to ~1.5
    static float decayMs (float v) noexcept       { return 5.0f * std::pow (400.0f, v); }
    static float tiltPower (float v) noexcept     { return 2.0f * v; }              // harmonic n at n^-p
    static float bodyT60 (float v) noexcept       { return 0.05f * std::pow (200.0f, v); }
    static float toneHz (float v) noexcept        { return 20.0f * std::pow (1000.0f, v); }
    static float nodeValue (int node) noexcept    { return (float) node / (float) (numNodes - 1); }

    // sin(2 pi x) for x in cycles: quarter-wave reduction and a 9th-order Taylor series (about -109 dB)
    static float fastSin (float x) noexcept
    {
        x -= std::floor (x + 0.5f);
        x = x > 0.25f ? 0.5f - x : (x < -0.25f ? -0.5f - x : x);
        const float t = 6.28318531f * x, t2 = t * t;
        return t * (1.0f + t2 * (-1.0f / 6.0f + t2 * (1.0f / 120.0f + t2 * (-1.0f / 5040.0f + t2 * (1.0f / 362880.0f)))));
    }

private:
    static constexpr int subBlock = 16;
    static constexpr float voiceLevel = 0.36f;      // the Dirt engine's level
    static constexpr float sawRms = 0.57735027f;    // every design is scaled to a saw's RMS
    static constexpr float sineToSaw = 0.81649658f; // sine RMS -> saw RMS
    static constexpr float twoPi = 6.28318531f;
    // The half-band decimator's delay in output samples: the 1x designs read the master phase this far back
    static constexpr double decimatorLag = 15.5;

    struct Voice
    {
        bool active = false, fixedPhase = false;
        float velocity = 1.0f, pitchBend = 0.0f;
        PitchGlide pitch;
        std::array<float, numUsedSlots> smooth {};
        std::array<bool, numNodes> on {};
        double phase = 0.0;                 // the master phase, in cycles
        std::array<double, 2> side {};      // Jupiter's detuned oscillators
        double mod = 0.0;                   // FM modulator
        float fb1 = 0.0f, fb2 = 0.0f;       // feedback operator's last two outputs
        float fmEnvLevel = 0.0f;
        bool fmEnvAttack = false, strikePending = false;
        int tail2x = 0;
        HalfbandDecimator decimator;
        std::array<float, numModes> zr {}, zi {};
        std::array<float, numNodes> power {};               // each design's power, as mixed
        float mixPower = 0.0f, comp = 1.0f;
        float fmRawPower = 0.0f, fmGain = 1.0f;              // FM before its level correction
        std::array<double, numNodes> periodSum {};           // this pitch period's sums of squares
        std::array<int, numNodes> periodCount {};
        double periodMix = 0.0, periodFmRaw = 0.0;
        int periodLength = 0;
        float dcX = 0.0f, dcY = 0.0f;
        ZdfSvf toneFilter;
    };

    static std::vector<SlotInfo> makeSlots()
    {
        auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        auto time = [] (float ms) { return ms < 1000.0f ? juce::String (ms, ms < 10.0f ? 1 : 0) + " ms" : juce::String (ms * 0.001f, 2) + " s"; };
        std::vector<SlotInfo> s ((size_t) numUsedSlots);
        s[architecture] = { "Architecture", 0.0f, [] (float v)
                            {
                                static const char* names[] = { "Jupiter", "FM", "Phase Dist", "Additive", "Body" };
                                const float x = v * (float) (numNodes - 1);
                                const int k = juce::jmin (numNodes - 2, (int) x);
                                const float w = x - (float) k;
                                if (w < 0.01f) return juce::String (names[k]);
                                if (w > 0.99f) return juce::String (names[k + 1]);
                                return juce::String (names[k]) + " > " + names[k + 1] + " " + juce::String (juce::roundToInt (w * 100.0f)) + " %";
                            } };
        s[shape]        = { "Shape", 0.0f, [] (float v)
                            {
                                if (v < 0.01f) return juce::String ("Saw");
                                if (v > 0.99f) return juce::String ("Square");
                                return "Saw > Sqr " + juce::String (juce::roundToInt (v * 100.0f)) + " %";
                            } };
        s[detune]       = { "Detune", 0.3f, [] (float v) { return juce::String (detuneCents (v), 1) + " ct"; } };
        s[fmIndex]      = { "FM Index", 0.35f, [] (float v) { return juce::String (fmIndexOf (v), 2); } };
        s[fmRatio]      = { "FM Ratio", 0.25f, [] (float v) { return juce::String (fmRatioOf (v), 2); } };
        s[fmFeedback]   = { "FM Feedback", 0.3f, percent };
        s[fmEnv]        = { "FM Env", 0.25f, percent };
        s[fmDecay]      = { "FM Decay", 0.5f, [time] (float v) { return time (decayMs (v)); } };
        s[pdAmount]     = { "PD Amount", 0.7f, percent };
        s[pdRes]        = { "PD Res", 0.0f, percent };
        s[tilt]         = { "Tilt", 0.5f, [] (float v) { return "-" + juce::String (6.02f * tiltPower (v), 1) + " dB/oct"; } };
        s[oddEven]      = { "Odd/Even", 0.5f, [] (float v)
                            {
                                if (std::abs (v - 0.5f) < 0.01f) return juce::String ("All");
                                return v < 0.5f ? "Odd " + juce::String (juce::roundToInt ((0.5f - v) * 200.0f)) + " %"
                                                : "Even " + juce::String (juce::roundToInt ((v - 0.5f) * 200.0f)) + " %";
                            } };
        s[bodyDecay]    = { "Body Decay", 0.5f, [time] (float v) { return time (1000.0f * bodyT60 (v)); } };
        s[bodyTone]     = { "Body Tone", 0.0f, [] (float v)
                            {
                                if (v < 0.01f) return juce::String ("String");
                                if (v > 0.99f) return juce::String ("Bar");
                                return "String > Bar " + juce::String (juce::roundToInt (v * 100.0f)) + " %";
                            } };
        s[strike]       = { "Strike", 0.5f, percent };
        s[tone]         = { "Tone", 1.0f, [] (float v)
                            {
                                if (v > 0.995f) return juce::String ("Open");
                                const float f = toneHz (v);
                                return f < 1000.0f ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f * 0.001f, 2) + " kHz";
                            } };
        s[architecture].role = SlotRole::position;
        s[detune].role = SlotRole::detune;
        s[fmIndex].role = SlotRole::crossMod;
        s[tone].role = SlotRole::brightness;
        return s;
    }

    void resetVoice (Voice& v) noexcept
    {
        const auto smooth = v.smooth;
        v = Voice {};
        v.smooth = smooth;
    }

    static float nodeWeight (float x, int node) noexcept { return juce::jmax (0.0f, 1.0f - std::abs (x - (float) node)); }

    // 4-point BLEP and BLAMP residuals from the integrated cubic B-spline: about 25 dB less aliasing than the
    // 2-point PolyBLEP at 2x (a saw at 2.1 kHz: -84 dB). y is the distance to the corner in samples (0..2).
    static float blepResidual (float y) noexcept
    {
        if (y >= 2.0f) return 0.0f;
        if (y >= 1.0f) { const float u = 2.0f - y, u2 = u * u; return u2 * u2 * (1.0f / 24.0f); }
        const float y2 = y * y;
        return 0.5f - (4.0f * y - 2.0f * y2 * y + 0.75f * y2 * y2) * (1.0f / 6.0f);
    }

    static float blampResidual (float y) noexcept
    {
        if (y >= 2.0f) return 0.0f;
        if (y >= 1.0f) { const float u = 2.0f - y, u2 = u * u; return u2 * u2 * u * (1.0f / 120.0f); }
        const float y2 = y * y;
        return (7.0f / 30.0f) - 0.5f * y + y2 * (1.0f / 3.0f) - y2 * y2 * (1.0f / 12.0f) + y2 * y2 * y * (1.0f / 40.0f);
    }

    // Correction for a unit upward step at phase 0 (needs dt <= 0.25, so the two sides never overlap)
    static float stepAt (float t, float invDt) noexcept
    {
        return blepResidual ((1.0f - t) * invDt) - blepResidual (t * invDt);
    }

    // Correction for a unit slope change (per sample) at phase 0
    static float cornerAt (float t, float invDt) noexcept
    {
        return blampResidual ((1.0f - t) * invDt) + blampResidual (t * invDt);
    }

    static float saw (float t, float invDt) noexcept { return 2.0f * t - 1.0f - 2.0f * stepAt (t, invDt); }

    static float square (float t, float invDt) noexcept
    {
        const float pw = t < 0.5f ? t + 0.5f : t - 0.5f;
        return (t < 0.5f ? 1.0f : -1.0f) + 2.0f * stepAt (t, invDt) - 2.0f * stepAt (pw, invDt);
    }

    // Band-limited saw -> square (in phase, power-matched), as in the Dirt engine
    struct ShapeMix
    {
        float sawGain = 1.0f, squareGain = 0.0f, norm = 1.0f;
        explicit ShapeMix (float s) noexcept
            : sawGain (1.0f - s), squareGain (s * sawRms),
              norm (1.0f / std::sqrt ((1.0f - s) * (1.0f - s) + s * s + 1.7320508f * s * (1.0f - s))) {}

        // The square inverted, so it rises with the saw's fundamental
        float operator() (float t, float dt) const noexcept
        {
            const float invDt = 1.0f / dt;
            float x = sawGain * saw (t, invDt);
            if (squareGain > 0.0f)
                x -= squareGain * square (t, invDt);
            return x * norm;
        }
    };

    static double wrap (double p) noexcept { return p - std::floor (p); }

    void renderSubBlock (Voice& v, float* outL, float* outR, int n) noexcept
    {
        const float coef = 1.0f - std::exp (-(float) n / (0.02f * sr));
        const auto previous = v.smooth;
        for (size_t i = 0; i < v.smooth.size(); ++i)
            v.smooth[i] += (target[i] - v.smooth[i]) * coef;

        // Pitch: exact, from the glide, tune and bend only
        alignas (32) std::array<float, subBlock> pitchAt {};
        for (int j = 0; j < n; ++j)
            pitchAt[(size_t) j] = (v.pitch.isGliding() ? v.pitch.next() : v.pitch.getCurrent()) + tune + v.pitchBend;
        const bool gliding = pitchAt[0] != pitchAt[(size_t) (n - 1)];
        auto incrementAt = [&] (int j) { return 440.0f * fastExp2 ((pitchAt[(size_t) j] - 69.0f) / 12.0f) / sr; };
        const float dtFirst = incrementAt (0), dtLast = gliding ? incrementAt (n - 1) : dtFirst;
        const float dtMean = 0.5f * (dtFirst + dtLast), dtMax = juce::jmax (dtFirst, dtLast);

        // Architecture: triangular weights over the five designs, interpolated per sample
        const float x0 = previous[architecture] * (float) (numNodes - 1), x1 = v.smooth[architecture] * (float) (numNodes - 1);
        const float xLo = juce::jmin (x0, x1), xHi = juce::jmax (x0, x1);
        std::array<bool, numNodes> on {};
        for (int k = 0; k < numNodes; ++k)
            on[(size_t) k] = (float) (k - 1) < xHi && (float) (k + 1) > xLo;
        initialiseNodes (v, on);

        const bool run2x = on[jupiter] || on[fm] || on[phaseDist];
        if (run2x)
            v.tail2x = 2 * HalfbandDecimator::sideTaps;
        const bool decimate = run2x || v.tail2x > 0;
        const bool runAdditive = on[additive] || on[body];

        // ---- 1x designs first: they read the master phase at the decimator's delay -----------------------
        alignas (32) std::array<float, subBlock> add {}, bod {};
        if (runAdditive)
            renderAdditive (v, add.data(), n, dtMean);
        if (on[body])
            renderBody (v, add.data(), bod.data(), n, dtMean);
        v.strikePending = false;

        // ---- 2x designs --------------------------------------------------------------------------------
        const float sr2 = 2.0f * sr;
        const float dtMax2 = 0.5f * dtMax, f0Max = dtMax * sr;
        const ShapeMix shapeMix (v.smooth[shape]);
        const float cents = detuneCents (v.smooth[detune]);
        const float sideGain = juce::jmin (1.0f, cents * 0.5f);
        const float jupiterNorm = 1.0f / std::sqrt (1.0f + 2.0f * sideGain * sideGain);
        const float sideUp = std::exp2 (cents / 1200.0f), sideDown = 1.0f / sideUp;

        // FM's level: a phase-modulated sine's power depends on the index, ratio and feedback, so FM is held at a
        // saw's power from its measured power (pitch-synchronous, below)
        const float fmGainStart = v.fmGain;
        {
            const float wantedGain = v.fmRawPower > 0.0f ? juce::jlimit (0.5f, 2.0f, sawRms / std::sqrt (v.fmRawPower)) : 1.0f;
            v.fmGain += (wantedGain - v.fmGain) * (1.0f - std::exp (-(float) n / (0.01f * sr)));
        }

        // FM: Carson's-rule caps. Sidebands up to 0.7 * fs2 fold back above 0.6 fs, into the decimator's stopband.
        const float ratio = fmRatioOf (v.smooth[fmRatio]);
        const float beta = feedbackBeta (v.smooth[fmFeedback]);
        const float limit = 0.7f * sr2 / juce::jmax (1.0f, f0Max);
        const float fbSpread = 1.0f + 4.0f * beta;
        const float fbIndex = juce::jmin (2.0f * v.smooth[fmFeedback], juce::jmax (0.0f, (limit - 1.0f) / fbSpread - 1.0f));
        const float fbReach = fbIndex > 0.0f ? (fbIndex + 1.0f) * fbSpread : 0.0f;
        const float indexCap = juce::jmax (0.0f, (limit - 1.0f - fbReach) / ratio - 1.0f);
        const float index0 = fmIndexOf (v.smooth[fmIndex]);
        const float envDepth = 8.0f * v.smooth[fmEnv] * (0.5f + 0.5f * v.velocity);
        const float envDecay2 = std::exp (-1.0f / (0.001f * decayMs (v.smooth[fmDecay]) * sr2));
        const float envAttack2 = 1.0f - std::exp (-1.0f / (0.001f * sr2));
        if (! on[fm])
        {
            // The index envelope keeps note time while FM is out of play
            if (v.fmEnvAttack)
                v.fmEnvLevel = 1.0f, v.fmEnvAttack = false;
            else
                v.fmEnvLevel *= std::pow (envDecay2, 2.0f * (float) n);
        }

        // Phase distortion: the knee kept >= 2.5 samples; the resonance kept under 0.4 of the 2x Nyquist
        const float knee = juce::jlimit (juce::jmin (0.5f, 2.5f * dtMax2), 0.5f, 0.5f - 0.49f * v.smooth[pdAmount]);
        const float resAmount = v.smooth[pdRes];
        const float resMix = juce::jmin (1.0f, resAmount * 10.0f);
        const float resK = juce::jmin (1.0f + 15.0f * resAmount, juce::jmax (1.0f, 0.2f / juce::jmax (1.0e-6f, dtMax2)));
        const float resWrapSlope = twoPi * resK + std::sin (twoPi * resK);
        const float pdDry = std::cos (resMix * 1.5707963f), pdWet = std::sin (resMix * 1.5707963f) * 1.7320508f;

        alignas (32) std::array<float, subBlock> out {};
        float dt = dtFirst;
        for (int j = 0; j < n; ++j)
        {
            const float frac = (float) (j + 1) / (float) n;
            const float x = x0 + (x1 - x0) * frac;
            if (gliding)
                dt = incrementAt (j);
            float y = 0.0f;
            const double phaseBefore = v.phase;
            std::array<float, numNodes> sq {};
            float fmRawSq = 0.0f;
            if (decimate)
            {
                const float dt2 = 0.5f * dt, invDt2 = 1.0f / dt2;
                const float wJ = nodeWeight (x, jupiter), wF = nodeWeight (x, fm), wP = nodeWeight (x, phaseDist);
                std::array<float, 2> twice {};
                for (int h = 0; h < 2; ++h)
                {
                    v.phase = wrap (v.phase + dt2);
                    const float t = (float) v.phase;
                    float s = 0.0f;
                    if (on[jupiter])
                    {
                        float a = shapeMix (t, dt2);
                        if (sideGain > 0.0f)
                        {
                            v.side[0] = wrap (v.side[0] + dt2 * sideUp);
                            v.side[1] = wrap (v.side[1] + dt2 * sideDown);
                            a += sideGain * (shapeMix ((float) v.side[0], dt2 * sideUp) + shapeMix ((float) v.side[1], dt2 * sideDown));
                        }
                        a *= jupiterNorm;
                        sq[jupiter] += 0.5f * a * a;
                        s += wJ * a;
                    }
                    if (on[fm])
                    {
                        if (v.fmEnvAttack)
                        {
                            v.fmEnvLevel += (1.0f - v.fmEnvLevel) * envAttack2;
                            v.fmEnvAttack = v.fmEnvLevel < 0.999f;
                        }
                        else
                            v.fmEnvLevel *= envDecay2;
                        const float fbOut = fastSin (t + beta * 0.5f * (v.fb1 + v.fb2) * (1.0f / twoPi));
                        v.fb2 = v.fb1;
                        v.fb1 = fbOut;
                        v.mod = wrap (v.mod + dt2 * ratio);
                        const float index = juce::jmin (indexCap, index0 + envDepth * v.fmEnvLevel);
                        const float raw = -sineToSaw * fastSin (t + (index * fastSin ((float) v.mod) + fbIndex * fbOut) * (1.0f / twoPi));
                        const float a = raw * (fmGainStart + (v.fmGain - fmGainStart) * frac);
                        fmRawSq += 0.5f * raw * raw;
                        sq[fm] += 0.5f * a * a;
                        s += wF * a;
                    }
                    if (on[phaseDist])
                    {
                        const float warped = t < knee ? 0.5f * t / knee : 0.5f + 0.5f * (t - knee) / (1.0f - knee);
                        float a = pdDry * fastSin (warped + 0.25f);
                        if (pdWet > 0.0f)
                            a += pdWet * ((1.0f - t) * fastSin (resK * t) + resWrapSlope * dt2 * cornerAt (t, invDt2));
                        a *= sineToSaw;
                        sq[phaseDist] += 0.5f * a * a;
                        s += wP * a;
                    }
                    twice[(size_t) h] = s;
                }
                y = v.decimator.process (twice[0], twice[1]);
            }
            else
                v.phase = wrap (v.phase + dt);

            if (on[additive])
            {
                const float a = add[(size_t) j];
                sq[additive] = a * a;
                y += nodeWeight (x, additive) * a;
            }
            if (on[body])
            {
                const float a = bod[(size_t) j];
                sq[body] = a * a;
                y += nodeWeight (x, body) * a;
            }
            out[(size_t) j] = y;

            for (int k = 0; k < numNodes; ++k)
                if (on[(size_t) k])
                {
                    v.periodSum[(size_t) k] += sq[(size_t) k];
                    ++v.periodCount[(size_t) k];
                }
            v.periodMix += y * y;
            v.periodFmRaw += fmRawSq;
            ++v.periodLength;
            if (v.phase < phaseBefore)
                closePeriod (v, on);
        }
        if (! run2x && v.tail2x > 0)
        {
            v.tail2x -= n;
            if (v.tail2x <= 0)
                v.decimator.reset();
        }

        // Crossfade power: the gain restores sum(w P) from the measured powers of the designs in play
        const float powerCoef = 1.0f - std::exp (-(float) n / (0.03f * sr));
        float compTarget = 1.0f;
        int inPlay = 0;
        float wanted = 0.0f;
        for (int k = 0; k < numNodes; ++k)
        {
            const float w = nodeWeight (x1, k);
            if (w > 0.0f)
            {
                ++inPlay;
                wanted += w * v.power[(size_t) k];
            }
        }
        if (inPlay > 1 && v.mixPower > 1.0e-9f)
            compTarget = juce::jlimit (0.5f, 2.0f, std::sqrt (wanted / v.mixPower));
        const float compStart = v.comp;
        v.comp += (compTarget - v.comp) * powerCoef;

        // DC block (5 Hz), Tone (12 dB low-pass, open at the top), level
        const float dcR = 1.0f - twoPi * 5.0f / sr;
        const float toneValue = v.smooth[tone];
        const bool toneOn = toneValue < 0.995f;
        const float toneWet = juce::jlimit (0.0f, 1.0f, (0.995f - toneValue) / 0.045f);
        const auto toneCoeffs = ZdfSvf::Coeffs::make (tptGain (toneHz (toneValue), sr), 1.41421356f);
        if (! toneOn)
            v.toneFilter.reset();
        for (int j = 0; j < n; ++j)
        {
            const float g = compStart + (v.comp - compStart) * (float) (j + 1) / (float) n;
            const float xin = out[(size_t) j] * g;
            const float dc = xin - v.dcX + dcR * v.dcY;
            v.dcX = xin;
            v.dcY = dc;
            float s = dc;
            if (toneOn)
                s += toneWet * (v.toneFilter.process (dc, toneCoeffs).low - dc);
            s *= voiceLevel;
            outL[j] += s;
            outR[j] += s;
        }
        v.on = on;
    }

    // A design entering play starts from rest, at the master phase; its crossfade weight is near zero then
    void initialiseNodes (Voice& v, const std::array<bool, numNodes>& on) noexcept
    {
        if (on[jupiter] && ! v.on[jupiter])
        {
            v.side[0] = wrap (v.phase + (v.fixedPhase ? 0.0 : 0.37));
            v.side[1] = wrap (v.phase + (v.fixedPhase ? 0.0 : 0.71));
        }
        if (on[fm] && ! v.on[fm])
        {
            v.mod = wrap (v.phase * (double) fmRatioOf (v.smooth[fmRatio]));
            v.fb1 = v.fb2 = 0.0f;
        }
        if (on[body] && ! v.on[body])
        {
            v.zr.fill (0.0f);
            v.zi.fill (0.0f);
        }
        for (int k = 0; k < numNodes; ++k)
            if (! on[(size_t) k])
            {
                v.power[(size_t) k] = 0.0f;
                v.periodSum[(size_t) k] = 0.0;
                v.periodCount[(size_t) k] = 0;
            }
        if (! on[fm])
        {
            v.fmRawPower = 0.0f;
            v.periodFmRaw = 0.0;
        }
    }

    // Powers are measured over whole pitch periods, so a steady note gives a steady reading (no ripple at the
    // pitch, which a running average would turn into amplitude modulation), then smoothed over 30 ms. A design
    // that was in play for less than half the period waits for the next one.
    void closePeriod (Voice& v, const std::array<bool, numNodes>& on) noexcept
    {
        const int length = v.periodLength;
        const float coef = 1.0f - std::exp (-(float) length / (0.03f * sr));
        auto track = [coef] (float& tracked, double sum, int count)
        {
            const auto p = (float) (sum / count);
            tracked = tracked > 0.0f ? tracked + (p - tracked) * coef : p;
        };
        for (int k = 0; k < numNodes; ++k)
        {
            const int count = v.periodCount[(size_t) k];
            if (on[(size_t) k] && 2 * count >= length && count > 0)
            {
                track (v.power[(size_t) k], v.periodSum[(size_t) k], count);
                if (k == fm && v.periodFmRaw > 0.0)
                    track (v.fmRawPower, v.periodFmRaw, count);
            }
            v.periodSum[(size_t) k] = 0.0;
            v.periodCount[(size_t) k] = 0;
        }
        if (length > 0)
            track (v.mixPower, v.periodMix, length);
        v.periodMix = v.periodFmRaw = 0.0;
        v.periodLength = 0;
    }

    // Harmonics read at the decimator's delay, so they line up with the 2x designs' output. Each block starts
    // from the exact master phase (powers of one complex exponential, in double) and rotates per sample.
    void renderAdditive (const Voice& v, float* out, int n, float dt) const noexcept
    {
        const int count = juce::jlimit (1, numPartials, (int) (0.45f / juce::jmax (1.0e-6f, dt)));
        alignas (32) std::array<float, numPartials> re {}, im {}, cr {}, ci {}, amp {};
        const float p = tiltPower (v.smooth[tilt]);
        const float oe = v.smooth[oddEven];
        const float evenGain = juce::jmin (1.0f, 2.0f * oe), oddGain = juce::jmin (1.0f, 2.0f * (1.0f - oe));
        float power = 0.0f;
        const double start = wrap (v.phase - decimatorLag * (double) dt);
        const double a0 = juce::MathConstants<double>::twoPi * start, w0 = juce::MathConstants<double>::twoPi * (double) dt;
        const double br = std::cos (a0), bi = std::sin (a0), rr = std::cos (w0), ri = std::sin (w0);
        double pr = 1.0, pi = 0.0, qr = 1.0, qi = 0.0;
        for (int k = 0; k < count; ++k)
        {
            const double npr = pr * br - pi * bi, npi = pr * bi + pi * br;
            pr = npr; pi = npi;
            const double nqr = qr * rr - qi * ri, nqi = qr * ri + qi * rr;
            qr = nqr; qi = nqi;
            const int h = k + 1;
            const float fade = juce::jlimit (0.0f, 1.0f, (0.45f - (float) h * dt) / 0.05f);
            const float shapeGain = h == 1 ? 1.0f : (h % 2 == 0 ? evenGain : oddGain);
            const float a = fastExp2 (-p * std::log2 ((float) h)) * shapeGain * fade;
            amp[(size_t) k] = a;
            power += 0.5f * a * a;
            re[(size_t) k] = (float) pr;
            im[(size_t) k] = (float) pi;
            cr[(size_t) k] = (float) qr;
            ci[(size_t) k] = (float) qi;
        }
        // Rising-saw polarity (-sin), at a saw's RMS
        const float norm = power > 1.0e-12f ? -sawRms / std::sqrt (power) : 0.0f;
        for (auto& a : amp)
            a *= norm;

        using simd::F;
        const int lanes = (count + F::width - 1) / F::width * F::width;
        for (int j = 0; j < n; ++j)
        {
            F acc (0.0f);
            for (int k = 0; k < lanes; k += F::width)
            {
                const F r = F::load (&re[(size_t) k]), i = F::load (&im[(size_t) k]);
                const F c = F::load (&cr[(size_t) k]), s = F::load (&ci[(size_t) k]);
                const F nr = r * c - i * s, ni = r * s + i * c;
                acc = acc + ni * F::load (&amp[(size_t) k]);
                nr.store (&re[(size_t) k]);
                ni.store (&im[(size_t) k]);
            }
            out[j] = simd::sum (acc);
        }
    }

    // Free-free bar mode ratios (Fletcher & Rossing); Body Tone moves the modes from harmonic to these
    static constexpr std::array<float, numModes> barRatios { 1.0f, 2.756f, 5.404f, 8.933f, 13.344f, 18.638f, 24.814f, 31.873f };

    // Complex one-pole modes: z <- r e^{iw} z + (1 - r) x passes x at the mode's frequency at unity (output
    // 2 Re z). The strike sets each mode ringing from zero (a sine onset), so it never clicks.
    void renderBody (Voice& v, const float* in, float* out, int n, float dt) const noexcept
    {
        std::array<float, numModes> pr {}, pi {}, g {};
        std::array<bool, numModes> live {};
        const float t60 = bodyT60 (v.smooth[bodyDecay]), toneMix = v.smooth[bodyTone];
        float strikePower = 0.0f;
        std::array<float, numModes> strikeAmp {};
        for (int k = 0; k < numModes; ++k)
        {
            const float ratio = (float) (k + 1) + ((barRatios[(size_t) k]) - (float) (k + 1)) * toneMix;
            const float f = ratio * dt;
            if (f >= 0.45f)
                continue;
            live[(size_t) k] = true;
            const float r = std::exp (-6.90775528f / (t60 / std::sqrt (ratio) * sr));
            pr[(size_t) k] = r * std::cos (twoPi * f);
            pi[(size_t) k] = r * std::sin (twoPi * f);
            g[(size_t) k] = 1.0f - r;
            strikeAmp[(size_t) k] = 1.0f / (float) (k + 1);
            strikePower += 0.5f * strikeAmp[(size_t) k] * strikeAmp[(size_t) k];
        }
        if (v.strikePending && strikePower > 0.0f)
        {
            // About twice a saw's RMS at full Strike and velocity
            const float scale = 2.0f * sawRms * v.smooth[strike] * v.velocity / std::sqrt (strikePower);
            for (int k = 0; k < numModes; ++k)
                if (live[(size_t) k])
                    v.zi[(size_t) k] += 0.5f * scale * strikeAmp[(size_t) k];
        }
        for (int j = 0; j < n; ++j)
        {
            float s = 0.0f;
            for (int k = 0; k < numModes; ++k)
            {
                if (! live[(size_t) k])
                    continue;
                const float zr = v.zr[(size_t) k], zi = v.zi[(size_t) k];
                const float nr = pr[(size_t) k] * zr - pi[(size_t) k] * zi + g[(size_t) k] * in[j];
                const float ni = pr[(size_t) k] * zi + pi[(size_t) k] * zr;
                v.zr[(size_t) k] = nr;
                v.zi[(size_t) k] = ni;
                s += 2.0f * nr;
            }
            out[j] = s;
        }
    }

    float sr = 48000.0f, tune = 0.0f;
    std::array<float, numUsedSlots> target = []
    {
        std::array<float, numUsedSlots> d {};
        const auto slots = makeSlots();
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = slots[i].defaultValue;
        return d;
    }();
    std::vector<Voice> voices;
};
}
