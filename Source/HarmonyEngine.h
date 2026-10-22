#pragma once
#include "Loudness.h"
#include "Filter.h"
#include "Params.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// The Harmony Engine (design: potato-synth-CHAIN-KNOBS.md, section 3): a control-rate pass that keeps the
// stack inside its invariants while the chain knobs move it. This part holds invariant 1, constant loudness.
// Invariants 2 (true peak, StackBus.h's clipper and limiter) and 3 (mono lows, the mono-bass stage) are held
// by the bus itself and checked by the knob-sweep tests.
namespace ps::harmony
{
// Short-term K-weighted power (BS.1770 weighting) of a stereo signal, averaged over about 400 ms, over the band
// that reaches the output: an LR4 low-pass at 19 kHz stands in for the output oversampler's half-band filters,
// which remove the top of the spectrum (and every harmonic a 4x stage makes above it). A meter that counted
// that energy would see a knob that brightens the top as louder than it ends up. Real-time safe.
class KPowerMeter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const auto k = loudness::kWeighting (sampleRate);
        for (int s = 0; s < 2; ++s)
        {
            c[(size_t) s] = { k[(size_t) s].b0, k[(size_t) s].b1, k[(size_t) s].b2, k[(size_t) s].a1, k[(size_t) s].a2 };
        }
        // Butterworth low-pass sections (bilinear), two of them make the LR4
        const double w = std::tan (juce::MathConstants<double>::pi * std::min (19000.0, 0.45 * sampleRate) / sampleRate);
        const double q = juce::MathConstants<double>::sqrt2 * 0.5, a0 = 1.0 + w / q + w * w;
        const Coeffs lp { w * w / a0, 2.0 * w * w / a0, w * w / a0, 2.0 * (w * w - 1.0) / a0, (1.0 - w / q + w * w) / a0 };
        c[2] = c[3] = lp;
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
            const double l = weigh (left[i], 0), r = weigh (right[i], 1);
            sum += l * l + r * r;
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

    // Starts the average at a known power (a meter switched in beside a running one), not at silence
    void seed (float p) noexcept { power = p; }

private:
    // Double precision: at the 4x rate the 38 Hz high-pass's poles sit within 2e-3 of z = 1, where float
    // coefficients and state misread the lows
    struct Coeffs { double b0, b1, b2, a1, a2; };
    struct State { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

    static constexpr int numStages = 4;   // K pre-filter, RLB high-pass, 2 x low-pass

    double weigh (double x, int ch) noexcept
    {
        for (int s = 0; s < numStages; ++s)
        {
            auto& z = state[(size_t) ch][(size_t) s];
            const auto& k = c[(size_t) s];
            const double y = k.b0 * x + k.b1 * z.x1 + k.b2 * z.x2 - k.a1 * z.y1 - k.a2 * z.y2;
            z.x2 = z.x1; z.x1 = x; z.y2 = z.y1; z.y1 = y;
            x = y;
        }
        return x;
    }

    void flushDenormals() noexcept
    {
        for (auto& ch : state)
            for (auto& z : ch)
                if (std::abs (z.y1) < 1.0e-30 && std::abs (z.y2) < 1.0e-30)
                    z = {};
    }

