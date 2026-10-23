#pragma once
#include "Params.h"
#include <array>
#include <atomic>
#include <cmath>

// The stack XY pad, Mix mode: each corner belongs to a layer slot (Sub top left, Body top right, Mid bottom
// left, Air bottom right) and the centre is the full stack. The pad's bilinear corner weights b_l (summing to
// 1) set each free layer's gain
//
//   g_l^2 = (1 - e) + e N b_l          (N free layers, e = 0.75 emphasis)
//
// so sum g_l^2 = N everywhere: constant power, and the Harmony Engine's trim takes out what remains of the
// loudness change (the layers' own levels differ). In a corner its layer is up 4-5 dB and the others down 6 dB,
// never muted. An anchored layer (by default the Sub) stays at unity; the weight of its corner, and of an empty
// layer's, is shared equally by the free layers, so pushing toward it changes nothing and nothing jumps.
namespace ps::morph
{
inline constexpr float emphasis = 0.75f;

// x: 0 left .. 1 right; y: 0 bottom .. 1 top. Returns each layer's gain in dB.
// Layers 5-8 have no corner: the pad leaves them at unity, like an anchored layer.
inline std::array<float, params::numLayers> mixGainsDb (float x, float y, const std::array<bool, params::numLayers>& free) noexcept
{
    x = std::fmin (1.0f, std::fmax (0.0f, x));
    y = std::fmin (1.0f, std::fmax (0.0f, y));
    const std::array<float, 4> corner { (1.0f - x) * y, x * y, (1.0f - x) * (1.0f - y), x * (1.0f - y) };
    int n = 0;
    float freeWeight = 0.0f;
    for (int l = 0; l < 4; ++l)
        if (free[(size_t) l])
        {
            ++n;
            freeWeight += corner[(size_t) l];
        }
    std::array<float, params::numLayers> db {};
    if (n < 2)
        return db;   // one free layer (or none): nothing to balance
    const float shared = (1.0f - freeWeight) / (float) n;
    for (int l = 0; l < 4; ++l)
        if (free[(size_t) l])
        {
            const float g2 = (1.0f - emphasis) + emphasis * (float) n * (corner[(size_t) l] + shared);
            db[(size_t) l] = 10.0f * std::log10 (g2);
        }
    return db;
}
}

namespace ps::morph
{
// The XY pad, Snapshot mode (AN1x / Nord style): each corner can hold a snapshot of every layer's continuous
// settings (level, pan, fine tune, envelopes, glide, engine slots, filter, drive, chorus, insert amounts and
// controls), and the pad blends the corners bilinearly, in each parameter's normalised range (so a cutoff
// morphs on its log scale). An empty corner stands for the sound as it is set, so a single captured corner
// morphs from the live sound to it. Anchored layers and switch-like parameters (octave, semitone, role, voice
// and filter modes, insert types) keep their live values; a corner's engine slots only count while its layer
// runs the engine the corner was captured with. The host parameters are never written: the layers read the
// blended values through the pointers bind() returns. Entering and leaving Snapshot mode glides over 30 ms.
class SnapshotMorph
{
public:
    static constexpr int numCorners = 4, maxEntries = 512;

    // Message thread, before audio starts: routes one layer parameter through the morph. Returns the value the
    // layer reads from now on.
    std::atomic<float>* bind (juce::RangedAudioParameter& param, std::atomic<float>* live, int layer, bool engineSlot)
    {
        jassert (numEntries < maxEntries);
        auto& e = entries[(size_t) numEntries];
        e.param = &param;
        e.live = live;
        e.layer = layer;
        e.engineSlot = engineSlot;
        const auto* f = dynamic_cast<juce::AudioParameterFloat*> (&param);
        e.morphs = f != nullptr && f->range.interval < 1.0f;
        e.effective.store (live->load());
        return &entries[(size_t) numEntries++].effective;
    }

    // Message thread. engines: each layer's engine ID now.
    void capture (int corner, const std::array<juce::String, params::numLayers>& engines)
    {
        auto& c = corners[(size_t) corner];
        const bool live = mix.load() < 1.0e-5f;
        for (int i = 0; i < numEntries; ++i)
        {
            const auto& e = entries[(size_t) i];
            c.values[(size_t) i].store (live ? e.param->getValue() : e.param->convertTo0to1 (e.effective.load()));
        }
        c.engines = engines;
        c.filled.store (true);
        updateEngines (engines);
    }

    void clear (int corner) { corners[(size_t) corner].filled.store (false); }
    void clearAll() { for (int c = 0; c < numCorners; ++c) clear (c); }
    bool isFilled (int corner) const noexcept { return corners[(size_t) corner].filled.load(); }

