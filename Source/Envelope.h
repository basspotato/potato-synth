#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace ps
{
// Per-layer ADSR settings shared by every voice of the layer. Recomputed once per block.
// Segments are exponential approaches to an overshooting target (analog style), so each segment ends in
// finite time and a retrigger starts from the current level.
struct AdsrSettings
{
    void set (double sampleRate, float attackMs, float decayMs, float sustainLevel, float releaseMs)
    {
        attack  = segment (sampleRate, attackMs, attackRatio);
        decay   = segment (sampleRate, decayMs, decayRatio);
        release = segment (sampleRate, releaseMs, decayRatio);
        sustain = juce::jlimit (0.0f, 1.0f, sustainLevel);
        sustainCoef = (float) (1.0 - std::exp (-1.0 / (0.005 * sampleRate)));   // 5 ms glide when sustain moves
        stealStep = (float) (1.0 / (0.003 * sampleRate));                       // 3 ms linear fade
    }

    struct Segment { float coef = 0.0f, oneMinusCoef = 1.0f; };

    static constexpr float attackRatio = 0.3f;    // gentle convex attack
    static constexpr float decayRatio = 1.0e-4f;  // close to a true exponential decay

    Segment attack, decay, release;
    float sustain = 1.0f, sustainCoef = 1.0f, stealStep = 1.0f;

private:
    static Segment segment (double sampleRate, float ms, float ratio)
    {
        const double samples = juce::jmax (1.0, ms * 0.001 * sampleRate);
        const auto coef = (float) std::exp (-std::log ((1.0 + ratio) / ratio) / samples);
        return { coef, 1.0f - coef };
    }
};

// One voice's amplitude envelope.
class AmpEnvelope
{
public:
    void trigger() noexcept   { stage = Stage::attack; }   // from the current level: retriggers never click
    void release() noexcept   { if (stage != Stage::idle && stage != Stage::steal) stage = Stage::release; }
    void steal() noexcept     { if (stage != Stage::idle) stage = Stage::steal; }
    void reset() noexcept     { stage = Stage::idle; level = 0.0f; }

    bool isActive() const noexcept   { return stage != Stage::idle; }
    bool isStealing() const noexcept { return stage == Stage::steal; }
    float getLevel() const noexcept  { return level; }

    float next (const AdsrSettings& s) noexcept
    {
        switch (stage)
        {
            case Stage::idle: break;
            case Stage::attack:
                level = (1.0f + AdsrSettings::attackRatio) * s.attack.oneMinusCoef + level * s.attack.coef;
                if (level >= 1.0f) { level = 1.0f; stage = Stage::decay; }
                break;
            case Stage::decay:
                if (level <= s.sustain) { stage = Stage::sustain; break; }   // sustain was raised mid-decay
                level = (s.sustain - AdsrSettings::decayRatio) * s.decay.oneMinusCoef + level * s.decay.coef;
                if (level <= s.sustain) { level = s.sustain; stage = Stage::sustain; }
                break;
            case Stage::sustain:
                level += (s.sustain - level) * s.sustainCoef;
                break;
            case Stage::release:
                level = -AdsrSettings::decayRatio * s.release.oneMinusCoef + level * s.release.coef;
                if (level <= 0.0f) reset();
                break;
            case Stage::steal:
                level -= s.stealStep;
                if (level <= 0.0f) reset();
                break;
        }
        return level;
    }

private:
    enum class Stage { idle, attack, decay, sustain, release, steal };
    Stage stage = Stage::idle;
    float level = 0.0f;
};
}