    double sr = 48000.0;
    std::array<Coeffs, numStages> c {};
    std::array<std::array<State, numStages>, 2> state {};
    float power = 0.0f;
    double blockSum = 0.0;
    int blockCount = 0;
};

// P_in / P_out of a measured stage, or 1 while either side is too quiet to tell
inline double stageRatio (const KPowerMeter& in, const KPowerMeter& out) noexcept
{
    constexpr float floor = 1.0e-10f;
    return in.getPower() > floor && out.getPower() > floor ? (double) in.getPower() / out.getPower() : 1.0;
}

// Invariant 1: the chain knobs never change the loudness. The trim compensates only what the knobs change,
// never the player's dynamics, by comparing the stack with and without the knobs' effect:
//
//   layer gains:  each layer's K-weighted power P_l is measured at unity gain, so the knobs' level change is
//                 known exactly:  ratio_layers = sum P_l g_l^2 / sum P_l (g_l c_l)^2   (g: patch, c: knob gain)
//   bus stages:   a stage the knobs drive (MASS's low focus EQ, PRESSURE's dynamics and clipping) is measured
//                 in and out: ratio_stage = P_in / P_out, which is 1 when the knob is at rest
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
        smoothed = 1.0f;
        juce::ignoreUnused (maxBlockSize);
    }

    void reset() { smoothed = 1.0f; }

    // Once per block. Powers are K-weighted (KPowerMeter). stageRatio is the product of P_in / P_out over the bus
    // stages the knobs drive (1 when none is working).
    void update (const float* layerPower, const float* patchGain, const float* knobGain, int numLayers,
                 double stageRatio, int numSamples) noexcept
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
        if (std::abs (stageRatio - 1.0) > 1.0e-9)
        {
            ratio *= stageRatio;
            valid = true;
        }
        if (valid)
        {
            const float limit = juce::Decibels::decibelsToGain (maxTrimDb);
            const float target = juce::jlimit (1.0f / limit, limit, (float) std::sqrt (ratio));
            const auto coef = (float) (1.0 - std::exp (-numSamples / (0.3 * sr)));
            smoothed += (target - smoothed) * coef;
        }
    }

    // The trim, smoothed over 300 ms; the output clipper ramps it across each block (StackBus.h)
    float getGain() const noexcept { return smoothed; }

private:
    double sr = 48000.0;
    float smoothed = 1.0f;
};

// Invariant 4, band ownership (CLARITY). Where two layers fight over the same band (the mud zone, 200 Hz to
// 3.2 kHz, in octave bands), the layer that doesn't own it is dipped, so each band has one clear owner.
// Each layer runs a CarveBank: five cascaded bells (y = x + (g - 1) k bp, unit peak band-pass, so g is exactly
// the gain at the centre), whose band-pass outputs also measure the layer's energy per band. The solver runs
// once per block on those energies.
class CarveBank
{
public:
    static constexpr int numBands = 5;
    static constexpr float centresHz[numBands] = { 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f };
    static constexpr float k = 0.7071f;   // 1/Q, Q = 1.41: one octave wide

    void prepare (double sampleRate)
    {
        for (size_t b = 0; b < (size_t) numBands; ++b)
            g[b] = tptGain (centresHz[b], (float) sampleRate);
        rate = (float) sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& ch : filters)
            for (auto& f : ch)
                f.reset();
        gain.fill (1.0f);
        energy.fill (0.0f);
        active = false;
    }

    // targets: each band's gain (linear, <= 1). Ramps from the last block's gains. analyse keeps the band
    // energies measured while flat (CLARITY is up but nothing is contested yet). Returns false when bypassed.
    bool process (float* left, float* right, int numSamples, const std::array<float, numBands>& targets, bool analyse) noexcept
    {
        // The solver's targets glide over 50 ms, so a band changing hands never pumps
        const float coef = 1.0f - std::exp (-(float) numSamples / (0.05f * rate));
        std::array<float, numBands> next {};
        bool flat = true;
        for (size_t b = 0; b < (size_t) numBands; ++b)
        {
            next[b] = gain[b] + (targets[b] - gain[b]) * coef;
            if (std::abs (next[b] - 1.0f) < 1.0e-4f && std::abs (targets[b] - 1.0f) < 1.0e-5f)
                next[b] = 1.0f;
            flat = flat && next[b] == 1.0f && gain[b] == 1.0f;
        }
        if (flat && ! analyse)
        {
            if (active)
                reset();
            return false;
        }
        active = true;
        std::array<float, numBands> step {}, sum {};
        for (size_t b = 0; b < (size_t) numBands; ++b)
            step[b] = (next[b] - gain[b]) / (float) numSamples;
        float* io[2] = { left, right };
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = filters[(size_t) ch];
            for (int i = 0; i < numSamples; ++i)
            {
                float x = io[ch][i];
                for (size_t b = 0; b < (size_t) numBands; ++b)
                {
                    const float band = k * f[b].process (x, g[b], k).band;
                    sum[b] += band * band;
                    x += (gain[b] + step[b] * (float) (i + 1) - 1.0f) * band;
                }
                io[ch][i] = x;
            }
        }
        for (size_t b = 0; b < (size_t) numBands; ++b)
        {
            gain[b] = next[b];
            energy[b] = sum[b] / (float) numSamples;
        }
        return true;
    }

    // Mean band power over the last block, at the input of each bell
    const std::array<float, numBands>& bandEnergy() const noexcept { return energy; }

