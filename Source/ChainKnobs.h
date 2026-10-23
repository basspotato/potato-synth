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
    bassHarmonics,     // the lows' 2nd and 3rd harmonics added on the bus, 0..1 (StackBus.h BassHarmonics)
    pressure,          // the PRESSURE bus stage's amount, 0..1 (StackBus.h)
    tiltDbPerOct,      // bus spectral tilt around 1 kHz, dB per octave (StackBus.h Tilt)
    exciter,           // oversampled harmonic exciter above 6 kHz, wet mix 0..1 (StackBus.h TruePeakClipper)
    pumpDb,            // tempo pump depth, dB (StackBus.h TempoPump)
    pumpHardness,      // tempo pump curve, 0 = half cosine .. 1 = exponential
    pumpTau,           // tempo pump exponential time constant, beats
    onsetDuckDb,       // matching layers dip at each new note, dB (Layer.h OnsetDuck)
    onsetDuckMs,       // that dip's window, ms
    tempoLock,         // PRESSURE's dynamics' releases toward 1/16 and 1/8 notes, 0..1
    carveDepthDb,      // band ownership: the most a non-owning layer is dipped, dB (HarmonyEngine.h)
    carveTemperature,  // band ownership softmax temperature: high = soft, low = one sharp owner per band
    deHarsh,           // the 2-5 kHz dynamic bell's amount, 0..1 (StackBus.h DeHarsh)
    driftCents,        // matching layers' pitch drift, RMS in cents (a slow, repeating wobble)
    slotMotion,        // a slot role on matching layers swings by this much (slow LFOs, golden-ratio rates)
    spaceWet,          // the shared reverb's return, linear gain (Space.h)
    spaceRt60,         // its decay, seconds
    spaceDuck,         // how far the return dips under the dry signal, 0..1
    layerDrive,        // matching layers' drive (Drive.h LayerDrive), added to the patch's, 0..1
    layerChorus,       // matching layers' chorus amount (Inserts.h), added to the patch's, 0..1
    loudnessLiftDb,    // the loudness the Harmony Engine holds, raised by this much (kept inside +-0.5 LU)
    beatCeiling,       // invariant 5: the knobs' added detune keeps beating under this share of a critical band
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
        // Listening (2026-10-22): the first tuning was too subtle. The body's drive now goes through the layer
        // drive, so it works on every engine (the init body, a Wavetable, has no drive slot).
        { mass, Target::slot, sub, SlotRole::translate, Path ({ 0.0f, 0.2f, 0.4f, 0.6f, 0.75f }) },
        { mass, Target::lowFocusDb, anyRole, SlotRole::none, Path ({ 0.0f, 1.8f, 3.5f, 5.0f, 6.0f }) },
        { mass, Target::layerDrive, body, SlotRole::none, Path ({ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f }) },
        { mass, Target::monoBassOctaves, anyRole, SlotRole::none, Path ({ 0.0f, 0.06f, 0.12f, 0.18f, 0.22f }) },
        // The limiter on the bigger sub peaks costs loudness the open-loop trim can't see. (The body's drive no
        // longer does: the layer measures what the knobs' drive adds, and the trim takes it back.)
        // (The body's own drive and sub-octave slots are left alone: on the init Dirt body they lost 0.8 LU
        // between them, the sub octave mostly under the Body role's 120 Hz band limit, where it can't be heard.)
        // (Since the presets moved 10 dB under the ceiling, 2026-10-23, the limiter no longer works on them and the
        // lift only made MASS 0.6 LU louder; the sweeps now hold it inside +-1 LU without one.)
        // Listening (build #25): still too subtle. The balance of the stack leans onto the low roles (the trim holds
        // the total), so the weight shift is heard on any speaker, not only on a sub.
        { mass, Target::layerLevelDb, sub, SlotRole::none, Path ({ 0.0f, 0.8f, 1.6f, 2.4f, 3.0f }) },
        { mass, Target::layerLevelDb, body, SlotRole::none, Path ({ 0.0f, 0.6f, 1.2f, 1.8f, 2.2f }) },
        // Listening (build #27): "MASS still sounds like it does nothing", most likely on speakers that don't
        // reach 60 Hz, where the low focus and the sub can't be heard. The lows' own harmonics carry the weight
        // into 120-400 Hz, which every speaker plays.
        { mass, Target::bassHarmonics, anyRole, SlotRole::none, Path ({ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) },

        // PRESSURE: distributed gain reduction across saturation, OTT, glue and clipper (the stage owns the split)
        { pressure, Target::pressure, anyRole, SlotRole::none, Path ({ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) },
        // Listening: at the same meter loudness, the denser sound read as quieter (the trim had also settled
        // 0.35 LU low). It now ends 0.3 LU up: louder by a hair, as a mastered sound is, and inside the band.
        { pressure, Target::loudnessLiftDb, anyRole, SlotRole::none, Path ({ 0.0f, 0.15f, 0.3f, 0.48f, 0.65f }) },

        // SCALE: wider and bigger on the upper roles; the lows are untouched
        // Listening: "detunes everything". Detune pulled back by a third (it also cost 0.4 LU on the init stack,
        // where slower-beating unisons read quieter); the size comes from spread and side voices.
        { scale, Target::slot, upper, SlotRole::detune, Path ({ 0.0f, 0.05f, 0.1f, 0.15f, 0.2f }) },
        { scale, Target::loudnessLiftDb, anyRole, SlotRole::none, Path ({ 0.0f, 0.04f, 0.08f, 0.12f, 0.15f }) },
        { scale, Target::slot, upper, SlotRole::unisonMix, Path ({ 0.0f, 0.08f, 0.15f, 0.21f, 0.25f }) },
        { scale, Target::slot, upper, SlotRole::stereoSpread, Path ({ 0.0f, 0.1f, 0.18f, 0.25f, 0.3f }) },
        { scale, Target::slot, upper, SlotRole::positionSpread, Path ({ 0.0f, 0.12f, 0.25f, 0.38f, 0.5f }) },

        // SHINE: air and sparkle
        { shine, Target::layerLevelDb, roleBit (R::air), SlotRole::none, Path ({ 0.0f, 0.75f, 1.5f, 2.25f, 3.0f }) },
        { shine, Target::slot, upper, SlotRole::brightness, Path ({ 0.0f, 0.015f, 0.03f, 0.045f, 0.06f }) },
        { shine, Target::tiltDbPerOct, anyRole, SlotRole::none, Path ({ 0.0f, 0.2f, 0.4f, 0.6f, 0.75f }) },
        { shine, Target::exciter, anyRole, SlotRole::none, Path ({ 0.0f, 0.04f, 0.08f, 0.12f, 0.15f }) },

        // GROOVE: the pump and the bounce. The transient roles (Sub, Mid) lead; Body and Air duck under their
        // onsets inside the forward-masking window.
        { groove, Target::pumpDb, anyRole, SlotRole::none, Path ({ 0.0f, 2.5f, 5.0f, 7.0f, 9.0f }) },
        { groove, Target::pumpHardness, anyRole, SlotRole::none, Path ({ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) },
        { groove, Target::pumpTau, anyRole, SlotRole::none, Path ({ 0.12f, 0.18f, 0.24f, 0.3f, 0.35f }) },
        { groove, Target::onsetDuckDb, body | roleBit (R::air), SlotRole::none, Path ({ 0.0f, 2.0f, 4.0f, 6.0f, 8.0f }) },
        { groove, Target::onsetDuckMs, anyRole, SlotRole::none, Path ({ 10.0f, 20.0f, 30.0f, 40.0f, 50.0f }) },
        { groove, Target::tempoLock, anyRole, SlotRole::none, Path ({ 0.0f, 0.3f, 0.6f, 0.85f, 1.0f }) },

        // CLARITY: harmony, not mud. One owner per band, a calmer presence band, and slower beating (detune
        // pulled back on every role, so pairs of partials beat further under the critical-band roughness peak).
        // Listening: "doing something, not sure what". Deeper carve and slower beating. (A low-mid mud cut was
        // tried: it raises the presence band's share, the opposite of what CLARITY promises.)
        { clarity, Target::carveDepthDb, anyRole, SlotRole::none, Path ({ 0.0f, 2.5f, 5.0f, 7.0f, 9.0f }) },
        { clarity, Target::carveTemperature, anyRole, SlotRole::none, Path ({ 1.0f, 0.75f, 0.55f, 0.4f, 0.3f }) },
        { clarity, Target::deHarsh, anyRole, SlotRole::none, Path ({ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) },
        // Less detune beats slower but reads quieter (0.67 LU at -0.15 on the init stack): a lighter pull, and a lift
        { clarity, Target::slot, anyRole, SlotRole::detune, Path ({ 0.0f, -0.02f, -0.04f, -0.06f, -0.08f }) },
        { clarity, Target::loudnessLiftDb, anyRole, SlotRole::none, Path ({ 0.0f, 0.06f, 0.12f, 0.19f, 0.25f }) },
        { clarity, Target::beatCeiling, anyRole, SlotRole::none, Path ({ 0.25f, 0.225f, 0.2f, 0.175f, 0.15f }) },

        // BLOOM: life and motion. The whole stack drifts together: a drift between layers would slowly phase
        // their sum in every band they share (the sub/body crossover first), costing punch and up to 3 dB at
        // a crossover. The layers' own motion comes from the slot swings, and the stack opens into a space
        // that ducks under the dry signal.
        // Listening: hard to hear. Twice the motion, a much bigger space, and chorus on the upper roles.
        { bloom, Target::driftCents, anyRole, SlotRole::none, Path ({ 0.0f, 2.0f, 4.0f, 6.0f, 8.0f }) },
        { bloom, Target::slotMotion, anyRole, SlotRole::position, Path ({ 0.0f, 0.06f, 0.12f, 0.18f, 0.25f }) },
        { bloom, Target::slotMotion, upper, SlotRole::brightness, Path ({ 0.0f, 0.03f, 0.06f, 0.09f, 0.12f }) },
        { bloom, Target::slotMotion, upper, SlotRole::stereoSpread, Path ({ 0.0f, 0.06f, 0.12f, 0.18f, 0.25f }) },
        { bloom, Target::layerChorus, upper, SlotRole::none, Path ({ 0.0f, 0.2f, 0.4f, 0.55f, 0.7f }) },
        { bloom, Target::spaceWet, anyRole, SlotRole::none, Path ({ 0.0f, 0.1f, 0.2f, 0.32f, 0.45f }) },
        { bloom, Target::spaceRt60, anyRole, SlotRole::none, Path ({ 0.8f, 1.8f, 3.0f, 4.5f, 6.0f }) },
        { bloom, Target::spaceDuck, anyRole, SlotRole::none, Path ({ 0.0f, 0.25f, 0.5f, 0.7f, 0.85f }) },

        // MORPH: every layer's spectral identity moves together
        // Listening: hard to hear, since only Wavetable layers followed. Now the full table sweep, more warp,
        // JP cross-mod on every non-sub role, and the Spectral engine's formant (0 -> +5 st) with its A/B morph.
        { morph, Target::slot, anyRole, SlotRole::position, Path ({ 0.0f, 0.22f, 0.45f, 0.65f, 0.85f }) },
        { morph, Target::slot, anyRole, SlotRole::warp, Path ({ 0.0f, 0.1f, 0.2f, 0.3f, 0.4f }) },
        { morph, Target::slot, body | upper, SlotRole::crossMod, Path ({ 0.0f, 0.06f, 0.13f, 0.2f, 0.26f }) },
        { morph, Target::slot, anyRole, SlotRole::formant, Path ({ 0.0f, 0.026f, 0.052f, 0.078f, 0.104f }) },
        // Frames are loudness-matched full band, but the roles' band limits pass more of the brighter ones
        { morph, Target::loudnessLiftDb, anyRole, SlotRole::none, Path ({ 0.0f, -0.08f, -0.16f, -0.24f, -0.3f }) },
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
    float bassHarmonics = 0.0f;
    float pressure = 0.0f;
    float tiltDbPerOct = 0.0f;
    float exciter = 0.0f;
    float pumpDb = 0.0f, pumpHardness = 0.0f, pumpTau = 0.0f;
    std::array<float, params::numLayers> onsetDuckDb {};
    float onsetDuckMs = 0.0f;
    float tempoLock = 0.0f;
    float carveDepthDb = 0.0f, carveTemperature = 1.0f, deHarsh = 0.0f;
    std::array<float, params::numLayers> driftCents {};
    std::array<std::array<float, numSlotRoles>, params::numLayers> slotMotion {};
    float spaceWet = 0.0f, spaceRt60 = 0.8f, spaceDuck = 0.0f;
    std::array<float, params::numLayers> layerDrive {}, layerChorus {};
    float loudnessLiftDb = 0.0f;
    float beatCeiling = 0.25f;
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
            case Target::bassHarmonics: o.bassHarmonics += v; break;
            case Target::lowFocusDb:      o.lowFocusDb += v; break;
            case Target::pressure:        o.pressure += v; break;
            case Target::tiltDbPerOct:    o.tiltDbPerOct += v; break;
            case Target::exciter:         o.exciter += v; break;
            case Target::pumpDb:          o.pumpDb += v; break;
            case Target::pumpHardness:    o.pumpHardness += v; break;
            case Target::pumpTau:         o.pumpTau += v; break;
            case Target::onsetDuckMs:     o.onsetDuckMs += v; break;
            case Target::tempoLock:       o.tempoLock += v; break;
            case Target::carveDepthDb:    o.carveDepthDb += v; break;
            case Target::carveTemperature: o.carveTemperature = v; break;
            case Target::deHarsh:         o.deHarsh += v; break;
            case Target::spaceWet:        o.spaceWet += v; break;
            case Target::spaceRt60:       o.spaceRt60 = v; break;
            case Target::spaceDuck:       o.spaceDuck += v; break;
            case Target::loudnessLiftDb:  o.loudnessLiftDb += v; break;
            case Target::beatCeiling:     o.beatCeiling = v; break;
            case Target::driftCents:
            case Target::layerDrive:
            case Target::layerChorus:
            case Target::slotMotion:
            case Target::onsetDuckDb:
            case Target::layerLevelDb:
            case Target::slot:
                for (int l = 0; l < params::numLayers; ++l)
                {
                    if ((r.roles & roleBit (roles[(size_t) l])) == 0)
                        continue;
                    if (r.target == Target::layerLevelDb)
                        o.levelDb[(size_t) l] += v;
                    else if (r.target == Target::onsetDuckDb)
                        o.onsetDuckDb[(size_t) l] += v;
                    else if (r.target == Target::driftCents)
                        o.driftCents[(size_t) l] += v;
                    else if (r.target == Target::layerDrive)
                        o.layerDrive[(size_t) l] += v;
                    else if (r.target == Target::layerChorus)
                        o.layerChorus[(size_t) l] += v;
                    else if (r.target == Target::slotMotion)
                        o.slotMotion[(size_t) l][(size_t) r.slotRole] += v;
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
