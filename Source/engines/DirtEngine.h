#pragma once
#include "Engine.h"
#include "Glide.h"
#include "Envelope.h"
#include "Filter.h"
#include "Drive.h"
#include "Unison.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ps
{
// Dirt: a Dreadbox-style analog body. One shape-morph oscillator, square subs one and two octaves down, noise,
// an OTA drive and a 2- or 3-pole filter whose resonance keeps the lows, with its own ADSR.
//
//   Shape 0 -> 50 %: triangle -> saw with a curved reset. The fall shortens from half the period to 2 % of it
//   and bends into an exponential discharge, the way an analog core's capacitor resets. 50 -> 100 %: the
//   saw crossfades into a square (in phase, power-matched). Every shape plays at the saw's RMS.
//   3-pole: three OTA one-poles in a feedback loop (18 dB/oct, self-oscillates near full resonance), solved
//   without delay with the saturator on the loop input. The input is raised by 1 + k, the loop's DC loss, so
//   resonance thins nothing below the cutoff. 2-pole: an SVF low-pass, whose lows are unity at any resonance.
//   Drift: each voice is "auto-tuned" to a small fixed offset plus a slow wander, like analog voice cards.
//   Re-roll picks a new set of offsets.
//
// Oscillators, drive and filter run at 2x and are decimated by a 63-tap half-band. The curved corners get
// PolyBLAMP corrections from their exact slopes, the square and subs PolyBLEP. The fall is never shorter
// than 2.5 samples at 2x, so the corners never overlap; its bend fades out on high notes, where it would be
// squeezed into a few samples, and a reset under 10 samples hands over to a PolyBLEP saw (the difference
// lies above 20 kHz there). Trade-off, flagged: the filter's in-loop saturator is not ADAA, so heavy
// resonance with full drive aliases more than the clean path (still 2x oversampled).
//
// Paraphonic mode (voices sharing one filter) is not built: the layer owns per-voice envelopes after the
// engine renders, so a shared filter needs the Engine interface extended.
class DirtEngine final : public Engine
{
public:
    enum Slot
    {
        shape, sub1, sub2, noise, drive, poles, cutoff, resonance, keyTrack, envAmount, envAttack, envDecay,
        envSustain, envRelease, velToEnv, drift, reroll, numUsedSlots
    };

    static constexpr int numPoles = 2, numSeeds = 64;

    const EngineInfo& info() const override
    {
        static const EngineInfo i { "dirt", "Dirt", 16, makeSlots() };
        return i;
    }

    void prepare (double sampleRate, int, int numVoices) override
    {
        sr = (float) sampleRate;
        voices.assign ((size_t) numVoices, {});
        for (auto& v : voices)
        {
            const auto k = (uint32_t) (&v - voices.data());
            v.rng = 0x9E3779B9u * (k + 1);
            v.index = (int) k;
            v.wanderRate = 0.07f + 0.16f * unit (hash (0x51ED27u, k));
            v.wanderPhase = unit (hash (0xA341316Cu, k));
        }
        updateEnvelope();
    }

    void reset() override
    {
        for (auto& v : voices)
            resetVoice (v);
    }

    void noteOn (int index, const NoteEvent& e) override
    {
        auto& v = voices[(size_t) index];
        if (! v.active)
        {
            resetVoice (v);
            v.pitch.jumpTo (e.glideSeconds > 0.0 && e.glideFrom >= 0.0f ? e.glideFrom : (float) e.note);
            // Subs start at the matching fraction of the main phase, so all three stay locked
            v.t = e.fixedPhase ? 0.0f : random (v);
            v.t1 = 0.5f * v.t;
            v.t2 = 0.25f * v.t;
            v.smooth = target;
            v.poles = choice (target[poles], numPoles);
            v.offsetCents = targetOffset (v);
            v.active = true;
        }
        if (! e.legato || ! v.envelope.isActive())
        {
            v.velocity = e.velocity;
            v.envelope.trigger();
        }
        v.pitch.glideTo ((float) e.note, e.glideSeconds, sr);
    }

    void noteOff (int index) override { voices[(size_t) index].envelope.release(); }
    void endVoice (int index) override { resetVoice (voices[(size_t) index]); }
    void setTune (float semitones) override { tune = semitones; }

    void setSlot (int slot, float value) override
    {
        if (slot < 0 || slot >= numUsedSlots)
            return;
        target[(size_t) slot] = juce::jlimit (0.0f, 1.0f, value);
        if (slot >= envAttack && slot <= envRelease)
            updateEnvelope();
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

    // ---- Slot mappings (shared with the slot text) -------------------------------------------------------
    static int choice (float v, int n) noexcept      { return juce::jlimit (0, n - 1, (int) (v * (float) n)); }
    static float choiceValue (int i, int n) noexcept { return ((float) i + 0.5f) / (float) n; }
    static float timeMs (float v) noexcept           { return 0.5f + 9999.5f * v * v * v; }
    static float timeValue (float ms) noexcept       { return std::cbrt ((ms - 0.5f) / 9999.5f); }
    static float cutoffHz (float v) noexcept         { return 20.0f * std::pow (1000.0f, v); }
    static float cutoffValue (float hz) noexcept     { return std::log (hz / 20.0f) / std::log (1000.0f); }
    static float envSemis (float v) noexcept         { return (v * 2.0f - 1.0f) * 60.0f; }
    static float driftCents (float v) noexcept       { return 12.0f * v; }

private:
    static constexpr int subBlock = 16;
    static constexpr float voiceLevel = 0.36f;      // the default patch at about the JP's level
    static constexpr float sawRms = 0.57735027f;    // every source is scaled to a saw's RMS
    static constexpr float minFall = 0.02f;         // the saw's reset: 2 % of the period

    // tanh-like soft clip (Filter.h softClip) with first-order ADAA: its antiderivative is closed-form, so this
    // is several times cheaper than ADAA tanh in double precision, which matters at 2x on every voice
    struct AdaaClip
    {
        static float antiderivative (float x) noexcept
        {
            const float a = std::abs (x);
            if (a >= 3.0f)
                return 2.3483925f + (a - 3.0f);   // softClip is flat (1) beyond 3
            return a * a * (1.0f / 18.0f) + (4.0f / 3.0f) * std::log (1.0f + a * a * (1.0f / 3.0f));
        }

        float process (float x) noexcept
        {
            const float f = antiderivative (x), dx = x - x1;
            const float y = std::abs (dx) > 1.0e-3f ? (f - f1) / dx : softClip (0.5f * (x + x1));
            x1 = x;
            f1 = f;
            return y;
        }

        void reset() noexcept { x1 = f1 = 0.0f; }
        float x1 = 0.0f, f1 = 0.0f;
    };

    struct Voice
    {
        float pitchBend = 0.0f;   // semitones, from the layer (pitch bend, MPE)
        bool active = false;
        int index = 0;
        float velocity = 1.0f;
        PitchGlide pitch;
        AmpEnvelope envelope;   // the filter envelope (the layer owns the amp envelope)
        uint32_t rng = 1;
        float t = 0.0f, t1 = 0.0f, t2 = 0.0f;
        float offsetCents = 0.0f, wanderRate = 0.1f, wanderPhase = 0.0f;
        std::array<float, numUsedSlots> smooth {};
        int poles = 1;
        float fade = 1.0f;      // the pole switch: the outgoing filter fades out as the new one fades in
        bool switching = false;
        AdaaClip saturator;
        bool driveOn = false;
        std::array<float, 3> ota {};
        ZdfSvf svf;
        HalfbandDecimator decimator;
    };

    // The oscillator's shape at one Shape setting: rise share, fall curvature and the constants they imply
    struct ShapeParams
    {
        float rise = 0.5f, fall = 0.5f, invFall = 2.0f, curve = 0.0f, square = 0.0f;
        float riseSlope = 4.0f, fallSlopeStart = -4.0f, fallSlopeEnd = -4.0f;
        float curveNorm = 1.0f, dc = 0.0f, gain = 1.0f, mixNorm = 1.0f;
        float sawBlend = 0.0f, sawShift = 0.0f;   // share of a PolyBLEP saw, and its phase lead (centred on the fall)
    };


    struct FilterCoeffs
    {
        float G = 0.0f, beta = 1.0f, G3 = 0.0f, k3 = 0.0f, inGain3 = 1.0f, norm3 = 1.0f;
        ZdfSvf::Coeffs svf {};
    };

    static std::vector<SlotInfo> makeSlots()
    {
        auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        auto ms = [] (float v)
        {
            const float t = timeMs (v);
            return t < 1000.0f ? juce::String (t, t < 10.0f ? 1 : 0) + " ms" : juce::String (t * 0.001f, 2) + " s";
        };
        std::vector<SlotInfo> s ((size_t) numUsedSlots);
        s[shape]      = { "Shape", 0.5f, [] (float v)
                          {
                              if (v < 0.01f) return juce::String ("Triangle");
                              if (std::abs (v - 0.5f) < 0.01f) return juce::String ("Saw");
                              if (v > 0.99f) return juce::String ("Square");
                              return v < 0.5f ? "Tri > Saw " + juce::String (juce::roundToInt (v * 200.0f)) + " %"
                                              : "Saw > Sqr " + juce::String (juce::roundToInt ((v - 0.5f) * 200.0f)) + " %";
                          } };
        s[sub1]       = { "Sub -1 Oct", 0.35f, percent };
        s[sub2]       = { "Sub -2 Oct", 0.0f, percent };
        s[noise]      = { "Noise", 0.0f, percent };
        s[drive]      = { "Drive", 0.2f, percent };
        s[poles]      = { "Filter", choiceValue (1, numPoles), [] (float v) { return juce::String (choice (v, numPoles) == 0 ? "2-Pole" : "3-Pole"); } };
        s[cutoff]     = { "Cutoff", cutoffValue (1400.0f), [] (float v)
                          {
                              const float f = cutoffHz (v);
                              return f < 1000.0f ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f * 0.001f, 2) + " kHz";
                          } };
        s[resonance]  = { "Resonance", 0.25f, percent };
        s[keyTrack]   = { "Key Track", 0.5f, percent };
        s[envAmount]  = { "Env Amount", 0.7f, [] (float v)
                          { const float st = envSemis (v); return (st > 0.0f ? "+" : "") + juce::String (st, 0) + " st"; } };
        s[envAttack]  = { "Env Attack", timeValue (1.0f), ms };
        s[envDecay]   = { "Env Decay", timeValue (450.0f), ms };
        s[envSustain] = { "Env Sustain", 0.35f, percent };
        s[envRelease] = { "Env Release", timeValue (300.0f), ms };
        s[velToEnv]   = { "Vel > Env", 0.3f, percent };
        s[drift]      = { "Drift", 0.3f, [] (float v) { return juce::String (driftCents (v), 1) + " ct"; } };
        s[reroll]     = { "Re-roll", choiceValue (0, numSeeds), [] (float v) { return "#" + juce::String (choice (v, numSeeds) + 1); } };
        s[shape].role = SlotRole::position;
        s[sub1].role = SlotRole::subOctave;
        s[drive].role = SlotRole::drive;
        s[cutoff].role = SlotRole::brightness;
        return s;
    }

    static std::array<float, numUsedSlots> defaults()
    {
        std::array<float, numUsedSlots> d {};
        const auto slots = makeSlots();
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = slots[i].defaultValue;
        return d;
    }

    static uint32_t hash (uint32_t a, uint32_t b) noexcept
    {
        uint32_t h = a * 0x9E3779B9u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu;
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        return h ^ (h >> 16);
    }

    static float unit (uint32_t h) noexcept { return (float) (h >> 8) * (1.0f / 16777216.0f); }

    float random (Voice& v) noexcept
    {
        v.rng ^= v.rng << 13;
        v.rng ^= v.rng >> 17;
        v.rng ^= v.rng << 5;
        return (float) (v.rng >> 8) * (1.0f / 16777216.0f);
    }

    // This voice's auto-tune offset for the current seed, in cents
    float targetOffset (const Voice& v) const noexcept
    {
        const auto seed = (uint32_t) choice (target[reroll], numSeeds);
        return driftCents (target[drift]) * (2.0f * unit (hash (seed + 1, (uint32_t) v.index)) - 1.0f);
    }

    void resetVoice (Voice& v) noexcept
    {
        v.active = false;
        v.envelope.reset();
        v.saturator.reset();
        v.driveOn = false;
        v.ota.fill (0.0f);
        v.svf.reset();
        v.decimator.reset();
        v.switching = false;
        v.fade = 1.0f;
    }

    void updateEnvelope() noexcept
    {
        envelope.set (sr, timeMs (target[envAttack]), timeMs (target[envDecay]), target[envSustain], timeMs (target[envRelease]));
    }

    // Shape at setting s with the fall kept at least 2.5 samples long for the increment dt
    static ShapeParams shapeAt (float s, float dt) noexcept
    {
        ShapeParams p;
        const float x = juce::jlimit (0.0f, 1.0f, 2.0f * s);
        p.square = juce::jlimit (0.0f, 1.0f, 2.0f * s - 1.0f);
        const float wantedFall = 0.5f * std::pow (minFall / 0.5f, x);
        p.fall = juce::jlimit (juce::jmin (0.5f, 2.5f * dt), 0.5f, wantedFall);
        // A reset squeezed into a few samples is a step: there a PolyBLEP saw (centred on the fall) takes over,
        // since two PolyBLAMPs a couple of samples apart band-limit a near-step worse than one PolyBLEP
        p.sawBlend = juce::jlimit (0.0f, 1.0f, (10.0f - wantedFall / juce::jmax (1.0e-6f, dt)) / 6.0f);
        p.sawShift = 0.5f * p.fall;
        p.rise = 1.0f - p.fall;
        // The bend lives inside the fall, so it fades out once the fall spans under 16 samples (at 2x): squeezed
        // into a few samples, the curve itself would alias. High notes get a straight, BLAMP-corrected reset.
        const float fallSamples = p.fall / juce::jmax (1.0e-6f, dt);
        p.curve = 4.0f * x * x * juce::jlimit (0.0f, 1.0f, (fallSamples - 4.0f) / 12.0f);

        // Fall: v = 1 - 2 f(u), f(u) = (1 - e^(-c u)) / (1 - e^(-c)), u = 0..1 across the fall (linear as c -> 0)
        const float c = p.curve;
        float meanF = 0.5f, meanF2 = 1.0f / 3.0f, slopeStart = 1.0f, slopeEnd = 1.0f;   // f's mean, mean square, f'(0), f'(1)
        if (c > 1.0e-3f)
        {
            const float e = std::exp (-c), a = 1.0f / (1.0f - e);
            meanF = a - 1.0f / c;
            meanF2 = a * a * (1.0f - 2.0f * (1.0f - e) / c + (1.0f - e * e) / (2.0f * c));
            slopeStart = c * a;
            slopeEnd = c * a * e;
            p.curveNorm = a;
        }
        p.invFall = 1.0f / p.fall;
        p.riseSlope = 2.0f / p.rise;
        p.fallSlopeStart = -2.0f * slopeStart / p.fall;
        p.fallSlopeEnd = -2.0f * slopeEnd / p.fall;

        // DC and RMS of the curved wave, so every shape is centred and at the saw's RMS
        p.dc = p.fall * (1.0f - 2.0f * meanF);
        const float meanSquare = p.rise / 3.0f + p.fall * (1.0f - 4.0f * meanF + 4.0f * meanF2);
        p.gain = sawRms / std::sqrt (juce::jmax (1.0e-6f, meanSquare - p.dc * p.dc));
        // Saw and square share their odd harmonics in phase (correlation sqrt(3)/2): keep the crossfade's power
        const float q = p.square;
        p.mixNorm = 1.0f / std::sqrt ((1.0f - q) * (1.0f - q) + q * q + 1.7320508f * q * (1.0f - q));
        return p;
    }

    static float curved (const ShapeParams& p, float t, float dt, float invDt) noexcept
    {
        float v;
        if (t < p.rise)
        {
            v = -1.0f + p.riseSlope * t;
        }
        else
        {
            const float u = (t - p.rise) * p.invFall;
            v = p.curve > 1.0e-3f ? 1.0f - 2.0f * p.curveNorm * (1.0f - fastExp2 (-1.442695f * p.curve * u)) : 1.0f - 2.0f * u;
        }
        // PolyBLAMP at both corners, from the slope change there
        v -= (p.fallSlopeStart - p.riseSlope) * dt * blep::ramp (blep::wrap (t - p.rise), dt, invDt);
        v -= (p.riseSlope - p.fallSlopeEnd) * dt * blep::ramp (t, dt, invDt);
        v = (v - p.dc) * p.gain;
        if (p.sawBlend > 0.0f)
        {
            const float ts = t + p.sawShift;
            const float saw = blep::wave<blep::saw> (ts - (ts >= 1.0f ? 1.0f : 0.0f), dt, 0.5f);
            v += (saw - v) * p.sawBlend;
        }
        return v;
    }

    static float otaSat (float x) noexcept { return 2.0f * softClip (0.5f * x); }

    // One filter step at 2x: type 1 is the 3-pole OTA cascade, 0 the SVF
    static float filter (Voice& v, int type, float x, const FilterCoeffs& c) noexcept
    {
        if (type == 0)
            return v.svf.process (otaSat (x), c.svf).low;
        const float G = c.G;
        const float S = c.beta * (G * (G * v.ota[0] + v.ota[1]) + v.ota[2]);
        const float y3 = (c.G3 * c.inGain3 * x + S) * c.norm3;
        float u = otaSat (c.inGain3 * x - c.k3 * y3);
        for (auto& state : v.ota)
        {
            const float vv = (u - state) * G;
            const float y = vv + state;
            state = y + vv;
            u = y;
        }
        return u;
    }

    void renderSubBlock (Voice& v, float* outL, float* outR, int n) noexcept
    {
        const float coef = 1.0f - std::exp (-(float) n / (0.02f * sr));
        const auto previous = v.smooth;
        for (size_t i = 0; i < v.smooth.size(); ++i)
            v.smooth[i] += (target[i] - v.smooth[i]) * coef;
        if (! v.switching && v.poles != choice (target[poles], numPoles))
        {
            // The incoming filter starts from rest and fades in over 5 ms while the old one fades out
            if (v.poles == 1)
                v.svf.reset();
            else
                v.ota.fill (0.0f);
            v.switching = true;
            v.fade = 0.0f;
        }

        // Pitch: the auto-tune offset (gliding when Drift or Re-roll move) plus a slow wander
        v.offsetCents += (targetOffset (v) - v.offsetCents) * coef;
        v.wanderPhase += v.wanderRate * (float) n / sr;
        v.wanderPhase -= std::floor (v.wanderPhase);
        const float driftSemis = 0.01f * (v.offsetCents + 0.25f * driftCents (v.smooth[drift])
                                                          * std::sin (juce::MathConstants<float>::twoPi * v.wanderPhase));

        const float sr2 = 2.0f * sr;
        alignas (32) std::array<float, subBlock> pitchAt {};
        for (int j = 0; j < n; ++j)
            pitchAt[(size_t) j] = (v.pitch.isGliding() ? v.pitch.next() : v.pitch.getCurrent()) + tune + v.pitchBend + driftSemis;
        const float maxDt = 440.0f * fastExp2 ((*std::max_element (pitchAt.begin(), pitchAt.begin() + n) - 69.0f) / 12.0f) / sr2;
        const bool gliding = pitchAt[0] != pitchAt[(size_t) (n - 1)];

        // The cutoff's integrator gain at every 4th sample (and the last), linear in between, as in the JP: the
        // envelope still moves it at a 12 kHz control rate, with no steps
        alignas (32) std::array<float, subBlock> cutoffG {};
        {
            const float c0 = 69.0f + 12.0f * std::log2 (cutoffHz (previous[cutoff]) / 440.0f);
            const float c1 = 69.0f + 12.0f * std::log2 (cutoffHz (v.smooth[cutoff]) / 440.0f);
            const float velScale = 1.0f - v.smooth[velToEnv] * (1.0f - v.velocity);
            const float envDepth = envSemis (v.smooth[envAmount]) * velScale;
            alignas (32) std::array<float, subBlock> semis {};
            for (int j = 0; j < n; ++j)
                semis[(size_t) j] = c0 + (c1 - c0) * (float) (j + 1) / (float) n + v.smooth[keyTrack] * (pitchAt[(size_t) j] - 60.0f)
                                  + v.envelope.next (envelope) * envDepth;
            auto gAt = [&] (int j) { return tptGain (440.0f * fastExp2 ((juce::jlimit (-60.0f, 136.0f, semis[(size_t) j]) - 69.0f) / 12.0f), sr2); };
            float gPrev = gAt (0);
            cutoffG[0] = gPrev;
            for (int j0 = 0; j0 < n - 1; j0 += 4)
            {
                const int j1 = juce::jmin (n - 1, j0 + 4);
                const float gNext = gAt (j1);
                const float step = (gNext - gPrev) / (float) (j1 - j0);
                for (int j = j0 + 1; j <= j1; ++j)
                    cutoffG[(size_t) j] = gPrev + step * (float) (j - j0);
                gPrev = gNext;
            }
        }
        const auto shapeParams = shapeAt (v.smooth[shape], juce::jmin (0.2f, maxDt));

        // Source levels, power-normalised (the subs and noise are uncorrelated with the oscillator)
        const float a1 = v.smooth[sub1], a2 = v.smooth[sub2], an = v.smooth[noise];
        const float sourceNorm = 1.0f / std::sqrt (1.0f + a1 * a1 + a2 * a2 + an * an);
        const float squareGain = sawRms * shapeParams.square, sawGain = 1.0f - shapeParams.square;

        // Drive (OTA input stage) as in the other engines: pre = 1 + 15 d^2, post = pre^-0.6
        const float d0 = previous[drive], d1 = v.smooth[drive];
        const bool useDrive = d1 > 1.0e-4f || target[drive] > 0.0f;
        if (useDrive && ! v.driveOn)
        {
            v.saturator.reset();
            v.driveOn = true;
        }
        const bool driveMoving = std::abs (d1 - d0) > 1.0e-7f;
        const float postSteady = 1.0f / std::pow (1.0f + 15.0f * d1 * d1, 0.6f);

        // Filter
        const float r = v.smooth[resonance];
        const float k3 = 8.5f * r;
        const float kSvf = 2.0f * (1.0f - 0.96f * r);
        const bool needSvf = v.poles == 0 || v.switching, needOta = v.poles == 1 || v.switching;
        const float fadeStep = 1.0f / (0.005f * sr);
        float dt = 0.0f, invDt = 0.0f;

        for (int j = 0; j < n; ++j)
        {
            const float frac = (float) (j + 1) / (float) n;
            if (j == 0 || gliding)
            {
                dt = juce::jlimit (1.0e-6f, 0.2f, 440.0f * fastExp2 ((pitchAt[(size_t) j] - 69.0f) / 12.0f) / sr2);
                invDt = 1.0f / dt;
            }
            const float g = cutoffG[(size_t) j];
            FilterCoeffs fc;
            if (needOta)
            {
                fc.beta = 1.0f / (1.0f + g);
                fc.G = g * fc.beta;
                fc.G3 = fc.G * fc.G * fc.G;
                fc.k3 = k3;
                fc.inGain3 = 1.0f + k3;
                fc.norm3 = 1.0f / (1.0f + k3 * fc.G3);
            }
            if (needSvf)
                fc.svf = ZdfSvf::Coeffs::make (g, kSvf);

            float pre = 1.0f, post = 1.0f, wet = 0.0f;
            if (v.driveOn)
            {
                const float dd = d0 + (d1 - d0) * frac;
                pre = 1.0f + 15.0f * dd * dd;
                post = driveMoving ? 1.0f / std::pow (pre, 0.6f) : postSteady;
                wet = juce::jmin (1.0f, dd * 20.0f);
            }

            std::array<float, 2> twice {};
            for (int h = 0; h < 2; ++h)
            {
                v.t += dt;
                v.t -= v.t >= 1.0f ? 1.0f : 0.0f;
                v.t1 += 0.5f * dt;
                v.t1 -= v.t1 >= 1.0f ? 1.0f : 0.0f;
                v.t2 += 0.25f * dt;
                v.t2 -= v.t2 >= 1.0f ? 1.0f : 0.0f;

                // Squares inverted, so they rise with the saw's fundamental
                float x = sawGain * curved (shapeParams, v.t, dt, invDt);
                if (squareGain > 0.0f)
                    x -= squareGain * blep::wave<blep::pulse> (v.t, dt, 0.5f);
                x *= shapeParams.mixNorm;
                if (a1 > 1.0e-4f)
                    x -= a1 * sawRms * blep::wave<blep::pulse> (v.t1, 0.5f * dt, 0.5f);
                if (a2 > 1.0e-4f)
                    x -= a2 * sawRms * blep::wave<blep::pulse> (v.t2, 0.25f * dt, 0.5f);
                if (an > 1.0e-4f)
                    x += an * (2.0f * random (v) - 1.0f);
                x *= sourceNorm;

                if (v.driveOn)
                    x += wet * (post * v.saturator.process (pre * x) - x);

                if (v.switching)
                {
                    const float outgoing = filter (v, v.poles, x, fc), incoming = filter (v, 1 - v.poles, x, fc);
                    x = outgoing + (incoming - outgoing) * v.fade;
                }
                else
                {
                    x = filter (v, v.poles, x, fc);
                }
                twice[(size_t) h] = x;
            }
            const float y = v.decimator.process (twice[0], twice[1]) * voiceLevel;

            if (v.switching)
            {
                v.fade += fadeStep;
                if (v.fade >= 1.0f)
                {
                    v.fade = 1.0f;
                    v.poles = 1 - v.poles;
                    v.switching = false;
                }
            }
            outL[j] += y;
            outR[j] += y;
        }
        if (! useDrive)
            v.driveOn = false;
    }

    float sr = 48000.0f, tune = 0.0f;
    std::array<float, numUsedSlots> target = defaults();
    AdsrSettings envelope;
    std::vector<Voice> voices;
};
}
