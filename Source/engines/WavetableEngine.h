#pragma once
#include "Engine.h"
#include "Glide.h"
#include "Filter.h"
#include "Simd.h"
#include "Unison.h"
#include "Wavetables.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ps
{
// Wavetable: a factory table scanned by Position (with its own decaying Position envelope for motion),
// through JP or Modern unison. Each voice reads the band-limited copy of its frames whose top harmonic stays
// under the alias floor (Wavetables::levelFor), interpolating between neighbouring frames and samples, so
// sweeps are smooth and the oscillator never aliases into the audible band.
//
// Discrete switches (table, unison mode) dip the voice for 3 ms each way instead of clicking.
class WavetableEngine final : public Engine
{
public:
    enum Slot { table, position, posEnv, posEnvTime, unison, detune, mix, spread, phase, numUsedSlots };

    WavetableEngine() : bank (Wavetables::get()) {}

    const EngineInfo& info() const override
    {
        static const EngineInfo i { "wavetable", "Wavetable", 16, makeSlots() };
        return i;
    }

    void prepare (double sampleRate, int, int numVoices) override
    {
        sr = (float) sampleRate;
        voices.assign ((size_t) numVoices, {});
        for (auto& v : voices)
            v.rng = 0x2545F491u * (uint32_t) (&v - voices.data() + 1);
    }

    void reset() override
    {
        for (auto& v : voices)
            v.active = false;
    }

    void noteOn (int index, const NoteEvent& e) override
    {
        auto& v = voices[(size_t) index];
        if (! v.active)
        {
            v.pitch.jumpTo (e.glideSeconds > 0.0 && e.glideFrom >= 0.0f ? e.glideFrom : (float) e.note);
            const bool randomPhase = target[phase] < 0.5f;
            for (auto& p : v.phase)
                p = randomPhase ? random (v) : 0.0f;
            v.smooth = target;
            v.tableIndex = choice (target[table], bank.size());
            v.unisonMode = choice (target[unison], numUnison);
            v.layoutDetune = -1.0f;
            updateLayout (v);
            v.gain = v.layout.gain;
            v.switchGain = 1.0f;
            v.switching = false;
            v.level = -1;
            v.active = true;
        }
        if (! e.legato)
            v.env = 1.0f;
        v.pitch.glideTo ((float) e.note, e.glideSeconds, sr);
    }

    void noteOff (int) override {}
    void endVoice (int index) override { voices[(size_t) index].active = false; }
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
        for (int pos = 0; pos < numSamples; pos += subBlock)
            renderSubBlock (v, left + pos, right + pos, juce::jmin (subBlock, numSamples - pos));
    }

    // ---- Slot mappings -----------------------------------------------------------------------------------
    static constexpr int numUnison = 5;
    static int choice (float v, int n) noexcept      { return juce::jlimit (0, n - 1, (int) (v * (float) n)); }
    static float choiceValue (int i, int n) noexcept { return ((float) i + 0.5f) / (float) n; }
    static float envMs (float v) noexcept            { return 5.0f + 4995.0f * v * v * v; }
    static float envValue (float ms) noexcept        { return std::cbrt ((ms - 5.0f) / 4995.0f); }

private:
    static constexpr int subBlock = 16;
    static constexpr float voiceLevel = 0.25f;   // sine-RMS frames at -15 dBFS RMS, like the other engines

    struct Voice
    {
        bool active = false;
        PitchGlide pitch;
        uint32_t rng = 1;
        std::array<float, UnisonLayout::maxVoices> phase {}, gain {}, panL {}, panR {};
        UnisonLayout layout;
        std::array<float, numUsedSlots> smooth {};
        float layoutDetune = -1.0f, layoutMix = -1.0f, layoutSpread = -1.0f;
        float env = 0.0f;   // Position envelope, 1 at the attack, decaying to 0
        int tableIndex = 0, unisonMode = 0, level = -1;
        float switchGain = 1.0f;
        bool switching = false;
    };

    static std::vector<SlotInfo> makeSlots()
    {
        auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        const auto names = Wavetables::get().names();
        std::vector<SlotInfo> s ((size_t) numUsedSlots);
        s[table]      = { "Table", choiceValue (0, names.size()), [names] (float v) { return names[choice (v, names.size())]; } };
        s[position]   = { "Position", 0.0f, percent };
        s[posEnv]     = { "Pos Env", 0.5f, [] (float v)
                          { const int p = juce::roundToInt ((v * 2.0f - 1.0f) * 100.0f); return (p > 0 ? "+" : "") + juce::String (p) + " %"; } };
        s[posEnvTime] = { "Pos Env Time", envValue (500.0f), [] (float v)
                          { const float ms = envMs (v); return ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String (ms * 0.001f, 2) + " s"; } };
        s[unison]     = { "Unison", choiceValue (2, numUnison), [] (float v)
                          { static const char* n[] = { "Off", "JP 7", "Modern 8", "Modern 12", "Modern 16" }; return juce::String (n[choice (v, numUnison)]); } };
        s[detune]     = { "Detune", 0.4f, percent };
        s[mix]        = { "Mix", 0.7f, percent };
        s[spread]     = { "Spread", 0.7f, percent };
        s[phase]      = { "Phase", 0.25f, [] (float v) { return juce::String (v < 0.5f ? "Random" : "Retrig"); } };
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

    static float random (Voice& v) noexcept
    {
        v.rng ^= v.rng << 13;
        v.rng ^= v.rng >> 17;
        v.rng ^= v.rng << 5;
        return (float) (v.rng >> 8) * (1.0f / 16777216.0f);
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

    void renderSubBlock (Voice& v, float* outL, float* outR, int n) noexcept
    {
        const float coef = 1.0f - std::exp (-(float) n / (0.02f * sr));
        const float positionBefore = framePosition (v);
        for (size_t i = 0; i < v.smooth.size(); ++i)
            v.smooth[i] += (target[i] - v.smooth[i]) * coef;
        updateLayout (v);

        if (! v.switching && (v.tableIndex != choice (target[table], bank.size()) || v.unisonMode != choice (target[unison], numUnison)))
            v.switching = true;

        // Pitch per sample while gliding
        std::array<float, subBlock> inc {};
        auto incrementFor = [this] (float pitch) { return 440.0f * fastExp2 ((pitch + tune - 69.0f) / 12.0f) / sr; };
        if (v.pitch.isGliding())
            for (int j = 0; j < n; ++j)
                inc[(size_t) j] = incrementFor (v.pitch.next());
        else
            std::fill (inc.begin(), inc.begin() + n, incrementFor (v.pitch.getCurrent()));

        // Position: knob plus envelope, ramped across the sub-block
        v.env *= std::exp (-(float) n / (envMs (v.smooth[posEnvTime]) * 0.001f * sr));
        const float positionAfter = framePosition (v);

        // One band limit for the sub-block, safe for the highest unison voice at the highest pitch in it
        float maxRatio = 1.0f;
        for (int k = 0; k < v.layout.count; ++k)
            maxRatio = juce::jmax (maxRatio, v.layout.ratio[(size_t) k]);
        const float topInc = *std::max_element (inc.begin(), inc.begin() + n);
        // When the band limit changes (a glide, more detune), crossfade to the new copy across the sub-block
        const int level = Wavetables::levelFor (topInc * maxRatio * sr, sr);
        const int fromLevel = v.level < 0 ? level : v.level;
        v.level = level;
        const auto& tbl = bank.table (v.tableIndex);

        // Unison voices run in SIMD lanes; each lane reads its own phase from the shared frames
        using simd::F;
        std::array<float, subBlock> sumL {}, sumR {};
        const float* frameA[2] = { tbl.frame (0, level), tbl.frame (0, fromLevel) };
        const bool crossfadeLevels = fromLevel != level;
        const float invN = 1.0f / (float) n;
        for (int k0 = 0; k0 < v.layout.count; k0 += F::width)
        {
            const auto ku = (size_t) k0;
            F t = F::load (&v.phase[ku]), ratio = F::load (&v.layout.ratio[ku]), g = F::load (&v.gain[ku]);
            const F gStep = (F::load (&v.layout.gain[ku]) - g) * F (invN);
            const F pl = F::load (&v.panL[ku]), pr = F::load (&v.panR[ku]);
            std::array<F, subBlock> accL, accR;
            accL.fill (F (0.0f));
            accR.fill (F (0.0f));
            for (int j = 0; j < n; ++j)
            {
                const float framePos = positionBefore + (positionAfter - positionBefore) * (float) (j + 1) * invN;
                const int f0 = juce::jmin (Wavetables::numFrames - 2, (int) framePos);
                const F fm (framePos - (float) f0);
                const size_t frameOffset = (size_t) f0 * Wavetables::numLevels * Wavetables::stride;
                const size_t nextFrame = (size_t) Wavetables::numLevels * Wavetables::stride;

                t = t + simd::min (F (0.45f), F (inc[(size_t) j]) * ratio);
                t = t - simd::select (t >= F (1.0f), F (1.0f), F (0.0f));
                const F idx = t * F ((float) Wavetables::frameSize);
                const simd::I i0 = simd::truncate (idx);
                const F frac = idx - simd::toFloat (i0);
                auto read = [&] (const float* frame)
                {
                    const F a = hermite (frame + frameOffset, i0, frac);
                    const F b = hermite (frame + frameOffset + nextFrame, i0, frac);
                    return a + (b - a) * fm;
                };
                F x = read (frameA[0]);
                if (crossfadeLevels)
                {
                    const F old = read (frameA[1]);
                    x = old + (x - old) * F ((float) (j + 1) * invN);
                }
                g = g + gStep;
                x = x * g;
                accL[(size_t) j] = x * pl;
                accR[(size_t) j] = x * pr;
            }
            for (int j = 0; j < subBlock; j += 8)
            {
                simd::addSums8 (&accL[(size_t) j], &sumL[(size_t) j]);
                simd::addSums8 (&accR[(size_t) j], &sumR[(size_t) j]);
            }
            t.store (&v.phase[ku]);
        }
        v.gain = v.layout.gain;

        // The sums above were made with the old setting, so once the dip reaches zero the rest of this
        // sub-block stays silent; the new setting rises from zero in the next one
        const float switchStep = 1.0f / (0.003f * sr);
        bool switchedHere = false;
        for (int j = 0; j < n; ++j)
        {
            if (switchedHere)
            {
            }
            else if (v.switching)
            {
                v.switchGain -= switchStep;
                if (v.switchGain <= 0.0f)
                {
                    v.switchGain = 0.0f;
                    v.switching = false;
                    v.tableIndex = choice (target[table], bank.size());
                    v.unisonMode = choice (target[unison], numUnison);
                    v.layoutDetune = -1.0f;
                    updateLayout (v);
                    v.gain = v.layout.gain;
                    switchedHere = true;
                }
            }
            else if (v.switchGain < 1.0f)
            {
                v.switchGain = juce::jmin (1.0f, v.switchGain + switchStep);
            }
            outL[j] += sumL[(size_t) j] * voiceLevel * v.switchGain;
            outR[j] += sumR[(size_t) j] * voiceLevel * v.switchGain;
        }
    }

    // 4-point cubic Hermite: its interpolation images sit about 10 dB below linear's on the brightest tables
    static simd::F hermite (const float* x, simd::I i, simd::F t) noexcept
    {
        using simd::F;
        F xm, x0, x1, x2;
        simd::load4 (x, i, xm, x0, x1, x2);
        const F c1 = F (0.5f) * (x1 - xm), c3 = F (1.5f) * (x0 - x1) + F (0.5f) * (x2 - xm), c2 = xm - x0 + c1 - c3;
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    // Position plus envelope, in frames (0 .. numFrames - 1)
    float framePosition (const Voice& v) const noexcept
    {
        const float pos = juce::jlimit (0.0f, 1.0f, v.smooth[position] + (v.smooth[posEnv] * 2.0f - 1.0f) * v.env);
        return pos * (float) (Wavetables::numFrames - 1);
    }

    const Wavetables& bank;
    float sr = 48000.0f, tune = 0.0f;
    std::array<float, numUsedSlots> target = defaults();
    std::vector<Voice> voices;
};
}