    // Message thread, whenever a layer's engine changes
    void updateEngines (const std::array<juce::String, params::numLayers>& engines)
    {
        for (auto& c : corners)
            for (int l = 0; l < params::numLayers; ++l)
                c.slotsValid[(size_t) l].store (c.engines[(size_t) l] == engines[(size_t) l]);
    }

    // Audio thread, before the layers render. x, y: the glided pad position; anchored: layers the pad leaves alone.
    void process (bool snapshotMode, float x, float y, const std::array<bool, params::numLayers>& anchored, float coef) noexcept
    {
        float m = mix.load();
        m += ((snapshotMode ? 1.0f : 0.0f) - m) * coef;
        const bool out = ! snapshotMode && m < 1.0e-5f;
        if (out)
            m = 0.0f;
        mix.store (m);
        if (out)
        {
            for (int i = 0; i < numEntries; ++i)
                entries[(size_t) i].effective.store (entries[(size_t) i].live->load());
            return;
        }
        x = juce::jlimit (0.0f, 1.0f, x);
        y = juce::jlimit (0.0f, 1.0f, y);
        // Corners as the pad shows them: top left, top right, bottom left, bottom right
        const std::array<float, numCorners> w { (1.0f - x) * y, x * y, (1.0f - x) * (1.0f - y), x * (1.0f - y) };
        std::array<bool, numCorners> filled {};
        for (int c = 0; c < numCorners; ++c)
            filled[(size_t) c] = corners[(size_t) c].filled.load() && w[(size_t) c] > 0.0f;
        for (int i = 0; i < numEntries; ++i)
        {
            auto& e = entries[(size_t) i];
            const float live = e.live->load();
            if (! e.morphs || anchored[(size_t) e.layer])
            {
                e.effective.store (live);
                continue;
            }
            const float liveN = e.param->convertTo0to1 (live);
            float v = 0.0f;
            for (int c = 0; c < numCorners; ++c)
            {
                const auto& corner = corners[(size_t) c];
                const bool use = filled[(size_t) c] && (! e.engineSlot || corner.slotsValid[(size_t) e.layer].load());
                v += w[(size_t) c] * (use ? corner.values[(size_t) i].load() : liveN);
            }
            e.effective.store (e.param->convertFrom0to1 (liveN + m * (v - liveN)));
        }
    }

    // How far Snapshot mode is in (0 Mix .. 1 Snapshot), glided
    float getMix() const noexcept { return mix.load(); }

    juce::ValueTree toState() const
    {
        juce::ValueTree tree ("Snapshots");
        for (int c = 0; c < numCorners; ++c)
        {
            const auto& corner = corners[(size_t) c];
            juce::ValueTree node ("CORNER");
            node.setProperty ("filled", corner.filled.load(), nullptr);
            if (corner.filled.load())
            {
                for (int l = 0; l < params::numLayers; ++l)
                    node.setProperty ("engine" + juce::String (l), corner.engines[(size_t) l], nullptr);
                for (int i = 0; i < numEntries; ++i)
                    node.setProperty (entries[(size_t) i].param->getParameterID(), corner.values[(size_t) i].load(), nullptr);
            }
            tree.appendChild (node, nullptr);
        }
        return tree;
    }

    // A parameter the saved corner doesn't name takes its value at load
    void fromState (const juce::ValueTree& tree, const std::array<juce::String, params::numLayers>& engines)
    {
        for (int c = 0; c < numCorners; ++c)
        {
            auto& corner = corners[(size_t) c];
            const auto node = tree.getChild (c);
            if (! (bool) node.getProperty ("filled", false))
            {
                corner.filled.store (false);
                continue;
            }
            for (int l = 0; l < params::numLayers; ++l)
                corner.engines[(size_t) l] = node.getProperty ("engine" + juce::String (l)).toString();
            for (int i = 0; i < numEntries; ++i)
            {
                const auto& e = entries[(size_t) i];
                corner.values[(size_t) i].store ((float) node.getProperty (e.param->getParameterID(), e.param->getValue()));
            }
            corner.filled.store (true);
        }
        updateEngines (engines);
    }

private:
    struct Entry
    {
        juce::RangedAudioParameter* param = nullptr;
        std::atomic<float>* live = nullptr;
        std::atomic<float> effective { 0.0f };
        int layer = 0;
        bool engineSlot = false, morphs = false;
    };
    struct Corner
    {
        std::array<std::atomic<float>, maxEntries> values {};
        std::array<std::atomic<bool>, params::numLayers> slotsValid {};
        std::array<juce::String, params::numLayers> engines;   // message thread only
        std::atomic<bool> filled { false };
    };
    std::array<Entry, maxEntries> entries;
    int numEntries = 0;
    std::array<Corner, numCorners> corners;
    std::atomic<float> mix { 0.0f };
};
}
