#pragma once
#include "Loudness.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// The Harmony Engine (design: potato-synth-CHAIN-KNOBS.md, section 3): a control-rate pass that keeps the
// stack inside its invariants while the chain knobs move it. This part holds invariant 1, constant loudness.
// Invariants 2 (true peak, StackBus.h's clipper and limiter) and 3 (mono lows, the mono-bass stage) are held
// by the bus itself and checked by the knob-sweep tests.
namespace ps::harmony
{
// Short-term K-weighted power (BS.1770 weighting) of a stereo signal, averaged over about 400 ms.
// Real-time safe; float state is enough for a slow meter.
class KPowerMeter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const auto k = loudness::kWeighting (sampleRate);
        for (int s = 0; s < 2; ++s)
        {
            c[(size_t) s] = { (float) k[(size_t) s].b0, (float) k[(size_t) s].b1, (float) k[(size_t) s].b2,
                              (float) k[(size_t) s].a1, (float) k[(size_t) s].a2 };
        }
        reset();
    }

    void reset()
    {
        state = {};
        power = 0.0f;
        blockSum = 0.0;
        blockCount = 0;
    }

    void process (const float* left, const float* right, int numSamples) noexcept
    {
        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = weigh (left[i], 0), r = weigh (right[i], 1);
            sum += (double) l * l + (double) r * r;
        }
        blockSum += sum;
        blockCount += numSamples;
    }

    // Folds the samples measured since the last call into the 400 ms average
    void update() noexcept
    {
        if (blockCount == 0)
            return;
        const auto mean = (float) (blockSum / blockCount);
        const auto coef = (float) (1.0 - std::exp (-(double) blockCount / (0.4 * sr)));
        power += (mean - power) * coef;
        flushDenormals();
        blockSum = 0.0;
        blockCount = 0;
    }

    // Skip a silent block without running the filters (an idle layer)
    void addSilence (int numSamples) noexcept { blockCount += numSamples; }

    float getPower() const noexcept { return power; }

private:
    struct Coeffs { float b0, b1, b2, a1, a2; };
    struct State { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

    float weigh (float x, int ch) noexcept
    {
        for (int s = 0; s < 2; ++s)
        {
            auto& z = state[(size_t) ch][(size_t) s];
            const auto& k = c[(size_t) s];
            const float y = k.b0 * x + k.b1 * z.x1 + k.b2 * z.x2 - k.a1 * z.y1 - k.a2 * z.y2;
            z.x2 = z.x1; z.x1 = x; z.y2 = z.y1; z.y1 = y;
            x = y;
        }
        return x;
    }

    void flushDenormals() noexcept
    {
        for (auto& ch : state)
            for (auto& z : ch)
                if (std::abs (z.y1) < 1.0e-20f && std::abs (z.y2) < 1.0e-20f)
                    z = {};
    }

    double sr = 48000.0;
    std::array<Coeffs, 2> c {};
    std::array<std::array<State, 2>, 2> state {};
    float power = 0.0f;
    double blockSum = 0.0;
    int blockCount = 0;
};

// Invariant 1: the chain knobs never change the loudness. The trim compensates only what the knobs change,
// never the player's dynamics, by comparing the stack with and without the knobs' effect:
//
//   layer gains:  each layer's K-weighted power P_l is measured at unity gain, so the knobs' level change is
//                 known exactly:  ratio_layers = sum P_l g_l^2 / sum P_l (g_l c_l)^2   (g: patch, c: knob gain)
//   bus stages:   a stage the knobs drive (e.g. MASS's low focus EQ) is measured in and out:
//                 ratio_stage = P_in / P_out, which is 1 when the knob is at rest
//
// The trim is sqrt of their product, smoothed over 300 ms. Engine slot moves are kept loudness-neutral by the
// engines themselves (power-normalised unison, translate, frames); the knob-sweep tests hold everything to
// +-0.5 LU.
class LoudnessTrim
{
public:
    static constexpr float maxTrimDb = 12.0f;

    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        gain.reset (sampleRate, 0.02);
        gain.setCurrentAndTargetValue (1.0f);
        smoothed = 1.0f;
        juce::ignoreUnused (maxBlockSize);
    }

    void reset()
    {
        gain.setCurrentAndTargetValue (1.0f);
        smoothed = 1.0f;
    }

    // Once per block. Powers are K-weighted (KPowerMeter); stageIn/stageOut may be equal when no stage is active.
    void update (const float* layerPower, const float* patchGain, const float* knobGain, int numLayers,
                 float stageIn, float stageOut, int numSamples) noexcept
    {
        double withoutKnobs = 0.0, withKnobs = 0.0;
        for (int l = 0; l < numLayers; ++l)
        {
            const double p = layerPower[l], g = patchGain[l], c = knobGain[l];
            withoutKnobs += p * g * g;
            withKnobs += p * g * g * c * c;
        }
        constexpr double floor = 1.0e-10;   // below about -100 LUFS: hold the last trim
        double ratio = 1.0;
        bool valid = false;
        if (withKnobs > floor && withoutKnobs > floor)
        {
            ratio = withoutKnobs / withKnobs;
            valid = true;
        }
        if (stageOut > floor && stageIn > floor)
        {
            ratio *= (double) stageIn / stageOut;
            valid = true;
        }
        if (valid)
        {
            const float limit = juce::Decibels::decibelsToGain (maxTrimDb);
            const float target = juce::jlimit (1.0f / limit, limit, (float) std::sqrt (ratio));
            const auto coef = (float) (1.0 - std::exp (-numSamples / (0.3 * sr)));
            smoothed += (target - smoothed) * coef;
        }
        gain.setTargetValue (smoothed);
    }

    void apply (float* left, float* right, int numSamples) noexcept
    {
        if (! gain.isSmoothing() && std::abs (gain.getTargetValue() - 1.0f) < 1.0e-6f)
            return;
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gain.getNextValue();
            left[i] *= g;
            right[i] *= g;
        }
    }

    float getGain() const noexcept { return smoothed; }

private:
    double sr = 48000.0;
    float smoothed = 1.0f;
    juce::SmoothedValue<float> gain { 1.0f };
};
}
