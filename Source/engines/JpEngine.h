#pragma once
#include "Engine.h"
#include "Glide.h"
#include "Envelope.h"
#include "Filter.h"
#include "Drive.h"
#include "Unison.h"
#include <array>
#include <cmath>
#include <vector>

namespace ps
{
// JP: a Roland JP-8000/8080 style virtual analog voice.
//
//   OSC1 (saw, pulse or triangle) through JP or Modern unison, then a key-tracked high-pass (the JP Super Saw's
//   own HPF, half an octave below the fundamental)
//   OSC2 (saw, pulse, triangle or noise) with hard sync, ring mod or cross-mod into OSC1
//   balance -> drive (ADAA tanh) -> filter (24 dB ladder, or 12 dB low/band/high SVF) with its own ADSR,
//   key tracking and velocity -> one-pole low cut
//
// Oscillators are band-limited with PolyBLEP (steps) and PolyBLAMP (triangle corners); hard sync adds a
// PolyBLEP at each reset, so OSC2 runs one sample late. Trade-off, flagged: two-sample polynomial
// corrections leave some aliasing on very high notes and sync sweeps (about -50 dB at 4 kHz), lower than
// the hardware's naive saws at 88.2 kHz; the drive is ADAA but not oversampled.
//
// Discrete switches (waves, unison mode, OSC2 mod, filter type) dip the voice for 3 ms each way instead of
// clicking. Every continuous control is smoothed per voice.
class JpEngine final : public Engine
{
public:
    enum Slot
    {
        wave1, unison, detune, mix, spread, pulseWidth, wave2, pitch2, fine2, balance, osc2Mod, xmodDepth,
        filterType, cutoff, resonance, drive, keyTrack, envAmount, envAttack, envDecay, envSustain, envRelease,
        lowCut, velToEnv, numUsedSlots
    };

    static constexpr int numWaves1 = 3, numUnison = 5, numWaves2 = 4, numMods = 4, numFilterTypes = 4;

    const EngineInfo& info() const override
    {
        static const EngineInfo i { "jp", "JP", 16, makeSlots() };
        return i;
    }

    void prepare (double sampleRate, int, int numVoices) override
    {
        sr = (float) sampleRate;
        voices.assign ((size_t) numVoices, {});
        for (auto& v : voices)
            v.rng = 0x9E3779B9u * (uint32_t) (&v - voices.data() + 1);
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
            for (auto& p : v.phase)
                p = random (v);   // JP: free-running oscillators, a new random phase on every trigger
            v.phase2 = random (v);
            snapControls (v);
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
        for (int pos = 0; pos < numSamples; pos += subBlock)
            renderSubBlock (v, left + pos, right + pos, juce::jmin (subBlock, numSamples - pos));
    }

    // ---- Slot mappings (shared with the slot text) -------------------------------------------------------
    static int choice (float v, int n) noexcept   { return juce::jlimit (0, n - 1, (int) (v * (float) n)); }
    static float choiceValue (int i, int n) noexcept { return ((float) i + 0.5f) / (float) n; }
    static float timeMs (float v) noexcept        { return 0.5f + 9999.5f * v * v * v; }
    static float timeValue (float ms) noexcept    { return std::cbrt ((ms - 0.5f) / 9999.5f); }
    static float cutoffHz (float v) noexcept      { return 20.0f * std::pow (1000.0f, v); }
    static float lowCutHz (float v) noexcept      { return 20.0f * std::pow (100.0f, v); }
    static float pitch2Semis (float v) noexcept   { return (float) juce::roundToInt ((v * 2.0f - 1.0f) * 24.0f); }
    static float envSemis (float v) noexcept      { return (v * 2.0f - 1.0f) * 72.0f; }

private:
    static constexpr int subBlock = 16;
    static constexpr float voiceLevel = 0.306f;   // one note at about -15 dBFS RMS, like the other engines

    struct Voice
    {
        bool active = false;
        float velocity = 1.0f;
        PitchGlide pitch;
        AmpEnvelope envelope;   // the filter envelope (the layer owns the amp envelope)
        uint32_t rng = 1;

        // oscillators
        std::array<double, UnisonLayout::maxVoices> phase {};
        std::array<float, UnisonLayout::maxVoices> uniGain {}, panL {}, panR {};
        UnisonLayout layout;
        double phase2 = 0.0, master = 0.0;
        float held2 = 0.0f;   // OSC2 runs one sample late so sync resets can be corrected on both sides

