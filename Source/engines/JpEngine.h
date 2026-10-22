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
            for (auto& p : v.bank.phase)
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
        UnisonBank bank;
        UnisonLayout layout;
        float phase2 = 0.0f, master = 0.0f;
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
        std::array<HalfbandDecimator, 2> decimator;
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
        s[detune].role = SlotRole::detune;
        s[mix].role = SlotRole::unisonMix;
        s[spread].role = SlotRole::stereoSpread;
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

    // ---- Waveforms ---------------------------------------------------------------------------------------
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
        v.bank.gain.fill (0.0f);
        for (auto& d : v.decimator) d.reset();
        v.held2 = 0.0f;
        v.master = 0.0f;
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
        v.bank.gain = v.layout.gain;
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
        v.bank.ratio = v.layout.ratio;
        for (int k = 0; k < UnisonLayout::maxVoices; ++k)
        {
            const float angle = (v.layout.pan[(size_t) k] + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            v.bank.panL[(size_t) k] = std::cos (angle) * juce::MathConstants<float>::sqrt2;
            v.bank.panR[(size_t) k] = std::sin (angle) * juce::MathConstants<float>::sqrt2;
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
        v.bank.gain = v.layout.gain;   // silent at this point, so the new layout can start directly
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
        if (! v.switching && discreteChanged (v))
            v.switching = true;

        // ---- Oscillators at twice the sample rate ----
        const int n2 = 2 * n;
        const float sr2 = 2.0f * sr;
        alignas (32) std::array<float, 2 * subBlock> inc {}, osc1L {}, osc1R {}, osc2 {}, fm {};
        auto incrementFor = [this, sr2] (float pitch) { return 440.0f * fastExp2 ((pitch + tune - 69.0f) / 12.0f) / sr2; };
        if (v.pitch.isGliding())
            for (int j = 0; j < n; ++j)
                inc[(size_t) (2 * j)] = inc[(size_t) (2 * j + 1)] = incrementFor (v.pitch.next());
        else
            std::fill (inc.begin(), inc.begin() + n2, incrementFor (v.pitch.getCurrent()));
        const float pitchNow = v.pitch.getCurrent() + tune;
        const float pw = 0.5f + 0.45f * v.smooth[pulseWidth];

        // OSC2, one sample late: hard sync resets are corrected on both sides of the reset
        const float ratio2 = fastExp2 ((pitch2Semis (v.smooth[pitch2]) + (v.smooth[fine2] * 2.0f - 1.0f) * 0.5f) / 12.0f);
        // Skipped while it can't be heard (balance fully on OSC1 and no X-Mod)
        const bool osc2Heard = v.mod == 3 || previous[balance] > 0.0f || v.smooth[balance] > 0.0f;
        if (! osc2Heard)
            v.held2 = 0.0f;
        for (int j = 0; j < (osc2Heard ? n2 : 0); ++j)
        {
            const float master = inc[(size_t) j];
            const float dt = juce::jlimit (1.0e-6f, 0.45f, master * ratio2);
            float value;
            if (v.wave2 == 3)
            {
                value = random (v) * 2.0f - 1.0f;
            }
            else
            {
                v.phase2 += dt;
                v.phase2 -= v.phase2 >= 1.0f ? 1.0f : 0.0f;
                v.master += master;
                if (v.master >= 1.0f)
                {
                    v.master -= 1.0f;
                    if (v.mod == 1)
                    {
                        const float since = juce::jlimit (0.0f, 1.0f, v.master / master);   // samples since the reset
                        const float before = blep::wrap (v.phase2 - dt * since);
                        const float jump = naive (v.wave2, 0.0f, pw) - naive (v.wave2, before, pw);
                        v.phase2 = dt * since;
                        v.held2 += 0.5f * jump * since * since;
                        osc2[(size_t) j] = v.held2;
                        v.held2 = wave2Sample (v.wave2, v.phase2, dt, pw) - 0.5f * jump * (1.0f - since) * (1.0f - since);
                        continue;
                    }
                }
                value = wave2Sample (v.wave2, v.phase2, dt, pw);
            }
            osc2[(size_t) j] = v.held2;
            v.held2 = value;
        }

        // OSC1 through the unison bank; X-Mod scales its increments by OSC2
        if (v.mod == 3)
        {
            const float depth = v.smooth[xmodDepth] * 2.0f;
            for (int j = 0; j < n2; ++j)
                fm[(size_t) j] = inc[(size_t) j] * juce::jmax (0.0f, 1.0f + depth * osc2[(size_t) j]);
        }
        else
        {
            fm = inc;
        }
        const int lanes = v.layout.count <= 8 ? 8 : 16;
        switch (v.wave1)
        {
            case 1:  v.bank.render<blep::pulse> (fm.data(), lanes, v.layout.gain.data(), pw, osc1L.data(), osc1R.data(), n2); break;
            case 2:  v.bank.render<blep::triangle> (fm.data(), lanes, v.layout.gain.data(), pw, osc1L.data(), osc1R.data(), n2); break;
            default: v.bank.render<blep::saw> (fm.data(), lanes, v.layout.gain.data(), pw, osc1L.data(), osc1R.data(), n2); break;
        }

        // Key-tracked HPF at half the fundamental (the JP Super Saw's), then balance and ring mod, then down to 1x
        const float keyHpG = tptGain (0.5f * 440.0f * fastExp2 ((pitchNow - 69.0f) / 12.0f), sr2);
        // Equal-power balance at the sub-block ends, linear in between
        const float b0 = previous[balance] * juce::MathConstants<float>::halfPi, b1 = v.smooth[balance] * juce::MathConstants<float>::halfPi;
        const float g1a = std::cos (b0), g2a = std::sin (b0);
        const float g1Step = (std::cos (b1) - g1a) / (float) n2, g2Step = (std::sin (b1) - g2a) / (float) n2;
        const auto keyHp = ZdfSvf::Coeffs::make (keyHpG, juce::MathConstants<float>::sqrt2);
        const bool ring = v.mod == 2;
        alignas (32) std::array<float, subBlock> mixL {}, mixR {};
        for (int j = 0; j < n2; j += 2)
        {
            std::array<float, 2> l {}, r {};
            for (int h = 0; h < 2; ++h)
            {
                const auto jj = (size_t) (j + h);
                const float g1 = g1a + g1Step * (float) (j + h + 1), g2 = g2a + g2Step * (float) (j + h + 1);
                const float o1L = v.keyHp[0].process (osc1L[jj], keyHp).high;
                const float o1R = v.keyHp[1].process (osc1R[jj], keyHp).high;
                const float o2 = osc2[jj];
                l[(size_t) h] = g1 * o1L + g2 * (ring ? o1L * o2 * 1.7320508f : o2);
                r[(size_t) h] = g1 * o1R + g2 * (ring ? o1R * o2 * 1.7320508f : o2);
            }
            mixL[(size_t) (j / 2)] = v.decimator[0].process (l[0], l[1]) * voiceLevel;
            mixR[(size_t) (j / 2)] = v.decimator[1].process (r[0], r[1]) * voiceLevel;
        }

        // ---- Drive, filter, low cut at 1x ----
        const float switchStep = 1.0f / (0.003f * sr);
        const float filterK = v.filterType == 0 ? 3.9f * v.smooth[resonance]
                                                : juce::MathConstants<float>::sqrt2 * (1.0f - 0.97f * v.smooth[resonance]);
        const float lowCutG = tptGain (lowCutHz (v.smooth[lowCut]), sr);
        const float velScale = 1.0f - v.smooth[velToEnv] * (1.0f - v.velocity);
        const float envDepth = envSemis (v.smooth[envAmount]) * velScale;
        const float trackSemis = v.smooth[keyTrack] * (pitchNow - 60.0f);
        const float cutoff0 = 69.0f + 12.0f * std::log2 (cutoffHz (previous[cutoff]) / 440.0f);
        const float cutoff1 = 69.0f + 12.0f * std::log2 (cutoffHz (v.smooth[cutoff]) / 440.0f);
        const float d0 = previous[drive], d1 = v.smooth[drive];
        const bool useDrive = d1 > 1.0e-4f || target[drive] > 0.0f;
        if (useDrive && ! v.driveOn)
        {
            for (auto& s : v.saturator) s.reset();
            v.driveOn = true;
        }

        // The cutoff's integrator gain at every 4th sample (and the last), linear in between: the envelope still
        // moves the cutoff at a 12 kHz control rate, with no steps, for a quarter of the exp2 and tan cost
        alignas (32) std::array<float, subBlock> cutoffG {};
        {
            alignas (32) std::array<float, subBlock> semis {};
            for (int j = 0; j < n; ++j)
            {
                const float frac = (float) (j + 1) / (float) n;
                semis[(size_t) j] = cutoff0 + (cutoff1 - cutoff0) * frac + trackSemis + v.envelope.next (envelope) * envDepth;
            }
            auto gAt = [&] (int j) { return tptGain (440.0f * fastExp2 ((semis[(size_t) j] - 69.0f) / 12.0f), sr); };
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
        // Drive's makeup at the sub-block ends, linear in between (d ramps linearly, so this is smooth)
        const float pre0 = 1.0f + 15.0f * d0 * d0, pre1 = 1.0f + 15.0f * d1 * d1;
        const float post0 = v.driveOn ? 1.0f / std::pow (pre0, 0.6f) : 1.0f, post1 = v.driveOn ? 1.0f / std::pow (pre1, 0.6f) : 1.0f;

        for (int j = 0; j < n; ++j)
        {
            const float frac = (float) (j + 1) / (float) n;
            const float g = cutoffG[(size_t) j];
            float pre = 1.0f, post = 1.0f, wet = 0.0f;
            if (v.driveOn)
            {
                const float d = d0 + (d1 - d0) * frac;
                pre = 1.0f + 15.0f * d * d;
                post = post0 + (post1 - post0) * frac;
                wet = juce::jmin (1.0f, d * 20.0f);
            }
            ZdfSvf::Coeffs svfCoeffs {};
            ZdfLadder::Coeffs ladderCoeffs {};
            if (v.filterType == 0)
                ladderCoeffs = ZdfLadder::Coeffs::make (g, filterK);
            else
                svfCoeffs = ZdfSvf::Coeffs::make (g, filterK);

            std::array<float, 2> x { mixL[(size_t) j], mixR[(size_t) j] };
            for (int c = 0; c < 2; ++c)
            {
                auto& ch = x[(size_t) c];
                if (v.driveOn)
                    ch += wet * (post * v.saturator[(size_t) c].process (pre * ch) - ch);
                switch (v.filterType)
                {
                    case 0:  ch = 2.0f * v.ladder[(size_t) c].process (0.5f * ch * (1.0f + 0.5f * filterK), ladderCoeffs); break;
                    case 1:  ch = v.svf[(size_t) c].process (ch, svfCoeffs).low; break;
                    case 2:  ch = v.svf[(size_t) c].process (ch, svfCoeffs).band * filterK; break;
                    default: ch = v.svf[(size_t) c].process (ch, svfCoeffs).high; break;
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

    static float wave2Sample (int type, float t, float dt, float pw) noexcept
    {
        switch (type)
        {
            case 1:  return blep::wave<blep::pulse> (t, dt, pw);
            case 2:  return blep::wave<blep::triangle> (t, dt, pw);
            default: return blep::wave<blep::saw> (t, dt, pw);
        }
    }

    float sr = 48000.0f, tune = 0.0f;
    std::array<float, numUsedSlots> target = defaults();
    AdsrSettings envelope;
    std::vector<Voice> voices;
};
}
