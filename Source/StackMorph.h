#pragma once
#include "Params.h"
#include <array>
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
