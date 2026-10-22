#pragma once
#include "Engine.h"
#include "Params.h"
#include <array>
#include <cmath>
#include <vector>

// The chain knobs: each one moves a whole set of parameters along a tuned path (design:
// potato-synth-CHAIN-KNOBS.md). A knob never writes host parameters; it adds offsets on top of the patch's own
// settings, in each target's natural unit (dB for levels, octaves for frequencies, the slot's own normalised
// scale for engine slots), so a knob makes *this* sound bigger rather than making every patch the same.
//
// Every waypoint value below is a first guess, to be tuned by ear (see the design doc's open questions).
namespace ps::chain
{
enum Knob { mass, scale, pressure, groove, clarity, shine, morph, bloom, numKnobs };
static_assert (numKnobs == params::numChainKnobs);

// A monotone cubic through 5 waypoints at u = 0, .25, .5, .75, 1 (Fritsch-Butland tangents: the harmonic mean
// of neighbouring secants, zero at a local extremum), so a path never overshoots its waypoints.
class Path
{
public:
    static constexpr int numPoints = 5;

    Path() = default;
    Path (std::array<float, numPoints> values) : y (values)
    {
        constexpr float h = 1.0f / (numPoints - 1);
        std::array<float, numPoints - 1> d {};
        for (int k = 0; k < numPoints - 1; ++k)
            d[(size_t) k] = (y[(size_t) k + 1] - y[(size_t) k]) / h;
        m[0] = d[0];
        m[numPoints - 1] = d[numPoints - 2];
        for (int k = 1; k < numPoints - 1; ++k)
        {
            const float a = d[(size_t) k - 1], b = d[(size_t) k];
            m[(size_t) k] = a * b > 0.0f ? 2.0f * a * b / (a + b) : 0.0f;
        }
        // End tangents: keep the end segments monotone too (Fritsch-Carlson limit, alpha^2 + beta^2 <= 9)
        for (int k : { 0, numPoints - 2 })
        {
            const float dk = d[(size_t) k];
            if (std::abs (dk) < 1.0e-12f)
            {
                m[(size_t) k] = m[(size_t) k + 1] = 0.0f;
                continue;
            }
            const float a = m[(size_t) k] / dk, b = m[(size_t) k + 1] / dk, r = a * a + b * b;
            if (r > 9.0f)
            {
                const float t = 3.0f / std::sqrt (r);
                m[(size_t) k] = t * a * dk;
                m[(size_t) k + 1] = t * b * dk;
            }
        }
    }

