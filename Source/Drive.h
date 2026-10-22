#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <memory>

namespace ps
{
// tanh saturation with first-order antiderivative anti-aliasing (ADAA, Parker et al. 2016): the output is
// the average of tanh over the segment between consecutive inputs, which suppresses most of the aliasing a
// plain tanh makes at the base rate. Adds half a sample of delay. One instance per channel.
class AdaaTanh
{
public:
    void reset() noexcept { x1 = 0.0; f1 = 0.0; }

    float process (float input) noexcept
    {
        const double x = input;
        const double f = logCosh (x);
        const double dx = x - x1;
        const double y = std::abs (dx) > 1.0e-5 ? (f - f1) / dx : std::tanh (0.5 * (x + x1));
        x1 = x;
        f1 = f;
        return (float) y;
    }

private:
    // log(cosh x) without overflow
    static double logCosh (double x) noexcept
    {
        const double a = std::abs (x);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
    }

    double x1 = 0.0, f1 = 0.0;
};

// A layer's drive: parallel ADAA tanh at 2x (polyphase IIR half-bands). pre = 1 + 15 d^2 into the curve,
// post = pre^-0.6 back out, so the wet path stays near the dry level as it saturates (its small-signal gain pre^0.4 rises monotonically), and the wet share
// reaches 100 % by d = 5 %. The oversampler runs on every sounding layer, driven or not, so all layers keep
// the same phase at the crossovers and switching the drive in never steps.
class LayerDrive
{
public:
    void prepare (int maxBlockSize)
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampler->initProcessing ((size_t) std::max (1, maxBlockSize));
        reset();
    }

    void reset()
    {
        if (oversampler != nullptr)
            oversampler->reset();
        for (auto& t : tanh)
            t.reset();
        amount = 0.0f;
    }

    float getLatencySamples() const { return oversampler != nullptr ? oversampler->getLatencyInSamples() : 0.0f; }

    // d: 0..1, ramped from the last block's value. beforeDrive and afterDrive (float* left, float* right, int n)
    // see the 2x signal around the curve; afterDrive is where the layer's insert slots run.
    void process (float* left, float* right, int numSamples, float d) noexcept
    {
        auto none = [] (float*, float*, int) {};
        process (left, right, numSamples, d, none, none);
    }

    template <typename Before, typename After>
    void process (float* left, float* right, int numSamples, float d, Before&& beforeDrive, After&& afterDrive) noexcept
    {
        float* io[2] = { left, right };
        juce::dsp::AudioBlock<float> block (io, 2, (size_t) numSamples);
        auto up = oversampler->processSamplesUp (block);
        beforeDrive (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples());
        const float from = amount;
        amount = d;
        if (from > 1.0e-5f || d > 1.0e-5f)
        {
            const int n = (int) up.getNumSamples();
            // While the amount moves, post follows pre exactly (interpolating it separately overshoots the slope)
            const bool moving = from < d || from > d;
            const float postFixed = std::pow (1.0f + 15.0f * d * d, -0.6f);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* x = up.getChannelPointer ((size_t) ch);
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) (i + 1) / (float) n, a = from + (d - from) * t;
                    const float pre = 1.0f + 15.0f * a * a, post = moving ? std::pow (pre, -0.6f) : postFixed;
                    const float wet = std::min (1.0f, a * 20.0f);
                    x[i] += wet * (post * tanh[(size_t) ch].process (pre * x[i]) - x[i]);
                }
            }
        }
        else
        {
            for (auto& t : tanh)
                t.reset();
        }
        afterDrive (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples());
        oversampler->processSamplesDown (block);
    }

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::array<AdaaTanh, 2> tanh;
    float amount = 0.0f;
};
}

