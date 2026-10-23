#pragma once
#include "Params.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace ps
{
// Group/Link editing (Nord Group style). While a control is being edited (between its begin and end gesture, so
// host automation never drags the others along) on a layer whose Link is on, the same control on every other
// linked layer follows: to the same value in Absolute mode, by the same amount (in the control's normalised
// range, clamped) in Relative mode. Covers amp envelope, pan, tune, glide, level, filter, drive, chorus, inserts,
// zones and engine slots; role, voice mode, anchor, mute, solo and Link itself stay per layer. An engine slot only
// follows between layers running the same engine. Message thread only.
class LayerLink : private juce::AudioProcessorParameter::Listener
{
public:
    // sameEngine (a, b): whether two layers run the same engine
    LayerLink (juce::AudioProcessorValueTreeState& state, std::function<bool (int, int)> sameEngine)
        : apvts (state), sameEngineFn (std::move (sameEngine))
    {
        using namespace params;
        const std::vector<juce::ParameterID (*)(int)> plain {
            layerLevel, layerPan, layerOctave, layerSemi, layerFine, layerAttack, layerDecay, layerSustain, layerRelease,
            layerGlide, layerFilterType, layerCutoff, layerResonance, layerFilterEnv, layerKeyTrack, layerFilterAttack,
            layerFilterDecay, layerFilterSustain, layerFilterRelease, layerDrive, layerChorus, layerChorusRate,
            layerChorusDepth, layerInsertSwap, layerKeyLow, layerKeyHigh, layerKeyFade, layerVelLow, layerVelHigh, layerVelFade };
        std::vector<std::function<juce::ParameterID (int)>> ids (plain.begin(), plain.end());
        for (int k = 0; k < numInsertSlots; ++k)
        {
            ids.push_back ([k] (int i) { return layerInsertType (i, k); });
            ids.push_back ([k] (int i) { return layerInsertAmount (i, k); });
            for (int c = 0; c < 3; ++c)
                ids.push_back ([k, c] (int i) { return layerInsertControl (i, k, c); });
        }
        const size_t numPlain = ids.size();
        for (int s = 0; s < numEngineSlots; ++s)
            ids.push_back ([s] (int i) { return layerSlot (i, s); });

        numKinds = (int) ids.size();
        controls.resize ((size_t) (numLayers * numKinds));
        for (int i = 0; i < numLayers; ++i)
            for (int k = 0; k < numKinds; ++k)
            {
                auto* p = apvts.getParameter (ids[(size_t) k] (i).getParamID());
                jassert (p != nullptr);
                auto& c = controls[(size_t) (i * numKinds + k)];
                c = { p, i, k, k >= (int) numPlain, p->getValue(), false };
                byIndex[p->getParameterIndex()] = i * numKinds + k;
                p->addListener (this);
            }
        for (int i = 0; i < numLayers; ++i)
            links[(size_t) i] = apvts.getRawParameterValue (layerLink (i).getParamID());
        mode = apvts.getRawParameterValue (linkMode.getParamID());
    }

    ~LayerLink() override
    {
        for (auto& c : controls)
            c.param->removeListener (this);
    }

    // Brings every control's remembered value up to date (after a preset or state load), so Relative mode's next
    // step starts from where the control is
    void resync()
    {
        for (auto& c : controls)
            c.last = c.param->getValue();
    }

private:
    struct Control
    {
        juce::RangedAudioParameter* param = nullptr;
        int layer = 0, kind = 0;
        bool engineSlot = false;
        float last = 0.0f;
        bool gesturing = false;
    };

    void parameterGestureChanged (int index, bool starting) override
    {
        if (auto it = byIndex.find (index); it != byIndex.end())
            controls[(size_t) it->second].gesturing = starting;
    }

    void parameterValueChanged (int index, float value) override
    {
        if (applying)
            return;
        const auto it = byIndex.find (index);
        if (it == byIndex.end())
            return;
        auto& c = controls[(size_t) it->second];
        const float delta = value - c.last;
        c.last = value;
        if (! c.gesturing || links[(size_t) c.layer]->load() < 0.5f)
            return;
        const bool relative = mode->load() > 0.5f;
        applying = true;
        for (int j = 0; j < params::numLayers; ++j)
        {
            if (j == c.layer || links[(size_t) j]->load() < 0.5f || (c.engineSlot && ! sameEngineFn (c.layer, j)))
                continue;
            auto& other = controls[(size_t) (j * numKinds + c.kind)];
            const float target = relative ? juce::jlimit (0.0f, 1.0f, other.param->getValue() + delta) : value;
            other.param->beginChangeGesture();
            other.param->setValueNotifyingHost (target);
            other.param->endChangeGesture();
            other.last = other.param->getValue();
        }
        applying = false;
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::function<bool (int, int)> sameEngineFn;
    int numKinds = 0;
    std::vector<Control> controls;
    std::unordered_map<int, int> byIndex;
    std::array<std::atomic<float>*, params::numLayers> links {};
    std::atomic<float>* mode = nullptr;
    bool applying = false;
};
}