    float at (float u) const noexcept
    {
        constexpr float h = 1.0f / (numPoints - 1);
        u = std::min (1.0f, std::max (0.0f, u));
        const int k = std::min (numPoints - 2, (int) (u / h));
        const float t = (u - (float) k * h) / h, t2 = t * t, t3 = t2 * t;
        const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f, h10 = t3 - 2.0f * t2 + t;
        const float h01 = -2.0f * t3 + 3.0f * t2, h11 = t3 - t2;
        return h00 * y[(size_t) k] + h10 * h * m[(size_t) k] + h01 * y[(size_t) k + 1] + h11 * h * m[(size_t) k + 1];
    }

private:
    std::array<float, numPoints> y {}, m {};
};

// What a knob moves
enum class Target
{
    layerLevelDb,      // dB on matching layers
    slot,              // a slot role on matching layers, in normalised slot units
    monoBassOctaves,   // mono-bass crossover, octaves relative to the patch's
    lowFocusDb,        // bus peak EQ on the tactile band (about 60 Hz)
    pressure,          // the PRESSURE bus stage's amount, 0..1 (StackBus.h)
    tiltDbPerOct,      // bus spectral tilt around 1 kHz, dB per octave (StackBus.h Tilt)
    exciter,           // oversampled harmonic exciter above 6 kHz, wet mix 0..1 (StackBus.h TruePeakClipper)
};

// Which layers a route applies to: a bit per params::Role
inline constexpr unsigned roleBit (params::Role r) { return 1u << (unsigned) r; }
inline constexpr unsigned anyRole = 0x1fu;

struct Route
{
    Knob knob;
    Target target;
    unsigned roles = anyRole;
    SlotRole slotRole = SlotRole::none;
    Path path;
};

inline const std::vector<Route>& routes()
{
    using R = params::Role;
    const unsigned sub = roleBit (R::sub), body = roleBit (R::body), upper = roleBit (R::mid) | roleBit (R::air);
    static const std::vector<Route> table {
        // MASS: more felt low end at the same meter level
        // (Translate's harmonics spend loudness the trim then takes back from the whole stack, so MASS leans on
        // the low focus, which K-weighting barely counts, and uses Translate lightly)
        { mass, Target::slot, sub, SlotRole::translate, Path ({ 0.0f, 0.15f, 0.3f, 0.4f, 0.5f }) },
        { mass, Target::lowFocusDb, anyRole, SlotRole::none, Path ({ 0.0f, 1.2f, 2.5f, 3.6f, 4.5f }) },
        { mass, Target::slot, body, SlotRole::drive, Path ({ 0.0f, 0.08f, 0.17f, 0.26f, 0.35f }) },
        { mass, Target::slot, body, SlotRole::subOctave, Path ({ 0.0f, 0.1f, 0.25f, 0.38f, 0.5f }) },
        { mass, Target::monoBassOctaves, anyRole, SlotRole::none, Path ({ 0.0f, 0.06f, 0.12f, 0.18f, 0.22f }) },

        // PRESSURE: distributed gain reduction across saturation, OTT, glue and clipper (the stage owns the split)
        { pressure, Target::pressure, anyRole, SlotRole::none, Path ({ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) },

        // SCALE: wider and bigger on the upper roles; the lows are untouched
        { scale, Target::slot, upper, SlotRole::detune, Path ({ 0.0f, 0.08f, 0.16f, 0.24f, 0.3f }) },
        { scale, Target::slot, upper, SlotRole::unisonMix, Path ({ 0.0f, 0.08f, 0.15f, 0.21f, 0.25f }) },
        { scale, Target::slot, upper, SlotRole::stereoSpread, Path ({ 0.0f, 0.1f, 0.18f, 0.25f, 0.3f }) },
        { scale, Target::slot, upper, SlotRole::positionSpread, Path ({ 0.0f, 0.12f, 0.25f, 0.38f, 0.5f }) },

        // SHINE: air and sparkle
        { shine, Target::layerLevelDb, roleBit (R::air), SlotRole::none, Path ({ 0.0f, 1.5f, 3.0f, 4.5f, 6.0f }) },
        { shine, Target::slot, upper, SlotRole::brightness, Path ({ 0.0f, 0.04f, 0.08f, 0.12f, 0.15f }) },
        { shine, Target::tiltDbPerOct, anyRole, SlotRole::none, Path ({ 0.0f, 0.4f, 0.8f, 1.2f, 1.5f }) },
        { shine, Target::exciter, anyRole, SlotRole::none, Path ({ 0.0f, 0.06f, 0.12f, 0.19f, 0.25f }) },

        // MORPH: every layer's spectral identity moves together
        { morph, Target::slot, anyRole, SlotRole::position, Path ({ 0.0f, 0.12f, 0.25f, 0.38f, 0.5f }) },
        { morph, Target::slot, anyRole, SlotRole::warp, Path ({ 0.0f, 0.05f, 0.12f, 0.2f, 0.3f }) },
    };
    return table;
}

// The knobs' combined effect at one moment
struct Offsets
{
    std::array<float, params::numLayers> levelDb {};
    std::array<std::array<float, numSlotRoles>, params::numLayers> slot {};
    float monoBassOctaves = 0.0f;
    float lowFocusDb = 0.0f;
    float pressure = 0.0f;
    float tiltDbPerOct = 0.0f;
    float exciter = 0.0f;
};

// u: each knob's position 0..1 (SUPER already folded in); roles: each layer's role
inline Offsets compute (const std::array<float, numKnobs>& u, const std::array<params::Role, params::numLayers>& roles) noexcept
{
    Offsets o;
    for (const auto& r : routes())
    {
        const float v = r.path.at (u[(size_t) r.knob]);
        if (std::abs (v) < 1.0e-9f)
            continue;
        switch (r.target)
        {
            case Target::monoBassOctaves: o.monoBassOctaves += v; break;
            case Target::lowFocusDb:      o.lowFocusDb += v; break;
            case Target::pressure:        o.pressure += v; break;
            case Target::tiltDbPerOct:    o.tiltDbPerOct += v; break;
            case Target::exciter:         o.exciter += v; break;
            case Target::layerLevelDb:
            case Target::slot:
                for (int l = 0; l < params::numLayers; ++l)
                {
                    if ((r.roles & roleBit (roles[(size_t) l])) == 0)
                        continue;
                    if (r.target == Target::layerLevelDb)
                        o.levelDb[(size_t) l] += v;
                    else
                        o.slot[(size_t) l][(size_t) r.slotRole] += v;
                }
                break;
        }
    }
    return o;
}

// SUPER sweeps every knob along a path; it adds to each knob's own position. Default path: SUPER brings in
// MASS, SCALE and SHINE most, MORPH a little.
inline float superContribution (Knob k, float super) noexcept
{
    static const std::array<float, numKnobs> depth { 0.6f, 0.7f, 0.5f, 0.3f, 0.4f, 0.5f, 0.2f, 0.4f };
    return depth[(size_t) k] * super;
}
}