private:
    std::array<std::array<ZdfSvf, numBands>, 2> filters {};
    std::array<float, numBands> g {}, gain {}, energy {};
    float rate = 48000.0f;
    bool active = false;
};

// Which role owns which band: role priority pi = priority x energy, shares w = pi^(1/T) / sum pi^(1/T)
// (a softmax over log pi with temperature T). Every layer but the owner is dipped by depth x (1 - w / w_owner),
// so a layer with an equal claim is barely touched and a weak claimant gives way fully. Layers more than
// 30 dB below the band's loudest aren't fighting for it and stay as they are.
inline std::array<std::array<float, CarveBank::numBands>, params::numLayers>
    solveOwnership (const std::array<std::array<float, CarveBank::numBands>, params::numLayers>& energy,
                    const std::array<params::Role, params::numLayers>& roles, const std::array<bool, params::numLayers>& sounding,
                    float temperature, float depthDb) noexcept
{
    //                                     200    400    800   1.6k   3.2k
    static constexpr float priority[5][CarveBank::numBands] = { { 0.5f, 0.2f, 0.1f, 0.05f, 0.05f },   // Sub
                                                                { 1.0f, 1.0f, 0.6f, 0.3f, 0.2f },     // Body
                                                                { 0.4f, 0.6f, 1.0f, 1.0f, 0.7f },     // Mid
                                                                { 0.1f, 0.2f, 0.4f, 0.7f, 1.0f },     // Air
                                                                { 0.6f, 0.6f, 0.6f, 0.6f, 0.6f } };   // Full
    std::array<std::array<float, CarveBank::numBands>, params::numLayers> gains {};
    for (auto& l : gains)
        l.fill (1.0f);
    const float depth = 1.0f - juce::Decibels::decibelsToGain (-juce::jmax (0.0f, depthDb));
    if (depth <= 0.0f)
        return gains;
    const float invT = 1.0f / juce::jmax (0.05f, temperature);
    for (size_t b = 0; b < (size_t) CarveBank::numBands; ++b)
    {
        float loudest = 0.0f;
        for (size_t l = 0; l < (size_t) params::numLayers; ++l)
            if (sounding[l])
                loudest = std::max (loudest, energy[l][b]);
        if (loudest < 1.0e-12f)
            continue;
        std::array<float, params::numLayers> claim {};
        float top = 0.0f;
        int contenders = 0;
        for (size_t l = 0; l < (size_t) params::numLayers; ++l)
        {
            if (! sounding[l] || energy[l][b] < loudest * 1.0e-3f)
                continue;
            ++contenders;
            // pi^(1/T), relative to the loudest so it stays in range
            claim[l] = std::pow (priority[(size_t) roles[l]][b] * energy[l][b] / loudest, invT);
            top = std::max (top, claim[l]);
        }
        if (contenders < 2 || top <= 0.0f)
            continue;
        for (size_t l = 0; l < (size_t) params::numLayers; ++l)
            if (claim[l] > 0.0f)
                gains[l][b] = 1.0f - depth * (1.0f - claim[l] / top);
    }
    return gains;
}
}