        // smoothed controls (per voice, so render order does not matter)
        std::array<float, numUsedSlots> smooth {};
        float layoutDetune = -1.0f, layoutMix = -1.0f, layoutSpread = -1.0f;

        // discrete controls in use, and the dip that changes them
        int wave1 = 0, unisonMode = 1, wave2 = 0, mod = 0, filterType = 0;
        float switchGain = 1.0f;
        bool switching = false;

        // filters, per channel
        std::array<ZdfSvf, 2> keyHp, svf;
        std::array<ZdfLadder, 2> ladder;
        std::array<ZdfHighPass1, 2> lowCut;
        std::array<AdaaTanh, 2> saturator;
        bool driveOn = false;
    };

    static std::vector<SlotInfo> makeSlots()
    {
        auto choices = [] (juce::StringArray names)
        {
            return [names] (float v) { return names[choice (v, names.size())]; };
        };
        auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        auto ms = [] (float v)
        {
            const float t = timeMs (v);
            return t < 1000.0f ? juce::String (t, t < 10.0f ? 1 : 0) + " ms" : juce::String (t * 0.001f, 2) + " s";
        };
        auto hz = [] (float f) { return f < 1000.0f ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f * 0.001f, 2) + " kHz"; };
        auto signedText = [] (float x, int decimals, const char* unit)
        {
            return (x > 0.0f ? "+" : "") + juce::String (x, decimals) + unit;
        };

        std::vector<SlotInfo> s ((size_t) numUsedSlots);
        s[wave1]       = { "Wave", choiceValue (0, numWaves1), choices ({ "Saw", "Pulse", "Triangle" }) };
        s[unison]      = { "Unison", choiceValue (1, numUnison), choices ({ "Off", "JP 7", "Modern 8", "Modern 12", "Modern 16" }) };
        s[detune]      = { "Detune", 0.6f, percent };
        s[mix]         = { "Mix", 0.65f, percent };
        s[spread]      = { "Spread", 0.7f, percent };
        s[pulseWidth]  = { "Pulse Width", 0.0f, [] (float v) { return juce::String (juce::roundToInt (50.0f + 45.0f * v)) + " %"; } };
        s[wave2]       = { "Wave 2", choiceValue (0, numWaves2), choices ({ "Saw", "Pulse", "Triangle", "Noise" }) };
        s[pitch2]      = { "Pitch 2", 0.5f, [signedText] (float v) { return signedText (pitch2Semis (v), 0, " st"); } };
        s[fine2]       = { "Fine 2", 0.5f, [signedText] (float v) { return signedText ((v * 2.0f - 1.0f) * 50.0f, 1, " ct"); } };
        s[balance]     = { "Balance", 0.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " % OSC2"; } };
        s[osc2Mod]     = { "OSC2 Mod", choiceValue (0, numMods), choices ({ "Off", "Sync", "Ring", "X-Mod" }) };
        s[xmodDepth]   = { "X-Mod", 0.0f, percent };
        s[filterType]  = { "Filter", choiceValue (0, numFilterTypes), choices ({ "LP 24", "LP 12", "BP 12", "HP 12" }) };
        s[cutoff]      = { "Cutoff", 1.0f, [hz] (float v) { return hz (cutoffHz (v)); } };
        s[resonance]   = { "Resonance", 0.0f, percent };
        s[drive]       = { "Drive", 0.0f, percent };
        s[keyTrack]    = { "Key Track", 0.0f, percent };
        s[envAmount]   = { "Env Amount", 0.5f, [signedText] (float v) { return signedText (envSemis (v), 0, " st"); } };
        s[envAttack]   = { "Env Attack", timeValue (1.0f), ms };
        s[envDecay]    = { "Env Decay", timeValue (400.0f), ms };
        s[envSustain]  = { "Env Sustain", 0.0f, percent };
        s[envRelease]  = { "Env Release", timeValue (300.0f), ms };
        s[lowCut]      = { "Low Cut", 0.0f, [hz] (float v) { return hz (lowCutHz (v)); } };
        s[velToEnv]    = { "Vel > Env", 0.0f, percent };
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

    // ---- Waveforms ---------------------------------------------------------------------------------------
    // Two-sample polynomial step correction for a unit-height (2) downward step at phase 0
    static float blep (float t, float dt) noexcept
    {
        if (t < dt)       { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    // Integrated blep, for slope changes
    static float blamp (float t, float dt) noexcept
    {
        if (t < dt)       { t = t / dt - 1.0f; return -t * t * t * (1.0f / 3.0f); }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt + 1.0f; return t * t * t * (1.0f / 3.0f); }
        return 0.0f;
    }

    static float wrapped (float t) noexcept { return t < 0.0f ? t + 1.0f : t; }

    // Band-limited wave at phase t (0..1) with increment dt. 0 saw, 1 pulse, 2 triangle.
    static float wave (int type, float t, float dt, float pw) noexcept
    {
        switch (type)
        {
            case 1:  return (t < pw ? 1.0f : -1.0f) + blep (t, dt) - blep (wrapped (t - pw), dt);
            case 2:  return 4.0f * std::abs (t - 0.5f) - 1.0f
                            + 8.0f * dt * (blamp (wrapped (t - 0.5f), dt) - blamp (t, dt));
            default: return 2.0f * t - 1.0f - blep (t, dt);
        }
    }

    // Naive value at phase t, for the sync step height
    static float naive (int type, float t, float pw) noexcept
    {
        switch (type)
        {
            case 1:  return t < pw ? 1.0f : -1.0f;
            case 2:  return 4.0f * std::abs (t - 0.5f) - 1.0f;
            default: return 2.0f * t - 1.0f;
        }
    }

    float random (Voice& v) noexcept
    {
        v.rng ^= v.rng << 13;
        v.rng ^= v.rng >> 17;
        v.rng ^= v.rng << 5;
        return (float) (v.rng >> 8) * (1.0f / 16777216.0f);
    }

    // ---- Voice -------------------------------------------------------------------------------------------
    void resetVoice (Voice& v) noexcept
    {
        v.active = false;
        v.envelope.reset();
        for (auto* f : { &v.keyHp[0], &v.keyHp[1], &v.svf[0], &v.svf[1] })
            f->reset();
        for (auto& f : v.ladder) f.reset();
        for (auto& f : v.lowCut) f.reset();
        for (auto& f : v.saturator) f.reset();
        v.uniGain.fill (0.0f);
        v.held2 = 0.0f;
        v.master = 0.0;
        v.driveOn = false;
        v.switching = false;
        v.switchGain = 1.0f;
    }

    // An idle voice starts from the current settings, without gliding in from old ones
    void snapControls (Voice& v) noexcept
    {
        v.smooth = target;
        v.wave1 = choice (target[wave1], numWaves1);
        v.unisonMode = choice (target[unison], numUnison);
        v.wave2 = choice (target[wave2], numWaves2);
        v.mod = choice (target[osc2Mod], numMods);
        v.filterType = choice (target[filterType], numFilterTypes);
        v.layoutDetune = -1.0f;
        updateLayout (v);
        for (int k = 0; k < UnisonLayout::maxVoices; ++k)
            v.uniGain[(size_t) k] = v.layout.gain[(size_t) k];
    }

    void updateLayout (Voice& v) noexcept
    {
        const float d = v.smooth[detune], m = v.smooth[mix], s = v.smooth[spread];
        if (std::abs (d - v.layoutDetune) < 1.0e-6f && std::abs (m - v.layoutMix) < 1.0e-6f && std::abs (s - v.layoutSpread) < 1.0e-6f)
            return;
        v.layoutDetune = d;
        v.layoutMix = m;
        v.layoutSpread = s;
        v.layout.set ((UnisonLayout::Mode) v.unisonMode, d, m, s);
        for (int k = 0; k < UnisonLayout::maxVoices; ++k)
        {
            const float angle = (v.layout.pan[(size_t) k] + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            v.panL[(size_t) k] = std::cos (angle) * juce::MathConstants<float>::sqrt2;
            v.panR[(size_t) k] = std::sin (angle) * juce::MathConstants<float>::sqrt2;
        }
    }

    bool discreteChanged (const Voice& v) const noexcept
    {
        return v.wave1 != choice (target[wave1], numWaves1) || v.unisonMode != choice (target[unison], numUnison)
            || v.wave2 != choice (target[wave2], numWaves2) || v.mod != choice (target[osc2Mod], numMods)
            || v.filterType != choice (target[filterType], numFilterTypes);
    }

    void applyDiscrete (Voice& v) noexcept
    {
        const int newFilter = choice (target[filterType], numFilterTypes);
        if (newFilter != v.filterType)
        {
            for (auto* f : { &v.svf[0], &v.svf[1] }) f->reset();
            for (auto& f : v.ladder) f.reset();
        }
        v.wave1 = choice (target[wave1], numWaves1);
        v.unisonMode = choice (target[unison], numUnison);
        v.wave2 = choice (target[wave2], numWaves2);
        v.mod = choice (target[osc2Mod], numMods);
        v.filterType = newFilter;
        v.layoutDetune = -1.0f;
        updateLayout (v);
        v.uniGain = v.layout.gain;   // silent at this point, so the new layout can start directly
    }

    void updateEnvelope() noexcept
    {
        envelope.set (sr, timeMs (target[envAttack]), timeMs (target[envDecay]), target[envSustain], timeMs (target[envRelease]));
    }

    void renderSubBlock (Voice& v, float* outL, float* outR, int n) noexcept
    {
        // Controls: one-pole toward the targets (20 ms); the per-sample ones ramp linearly across the sub-block
        const float coef = 1.0f - std::exp (-(float) n / (0.02f * sr));
        const auto previous = v.smooth;
        for (size_t i = 0; i < v.smooth.size(); ++i)
            v.smooth[i] += (target[i] - v.smooth[i]) * coef;
        updateLayout (v);

        // Discrete switches: 3 ms down, swap, 3 ms up
        const float switchStep = 1.0f / (0.003f * sr);
        if (! v.switching && discreteChanged (v))
            v.switching = true;

        // Pitch: per sample while gliding, otherwise once per sub-block
        std::array<float, subBlock> baseInc {}, osc2 {}, osc1L {}, osc1R {};
        auto incrementFor = [this] (float pitch) { return 440.0f * std::exp2 ((pitch + tune - 69.0f) / 12.0f) / sr; };
        if (v.pitch.isGliding())
            for (int j = 0; j < n; ++j)
                baseInc[(size_t) j] = incrementFor (v.pitch.next());
        else
            std::fill (baseInc.begin(), baseInc.begin() + n, incrementFor (v.pitch.getCurrent()));
        const float pitchNow = v.pitch.getCurrent() + tune;

        // ---- OSC2 (one sample late) ----
        const float pw = 0.5f + 0.45f * v.smooth[pulseWidth];
        const float ratio2 = std::exp2 ((pitch2Semis (v.smooth[pitch2]) + (v.smooth[fine2] * 2.0f - 1.0f) * 0.5f) / 12.0f);
        for (int j = 0; j < n; ++j)
        {
            const float inc = baseInc[(size_t) j];
            const float dt = juce::jmin (0.45f, inc * ratio2);
            float value;
            if (v.wave2 == 3)
            {
                value = random (v) * 2.0f - 1.0f;
            }
            else
            {
                v.phase2 += dt;
                v.phase2 -= std::floor (v.phase2);
                v.master += inc;
                bool reset = false;
                float sinceReset = 0.0f;   // samples from the reset to this sample
                if (v.master >= 1.0)
                {
                    v.master -= 1.0;
                    if (v.mod == 1)
                    {
                        reset = true;
                        sinceReset = juce::jlimit (0.0f, 1.0f, (float) v.master / inc);
                    }
                }
                if (reset)
                {
                    const float before = (float) v.phase2 - dt * sinceReset;
                    const float jump = naive (v.wave2, 0.0f, pw) - naive (v.wave2, wrapped (before - std::floor (before)), pw);
                    v.phase2 = dt * sinceReset;
                    v.held2 += 0.5f * jump * sinceReset * sinceReset;
                    value = wave (v.wave2, (float) v.phase2, dt, pw) - 0.5f * jump * (1.0f - sinceReset) * (1.0f - sinceReset);
                }
                else
                {
                    value = wave (v.wave2, (float) v.phase2, dt, pw);
                }
            }
            osc2[(size_t) j] = v.held2;
            v.held2 = value;
        }

        // ---- OSC1 through unison ----
        const bool xmod = v.mod == 3;
        const float xmodAmount = v.smooth[xmodDepth] * 2.0f;
        for (int k = 0; k < v.layout.count; ++k)
        {
            const auto ku = (size_t) k;
            const float ratio = v.layout.ratio[ku];
            float g = v.uniGain[ku];
            const float gStep = (v.layout.gain[ku] - g) / (float) n;
            const float pl = v.panL[ku], pr = v.panR[ku];
            double t = v.phase[ku];
            for (int j = 0; j < n; ++j)
            {
                float dt = baseInc[(size_t) j] * ratio;
                if (xmod)
                    dt *= juce::jmax (0.0f, 1.0f + xmodAmount * osc2[(size_t) j]);
                dt = juce::jmin (0.45f, dt);
                t += dt;
                t -= std::floor (t);
                g += gStep;
                const float s = wave (v.wave1, (float) t, dt, pw) * g;
                osc1L[(size_t) j] += s * pl;
                osc1R[(size_t) j] += s * pr;
            }
            v.phase[ku] = t;
            v.uniGain[ku] = v.layout.gain[ku];
        }

        // Key-tracked HPF half an octave... at half the fundamental, as on the JP Super Saw
        const float keyHpG = tptGain (0.5f * 440.0f * std::exp2 ((pitchNow - 69.0f) / 12.0f), sr);

        // ---- Mix, drive, filter ----
        const float filterK = v.filterType == 0 ? 3.9f * v.smooth[resonance]
                                                : juce::MathConstants<float>::sqrt2 * (1.0f - 0.97f * v.smooth[resonance]);
        const float lowCutG = tptGain (lowCutHz (v.smooth[lowCut]), sr);
        const float velScale = 1.0f - v.smooth[velToEnv] * (1.0f - v.velocity);
        const float envDepth = envSemis (v.smooth[envAmount]) * velScale;
        const float trackSemis = v.smooth[keyTrack] * (pitchNow - 60.0f);
        const float cutoff0 = 69.0f + 12.0f * std::log2 (cutoffHz (previous[cutoff]) / 440.0f);
        const float cutoff1 = 69.0f + 12.0f * std::log2 (cutoffHz (v.smooth[cutoff]) / 440.0f);
        const float driveAmount = v.smooth[drive];
        const bool useDrive = driveAmount > 1.0e-4f || target[drive] > 0.0f;
        const float pre = 1.0f + 15.0f * driveAmount * driveAmount;
        const float post = 1.0f / std::pow (pre, 0.6f);
        const float wet = juce::jmin (1.0f, driveAmount * 20.0f);
        const float b0 = previous[balance], b1 = v.smooth[balance];
        const bool ring = v.mod == 2;

        for (int j = 0; j < n; ++j)
        {
            const float frac = (float) (j + 1) / (float) n;
            const float bal = (b0 + (b1 - b0) * frac) * juce::MathConstants<float>::halfPi;
            const float g1 = std::cos (bal) * voiceLevel, g2 = std::sin (bal) * voiceLevel;
            const float env = v.envelope.next (envelope);
            const float semis = cutoff0 + (cutoff1 - cutoff0) * frac + trackSemis + env * envDepth;
            const float g = tptGain (440.0f * std::exp2 ((semis - 69.0f) / 12.0f), sr);

            if (useDrive && ! v.driveOn)
            {
                for (auto& s : v.saturator) s.reset();
                v.driveOn = true;
            }

            std::array<float, 2> x { osc1L[(size_t) j], osc1R[(size_t) j] };
            for (int c = 0; c < 2; ++c)
            {
                auto& ch = x[(size_t) c];
                ch = v.keyHp[(size_t) c].process (ch, keyHpG, juce::MathConstants<float>::sqrt2).high;
                const float o2 = ring ? ch * osc2[(size_t) j] * 1.7320508f : osc2[(size_t) j];
                ch = ch * g1 + o2 * g2;
                if (v.driveOn)
                    ch += wet * (post * v.saturator[(size_t) c].process (pre * ch) - ch);

                switch (v.filterType)
                {
                    case 0:  ch = 2.0f * v.ladder[(size_t) c].process (0.5f * ch * (1.0f + 0.5f * filterK), g, filterK); break;
                    case 1:  ch = v.svf[(size_t) c].process (ch, g, filterK).low; break;
                    case 2:  ch = v.svf[(size_t) c].process (ch, g, filterK).band * filterK; break;
                    default: ch = v.svf[(size_t) c].process (ch, g, filterK).high; break;
                }
                ch = v.lowCut[(size_t) c].process (ch, lowCutG);
            }

            if (v.switching)
            {
                v.switchGain -= switchStep;
                if (v.switchGain <= 0.0f)
                {
                    v.switchGain = 0.0f;
                    applyDiscrete (v);
                    v.switching = false;
                }
            }
            else if (v.switchGain < 1.0f)
            {
                v.switchGain = juce::jmin (1.0f, v.switchGain + switchStep);
            }

            outL[j] += x[0] * v.switchGain;
            outR[j] += x[1] * v.switchGain;
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
