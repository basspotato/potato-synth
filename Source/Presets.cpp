#include "Presets.h"
#include "EngineRegistry.h"
#include "Inserts.h"

// The factory bank. Each category has one recipe (a function that builds a stack from a few character
// values) and a table of presets, one row each. Every preset is built on the stack: Sub, Body, Mid and Air
// layers on layers 1-4. Levels are trimmed per preset by the Potato Ready calibration (PresetTrims.inc).
namespace ps::presets
{
namespace
{
    namespace p = params;
    using Role = params::Role;
    using IT = inserts::Type;
    constexpr int sub = 0, body = 1, mid = 2, air = 3;

    // ---- Shared helpers ----
    float log1000 (float hz) { return std::log (hz / 20.0f) / std::log (1000.0f); }   // 20 Hz..20 kHz knob
    float bipolar (float x) { return 0.5f + 0.5f * x; }

    void amp (Preset& x, int l, float attackMs, float decayMs, float sustainPercent, float releaseMs)
    {
        x.set (p::layerAttack (l), attackMs).set (p::layerDecay (l), decayMs).set (p::layerSustain (l), sustainPercent).set (p::layerRelease (l), releaseMs);
    }
    void ampAll (Preset& x, float attackMs, float decayMs, float sustainPercent, float releaseMs)
    {
        for (int l = 0; l < 4; ++l)
            if (x.engines[(size_t) l].isNotEmpty())
                amp (x, l, attackMs, decayMs, sustainPercent, releaseMs);
    }
    void level (Preset& x, int l, float db) { x.set (p::layerLevel (l), db); }
    void mono (Preset& x, bool legato, float glideMs)
    {
        for (int l = 0; l < 4; ++l)
            x.set (p::layerVoiceMode (l), (float) (legato ? p::VoiceMode::legato : p::VoiceMode::mono)).set (p::layerGlide (l), glideMs);
    }
    void insert (Preset& x, int l, int slot, IT type, float amountPercent, std::array<float, 3> controls = { -1.0f, -1.0f, -1.0f })
    {
        x.set (p::layerInsertType (l, slot), (float) type).set (p::layerInsertAmount (l, slot), amountPercent);
        for (int c = 0; c < 3; ++c)
            x.set (p::layerInsertControl (l, slot, c), controls[(size_t) c] >= 0.0f ? controls[(size_t) c]
                                                                                   : inserts::controls[(size_t) type][(size_t) c].defaultValue);
    }
    void space (Preset& x, float reverbPercent, float sizeSeconds, float delayPercent = 0.0f, int delayDivision = 6, float feedbackPercent = 35.0f)
    {
        x.set (p::reverbAmount, reverbPercent).set (p::reverbSize, sizeSeconds);
        if (delayPercent > 0.0f)
            x.set (p::delayAmount, delayPercent).set (p::delayTime, (float) delayDivision).set (p::delayFeedback, feedbackPercent);
    }

    // ---- Engines ----
    void subLayer (Preset& x, float shape, float drive = 0.0f, float punchSemis = 0.0f)
    {
        x.engine (sub, "sub", Role::sub);
        x.slot (sub, SubEngine::shape, shape).slot (sub, SubEngine::drive, drive);
        if (punchSemis > 0.0f)
            x.slot (sub, SubEngine::punch, punchSemis / 36.0f).slot (sub, SubEngine::punchDecay, SubEngine::punchValue (40.0f));
    }

    struct JpVoice
    {
        int wave = 0;            // Saw, Pulse, Triangle
        int unison = 1;          // Off, JP 7, Modern 8, Modern 12, Modern 16
        float detune = 0.6f, mix = 0.65f, spread = 0.7f;
        float cutoffHz = 20000.0f, resonance = 0.0f;
        float envSemis = 0.0f, envDecayMs = 400.0f, envSustain = 0.0f;
        float drive = 0.0f;
    };
    void jpLayer (Preset& x, int l, Role role, const JpVoice& v)
    {
        using J = JpEngine;
        x.engine (l, "jp", role);
        x.slot (l, J::wave1, J::choiceValue (v.wave, J::numWaves1)).slot (l, J::unison, J::choiceValue (v.unison, J::numUnison))
         .slot (l, J::detune, v.detune).slot (l, J::mix, v.mix).slot (l, J::spread, v.spread)
         .slot (l, J::cutoff, log1000 (v.cutoffHz)).slot (l, J::resonance, v.resonance)
         .slot (l, J::envAmount, bipolar (v.envSemis / 72.0f)).slot (l, J::envDecay, J::timeValue (v.envDecayMs))
         .slot (l, J::envSustain, v.envSustain).slot (l, J::drive, v.drive);
    }

    struct DirtVoice
    {
        float shape = 0.5f, sub1 = 0.35f, sub2 = 0.0f, noise = 0.0f, drive = 0.2f;
        float cutoffHz = 1400.0f, resonance = 0.25f, envSemis = 24.0f, envDecayMs = 450.0f, envSustain = 0.35f;
    };
    void dirtLayer (Preset& x, int l, Role role, const DirtVoice& v)
    {
        using D = DirtEngine;
        x.engine (l, "dirt", role);
        x.slot (l, D::shape, v.shape).slot (l, D::sub1, v.sub1).slot (l, D::sub2, v.sub2).slot (l, D::noise, v.noise)
         .slot (l, D::drive, v.drive).slot (l, D::cutoff, D::cutoffValue (v.cutoffHz)).slot (l, D::resonance, v.resonance)
         .slot (l, D::envAmount, bipolar (v.envSemis / 60.0f)).slot (l, D::envDecay, D::timeValue (v.envDecayMs))
         .slot (l, D::envSustain, v.envSustain);
    }

    struct WtVoice
    {
        int table = 0;           // Basic, PWM, Vowels, Reso Sweep, Drawbars, Digital
        float position = 0.0f, posEnv = 0.0f, posEnvMs = 500.0f, warp = 0.0f;
        int unison = 2;
        float detune = 0.4f, mix = 0.7f, spread = 0.7f;
    };
    void wtLayer (Preset& x, int l, Role role, const WtVoice& v)
    {
        using W = WavetableEngine;
        x.engine (l, "wavetable", role);
        x.slot (l, W::table, W::choiceValue (v.table, 6)).slot (l, W::position, v.position).slot (l, W::posEnv, bipolar (v.posEnv))
         .slot (l, W::posEnvTime, W::envValue (v.posEnvMs)).slot (l, W::warp, bipolar (v.warp))
         .slot (l, W::unison, W::choiceValue (v.unison, 5)).slot (l, W::detune, v.detune).slot (l, W::mix, v.mix).slot (l, W::spread, v.spread);
    }

    struct SpecVoice
    {
        int sourceA = 0, sourceB = 6;   // Saw, Square, Triangle, Pulse 25, Sine, Organ, Vowel A, Vowel O, Glass
        float morph = 0.0f, tiltDb = 0.0f, formantSemis = 0.0f, smear = 0.0f, shepard = 0.0f, shepardRate = 0.0f;
        int unison = 1;
        float detune = 0.35f, spread = 0.7f;
    };
    void specLayer (Preset& x, int l, Role role, const SpecVoice& v)
    {
        using S = SpectralEngine;
        x.engine (l, "spectral", role);
        x.slot (l, S::sourceA, S::choiceValue (v.sourceA, S::numSources)).slot (l, S::sourceB, S::choiceValue (v.sourceB, S::numSources))
         .slot (l, S::morph, v.morph).slot (l, S::tilt, bipolar (v.tiltDb / 6.0f)).slot (l, S::formant, bipolar (v.formantSemis / 24.0f))
         .slot (l, S::smear, v.smear).slot (l, S::shepard, v.shepard).slot (l, S::shepardRate, v.shepardRate)
         .slot (l, S::unison, S::choiceValue (v.unison, S::numUnison)).slot (l, S::detune, v.detune).slot (l, S::spread, v.spread);
    }

    struct GenomeVoice
    {
        float architecture = 0.0f;      // Jupiter 0, FM 0.25, Phase Dist 0.5, Additive 0.75, Body 1
        float shape = 0.0f, detune = 0.3f, fmIndex = 0.35f, fmRatio = 0.25f, fmFeedback = 0.3f, fmEnv = 0.25f, fmDecay = 0.5f;
        float tilt = 0.5f, bodyDecay = 0.5f, bodyTone = 0.0f, strike = 0.5f, toneHz = 20000.0f;
    };
    void genomeLayer (Preset& x, int l, Role role, const GenomeVoice& v)
    {
        using G = GenomeEngine;
        x.engine (l, "genome", role);
        x.slot (l, G::architecture, v.architecture).slot (l, G::shape, v.shape).slot (l, G::detune, v.detune)
         .slot (l, G::fmIndex, v.fmIndex).slot (l, G::fmRatio, v.fmRatio).slot (l, G::fmFeedback, v.fmFeedback)
         .slot (l, G::fmEnv, v.fmEnv).slot (l, G::fmDecay, v.fmDecay).slot (l, G::tilt, v.tilt)
         .slot (l, G::bodyDecay, v.bodyDecay).slot (l, G::bodyTone, v.bodyTone).slot (l, G::strike, v.strike)
         .slot (l, G::tone, v.toneHz >= 19999.0f ? 1.0f : log1000 (v.toneHz));
    }

    // FM ratio knob value for a ratio (0.5..8)
    float fmRatio (float r) { return std::log (r / 0.5f) / std::log (16.0f); }
    float fmIndex (float radians) { return std::sqrt (radians / 6.0f); }

    Preset make (const char* name, Category c)
    {
        Preset x;
        x.name = name;
        x.category = c;
        return x;
    }

    // ==== Supersaw / Lead ====================================================================================
    struct LeadRow
    {
        const char* name;
        int unison;          // the Mid supersaw: JP 7 .. Modern 16
        float detune, cutoffHz, resonance;
        int bodyKind;        // 0 Dirt saw, 1 JP pulse, 2 Genome FM, 3 Genome phase dist
        int airKind;         // 0 Spectral glass, 1 Spectral saw shimmer, 2 Wavetable digital, 3 Spectral vowel
        float glideMs;       // > 0: legato lead
        float reverb, delay;
        IT midInsert;
        float releaseMs;
    };

    Preset lead (const LeadRow& r)
    {
        auto x = make (r.name, Category::lead);
        subLayer (x, 0.0f);
        level (x, sub, -7.0f);
        switch (r.bodyKind)
        {
            case 0:  dirtLayer (x, body, Role::body, { 0.5f, 0.2f, 0.0f, 0.0f, 0.3f, r.cutoffHz * 0.5f, 0.2f, 18.0f, 600.0f, 0.6f }); break;
            case 1:  jpLayer (x, body, Role::body, { 1, 0, 0.0f, 0.65f, 0.5f, r.cutoffHz * 0.6f, 0.15f, 12.0f, 500.0f, 0.5f, 0.2f }); break;
            case 2:  genomeLayer (x, body, Role::body, { 0.25f, 0.0f, 0.2f, fmIndex (1.6f), fmRatio (2.0f), 0.25f, 0.3f, 0.45f }); break;
            default: genomeLayer (x, body, Role::body, { 0.5f, 0.0f, 0.25f }); break;
        }
        level (x, body, -3.0f);
        jpLayer (x, mid, Role::mid, { 0, r.unison, r.detune, 0.7f, 0.85f, r.cutoffHz, r.resonance, 18.0f, 350.0f, 0.7f });
        switch (r.airKind)
        {
            case 0:  specLayer (x, air, Role::air, { 8, 0, 0.0f, 1.5f, 0.0f, 0.1f }); break;
            case 1:  specLayer (x, air, Role::air, { 0, 8, 0.4f, 2.0f, 0.0f, 0.2f, 0.0f, 0.0f, 2, 0.5f, 1.0f }); break;
            case 2:  wtLayer (x, air, Role::air, { 5, 0.6f, 0.0f, 500.0f, 0.2f, 2, 0.5f, 0.6f, 1.0f }); break;
            default: specLayer (x, air, Role::air, { 6, 7, 0.5f, 1.0f, 5.0f, 0.15f }); break;
        }
        level (x, air, -8.0f);
        ampAll (x, 3.0f, 400.0f, 100.0f, r.releaseMs);
        if (r.glideMs > 0.0f)
            mono (x, true, r.glideMs);
        if (r.midInsert != IT::off)
            insert (x, mid, 0, r.midInsert, 35.0f);
        space (x, r.reverb, 2.4f, r.delay, 6, 30.0f);
        return x;
    }

    const LeadRow leadRows[] = {
        { "Mash Saw",            1, 0.60f, 12000.0f, 0.10f, 0, 0, 0.0f,  18.0f, 0.0f,  IT::off,       350.0f },
        { "Festival Anthem",     4, 0.70f, 14000.0f, 0.05f, 0, 1, 0.0f,  25.0f, 15.0f, IT::off,       500.0f },
        { "Hoover Royale",       1, 0.85f,  6000.0f, 0.20f, 1, 3, 0.0f,  15.0f, 0.0f,  IT::phaser,    300.0f },
        { "Supernova Stack",     3, 0.65f, 16000.0f, 0.00f, 0, 1, 0.0f,  30.0f, 20.0f, IT::off,       600.0f },
        { "Trance Gate Lead",    2, 0.55f,  9000.0f, 0.15f, 1, 0, 0.0f,  20.0f, 25.0f, IT::off,       250.0f },
        { "Glass Cannon",        2, 0.45f, 18000.0f, 0.05f, 2, 0, 0.0f,  22.0f, 10.0f, IT::off,       400.0f },
        { "Neon Legato",         1, 0.50f,  8000.0f, 0.25f, 0, 2, 90.0f, 18.0f, 20.0f, IT::off,       300.0f },
        { "Screamer",            2, 0.35f,  5000.0f, 0.45f, 0, 3, 60.0f, 12.0f, 10.0f, IT::drive,     250.0f },
        { "Hard Dance Hero",     4, 0.80f, 11000.0f, 0.10f, 0, 1, 0.0f,  15.0f, 0.0f,  IT::drive,     300.0f },
        { "Velvet Wall",         4, 0.50f,  7000.0f, 0.05f, 3, 0, 0.0f,  35.0f, 0.0f,  IT::off,       700.0f },
        { "Laser Choir",         2, 0.40f, 12000.0f, 0.20f, 2, 3, 0.0f,  30.0f, 15.0f, IT::off,       500.0f },
        { "Mainroom Brass",      1, 0.30f,  4500.0f, 0.15f, 1, 2, 0.0f,  15.0f, 0.0f,  IT::off,       250.0f },
        { "Sky Splitter",        3, 0.75f, 17000.0f, 0.05f, 0, 1, 0.0f,  28.0f, 22.0f, IT::off,       550.0f },
        { "Phase Blade",         2, 0.55f, 10000.0f, 0.20f, 3, 2, 0.0f,  18.0f, 12.0f, IT::flanger,   350.0f },
        { "Big Room Saw",        4, 0.65f, 13000.0f, 0.10f, 0, 0, 0.0f,  20.0f, 0.0f,  IT::off,       400.0f },
        { "Chrome Lead",         1, 0.40f,  7500.0f, 0.30f, 1, 2, 40.0f, 15.0f, 20.0f, IT::off,       300.0f },
        { "Euphoria",            3, 0.70f, 15000.0f, 0.05f, 2, 1, 0.0f,  35.0f, 25.0f, IT::off,       650.0f },
        { "Rave Siren",          1, 0.90f,  6500.0f, 0.35f, 1, 3, 120.0f, 12.0f, 10.0f, IT::phaser,   300.0f },
        { "Ultraviolet",         2, 0.60f, 12500.0f, 0.15f, 3, 0, 0.0f,  25.0f, 15.0f, IT::off,       450.0f },
        { "Molten Lead",         1, 0.45f,  5500.0f, 0.25f, 0, 2, 70.0f, 10.0f, 15.0f, IT::drive,     250.0f },
        { "Silk Supersaw",       4, 0.40f, 11000.0f, 0.00f, 3, 0, 0.0f,  30.0f, 10.0f, IT::off,       600.0f },
        { "Stadium Lights",      4, 0.75f, 16000.0f, 0.05f, 0, 1, 0.0f,  32.0f, 20.0f, IT::off,       700.0f },
        { "Retro Future",        1, 0.55f,  6000.0f, 0.20f, 1, 3, 0.0f,  20.0f, 18.0f, IT::off,       350.0f },
        { "Ion Drive",           2, 0.50f,  9500.0f, 0.30f, 2, 2, 0.0f,  15.0f, 12.0f, IT::comb,      300.0f },
        { "Halo Lead",           3, 0.55f, 14000.0f, 0.10f, 2, 0, 0.0f,  38.0f, 25.0f, IT::off,       700.0f },
        { "Distorted Anthem",    4, 0.70f,  9000.0f, 0.15f, 0, 1, 0.0f,  18.0f, 10.0f, IT::drive,     350.0f },
        { "Pure Saw Solo",       1, 0.30f, 10000.0f, 0.10f, 0, 0, 50.0f, 15.0f, 20.0f, IT::off,       300.0f },
        { "Vocal Synth Lead",    2, 0.45f,  8000.0f, 0.20f, 3, 3, 0.0f,  25.0f, 15.0f, IT::formant,   400.0f },
        { "Hyperspace",          3, 0.80f, 18000.0f, 0.05f, 2, 1, 0.0f,  40.0f, 30.0f, IT::off,       800.0f },
        { "Tight Saw Stab Lead", 2, 0.50f,  7000.0f, 0.20f, 1, 2, 0.0f,  12.0f, 0.0f,  IT::off,       150.0f },
        { "Crystal Edge",        2, 0.35f, 19000.0f, 0.05f, 2, 0, 0.0f,  22.0f, 15.0f, IT::off,       450.0f },
        { "Overdrive Hero",      1, 0.60f,  6000.0f, 0.30f, 0, 2, 80.0f, 12.0f, 18.0f, IT::drive,     300.0f },
        { "Sunrise Saws",        4, 0.55f, 12000.0f, 0.05f, 3, 1, 0.0f,  35.0f, 20.0f, IT::off,       650.0f },
        { "Electric Dream",      2, 0.50f, 10500.0f, 0.15f, 2, 3, 0.0f,  28.0f, 22.0f, IT::phaser,    500.0f },
        { "Mono Ripper",         1, 0.40f,  4500.0f, 0.40f, 0, 2, 35.0f, 10.0f, 10.0f, IT::drive,     200.0f },
        { "Wide Lead Classic",   1, 0.65f, 11000.0f, 0.10f, 1, 0, 0.0f,  20.0f, 15.0f, IT::off,       400.0f },
        { "Ghost Lead",          2, 0.45f,  6500.0f, 0.10f, 3, 3, 60.0f, 40.0f, 30.0f, IT::off,       700.0f },
        { "Titanium",            4, 0.60f, 15000.0f, 0.10f, 2, 2, 0.0f,  18.0f, 10.0f, IT::off,       400.0f },
        { "Shimmer Saw",         3, 0.50f, 13000.0f, 0.05f, 0, 1, 0.0f,  30.0f, 15.0f, IT::freqShift, 550.0f },
        { "Final Drop Lead",     4, 0.85f, 14000.0f, 0.10f, 0, 1, 0.0f,  22.0f, 12.0f, IT::off,       450.0f },
    };

    // ==== Bass / Reese / Growl ===============================================================================
    struct BassRow
    {
        const char* name;
        float subShape, subDrive, punchSemis;
        int bodyKind;            // 0 Dirt, 1 JP saw, 2 Genome FM
        float bodyCutoffHz, bodyEnvSemis, bodyDecayMs, bodyResonance;
        int midKind;             // 0 Wavetable growl, 1 JP reese, 2 Spectral, 3 Dirt square
        int table;               // wavetable
        float position, posEnv, detune;
        IT midInsert;
        float glideMs, releaseMs;
    };

    Preset bass (const BassRow& r)
    {
        auto x = make (r.name, Category::bass);
        subLayer (x, r.subShape, r.subDrive, r.punchSemis);
        switch (r.bodyKind)
        {
            case 0:  dirtLayer (x, body, Role::body, { 0.5f, 0.4f, 0.0f, 0.0f, 0.4f, r.bodyCutoffHz, r.bodyResonance, r.bodyEnvSemis, r.bodyDecayMs, 0.3f }); break;
            case 1:  jpLayer (x, body, Role::body, { 0, 0, 0.0f, 0.65f, 0.0f, r.bodyCutoffHz, r.bodyResonance, r.bodyEnvSemis, r.bodyDecayMs, 0.3f, 0.3f }); break;
            default: genomeLayer (x, body, Role::body, { 0.25f, 0.0f, 0.1f, fmIndex (2.0f), fmRatio (1.0f), 0.4f, 0.5f, 0.35f, 0.5f, 0.5f, 0.0f, 0.5f, r.bodyCutoffHz }); break;
        }
        level (x, body, -2.0f);
        switch (r.midKind)
        {
            case 0:  wtLayer (x, mid, Role::mid, { r.table, r.position, r.posEnv, 350.0f, 0.3f, 2, r.detune, 0.7f, 0.6f }); break;
            case 1:  jpLayer (x, mid, Role::mid, { 0, 1, r.detune, 0.75f, 0.6f, r.bodyCutoffHz * 2.0f, r.bodyResonance, r.bodyEnvSemis * 0.5f, r.bodyDecayMs, 0.5f, 0.2f }); break;
            case 2:  specLayer (x, mid, Role::mid, { 0, 6, r.position, -1.0f, 0.0f, 0.1f, 0.0f, 0.0f, 1, r.detune, 0.6f }); break;
            default: dirtLayer (x, mid, Role::mid, { 1.0f, 0.0f, 0.0f, 0.05f, 0.6f, r.bodyCutoffHz * 2.0f, 0.35f, r.bodyEnvSemis, r.bodyDecayMs, 0.4f }); break;
        }
        level (x, mid, -4.0f);
        specLayer (x, air, Role::air, { 0, 8, 0.3f, 0.0f, 0.0f, 0.3f });
        level (x, air, -16.0f);
        ampAll (x, 1.0f, 400.0f, 100.0f, r.releaseMs);
        mono (x, r.glideMs > 0.0f, r.glideMs);
        if (r.midInsert != IT::off)
            insert (x, mid, 0, r.midInsert, 45.0f);
        space (x, 6.0f, 1.2f);
        return x;
    }

    const BassRow bassRows[] = {
        { "Mash Bass",          0.00f, 0.10f, 0.0f,  0, 900.0f,  30.0f, 300.0f, 0.30f, 0, 5, 0.3f, 0.4f, 0.30f, IT::off,       0.0f,  120.0f },
        { "Reese Machine",      0.00f, 0.00f, 0.0f,  1, 700.0f,  12.0f, 600.0f, 0.10f, 1, 0, 0.0f, 0.0f, 0.75f, IT::phaser,    0.0f,  150.0f },
        { "Growl Factory",      0.10f, 0.20f, 0.0f,  0, 600.0f,  36.0f, 250.0f, 0.40f, 0, 2, 0.2f, 0.7f, 0.25f, IT::formant,   0.0f,  100.0f },
        { "Neuro Rumble",       0.00f, 0.25f, 0.0f,  0, 500.0f,  24.0f, 200.0f, 0.50f, 0, 3, 0.4f, 0.5f, 0.35f, IT::comb,      0.0f,  100.0f },
        { "808 Boom",           0.00f, 0.30f, 12.0f, 2, 2000.0f, 0.0f,  800.0f, 0.00f, 3, 0, 0.0f, 0.0f, 0.10f, IT::off,       60.0f, 400.0f },
        { "Sub Pressure",       0.50f, 0.50f, 0.0f,  0, 900.0f,  12.0f, 400.0f, 0.20f, 2, 0, 0.1f, 0.0f, 0.20f, IT::drive,       0.0f,  150.0f },
        { "Dark Reese",         0.00f, 0.10f, 0.0f,  1, 450.0f,  6.0f,  800.0f, 0.15f, 1, 0, 0.0f, 0.0f, 0.85f, IT::off,       0.0f,  200.0f },
        { "Wobble Core",        0.05f, 0.20f, 0.0f,  0, 700.0f,  30.0f, 450.0f, 0.45f, 0, 2, 0.5f, 0.6f, 0.30f, IT::formant,   0.0f,  120.0f },
        { "Big Room Bass",      0.20f, 0.25f, 6.0f,  1, 1200.0f, 24.0f, 250.0f, 0.20f, 3, 0, 0.0f, 0.0f, 0.20f, IT::drive,     0.0f,  120.0f },
        { "Future Bass Saw",    0.00f, 0.10f, 0.0f,  1, 3000.0f, 18.0f, 350.0f, 0.10f, 1, 0, 0.0f, 0.0f, 0.55f, IT::off,       0.0f,  200.0f },
        { "Metal Growl",        0.10f, 0.30f, 0.0f,  0, 800.0f,  40.0f, 300.0f, 0.55f, 0, 5, 0.6f, 0.4f, 0.30f, IT::drive,     0.0f,  100.0f },
        { "Liquid Sub",         0.00f, 0.00f, 0.0f,  2, 800.0f,  0.0f,  600.0f, 0.00f, 2, 0, 0.0f, 0.0f, 0.20f, IT::off,       80.0f, 250.0f },
        { "Rubber Band",        0.25f, 0.15f, 4.0f,  0, 1000.0f, 36.0f, 180.0f, 0.35f, 3, 0, 0.0f, 0.0f, 0.15f, IT::off,       40.0f, 120.0f },
        { "Tear Out",           0.10f, 0.35f, 0.0f,  0, 650.0f,  42.0f, 220.0f, 0.60f, 0, 3, 0.7f, 0.5f, 0.35f, IT::freqShift, 0.0f,  100.0f },
        { "Deep House Bass",    0.30f, 0.05f, 0.0f,  1, 1100.0f, 18.0f, 300.0f, 0.15f, 3, 0, 0.0f, 0.0f, 0.10f, IT::off,       0.0f,  150.0f },
        { "Garage Organ Bass",  0.40f, 0.10f, 0.0f,  2, 1500.0f, 0.0f,  400.0f, 0.00f, 0, 4, 0.4f, 0.0f, 0.15f, IT::off,       0.0f,  150.0f },
        { "Hollow Reese",       0.00f, 0.05f, 0.0f,  1, 600.0f,  6.0f,  700.0f, 0.25f, 1, 0, 0.0f, 0.0f, 0.65f, IT::comb,      0.0f,  200.0f },
        { "Talking Bass",       0.05f, 0.20f, 0.0f,  0, 750.0f,  30.0f, 400.0f, 0.40f, 0, 2, 0.0f, 0.9f, 0.25f, IT::formant,   0.0f,  120.0f },
        { "Pluck Bass",         0.20f, 0.15f, 8.0f,  1, 600.0f,  42.0f, 150.0f, 0.30f, 3, 0, 0.0f, 0.0f, 0.15f, IT::off,       0.0f,  100.0f },
        { "Distorted Square",   0.90f, 0.40f, 0.0f,  0, 1200.0f, 18.0f, 400.0f, 0.20f, 3, 0, 0.0f, 0.0f, 0.10f, IT::drive,     0.0f,  120.0f },
        { "Moog Thump",         0.10f, 0.10f, 5.0f,  0, 450.0f,  30.0f, 250.0f, 0.35f, 3, 0, 0.0f, 0.0f, 0.10f, IT::off,       30.0f, 120.0f },
        { "Halftime Monster",   0.00f, 0.30f, 0.0f,  0, 500.0f,  36.0f, 500.0f, 0.50f, 0, 5, 0.8f, 0.3f, 0.40f, IT::comb,      0.0f,  150.0f },
        { "Laser Reese",        0.00f, 0.10f, 0.0f,  1, 900.0f,  24.0f, 300.0f, 0.30f, 1, 0, 0.0f, 0.0f, 0.70f, IT::flanger,   0.0f,  150.0f },
        { "Grime Square",       0.70f, 0.20f, 0.0f,  1, 1400.0f, 12.0f, 200.0f, 0.10f, 3, 0, 0.0f, 0.0f, 0.10f, IT::crush,     0.0f,  100.0f },
        { "Velvet Sub",         0.00f, 0.00f, 0.0f,  2, 600.0f,  0.0f,  500.0f, 0.00f, 2, 0, 0.0f, 0.0f, 0.15f, IT::off,       0.0f,  250.0f },
        { "Acid Bite",          0.20f, 0.15f, 0.0f,  0, 350.0f,  36.0f, 300.0f, 0.75f, 3, 0, 0.0f, 0.0f, 0.10f, IT::drive,     60.0f, 100.0f },
        { "Formant Wobble",     0.05f, 0.20f, 0.0f,  0, 700.0f,  24.0f, 350.0f, 0.45f, 2, 0, 0.6f, 0.0f, 0.30f, IT::formant,   0.0f,  120.0f },
        { "Trap Glide 808",     0.00f, 0.35f, 10.0f, 2, 1800.0f, 0.0f,  900.0f, 0.00f, 3, 0, 0.0f, 0.0f, 0.10f, IT::drive,     120.0f, 500.0f },
        { "Dubstep Saw",        0.00f, 0.25f, 0.0f,  1, 1500.0f, 36.0f, 250.0f, 0.40f, 1, 0, 0.0f, 0.0f, 0.50f, IT::drive,     0.0f,  120.0f },
        { "Electro Stomp",      0.30f, 0.25f, 6.0f,  0, 900.0f,  30.0f, 180.0f, 0.35f, 0, 1, 0.5f, 0.3f, 0.30f, IT::off,       0.0f,  100.0f },
        { "Warm Analog Bass",   0.10f, 0.05f, 0.0f,  0, 800.0f,  18.0f, 400.0f, 0.20f, 3, 0, 0.0f, 0.0f, 0.10f, IT::off,       0.0f,  150.0f },
        { "Bit Crusher Bass",   0.40f, 0.20f, 0.0f,  0, 900.0f,  24.0f, 300.0f, 0.30f, 0, 5, 0.4f, 0.2f, 0.25f, IT::crush,     0.0f,  120.0f },
        { "Phaser Reese",       0.00f, 0.05f, 0.0f,  1, 700.0f,  6.0f,  600.0f, 0.10f, 1, 0, 0.0f, 0.0f, 0.80f, IT::phaser,    0.0f,  200.0f },
        { "Mid Bass Punch",     0.20f, 0.30f, 7.0f,  1, 1300.0f, 24.0f, 200.0f, 0.20f, 3, 0, 0.0f, 0.0f, 0.10f, IT::transient, 0.0f,  100.0f },
        { "Throat Growl",       0.10f, 0.25f, 0.0f,  0, 550.0f,  36.0f, 280.0f, 0.55f, 0, 2, 0.4f, 0.8f, 0.30f, IT::formant,   0.0f,  100.0f },
        { "Spectral Bass",      0.00f, 0.10f, 0.0f,  2, 900.0f,  0.0f,  400.0f, 0.00f, 2, 0, 0.4f, 0.0f, 0.30f, IT::off,       0.0f,  150.0f },
        { "Gnarly Saw Bass",    0.10f, 0.35f, 0.0f,  1, 1100.0f, 30.0f, 300.0f, 0.45f, 1, 0, 0.0f, 0.0f, 0.45f, IT::drive,     0.0f,  120.0f },
        { "Shifted Bass",       0.00f, 0.20f, 0.0f,  0, 800.0f,  24.0f, 350.0f, 0.30f, 0, 3, 0.3f, 0.4f, 0.30f, IT::freqShift, 0.0f,  120.0f },
        { "Slide Bass",         0.15f, 0.15f, 0.0f,  0, 900.0f,  24.0f, 300.0f, 0.30f, 3, 0, 0.0f, 0.0f, 0.15f, IT::off,       90.0f, 150.0f },
        { "Final Boss Bass",    0.10f, 0.40f, 6.0f,  0, 650.0f,  42.0f, 250.0f, 0.55f, 0, 5, 0.7f, 0.5f, 0.35f, IT::drive,     0.0f,  120.0f },
    };

    // ==== Pad / Atmos ========================================================================================
    struct PadRow
    {
        const char* name;
        float attackMs, releaseMs;
        int midKind;          // 0 Spectral, 1 Wavetable, 2 JP soft saw
        int a, b;             // spectral sources or wavetable table (a)
        float morph;          // spectral morph or wavetable position
        float smear;
        float chorus, reverb, reverbSize;
        float cutoffHz;
    };

    Preset pad (const PadRow& r)
    {
        auto x = make (r.name, Category::pad);
        subLayer (x, 0.0f);
        level (x, sub, -10.0f);
        jpLayer (x, body, Role::body, { 2, 1, 0.4f, 0.6f, 0.5f, r.cutoffHz, 0.05f, 0.0f, 400.0f, 0.0f });
        level (x, body, -5.0f);
        switch (r.midKind)
        {
            case 0:  specLayer (x, mid, Role::mid, { r.a, r.b, r.morph, -1.0f, 0.0f, r.smear, 0.0f, 0.0f, 2, 0.4f, 1.0f }); break;
            case 1:  wtLayer (x, mid, Role::mid, { r.a, r.morph, 0.2f, 3000.0f, 0.0f, 3, 0.35f, 0.75f, 1.0f }); break;
            default: jpLayer (x, mid, Role::mid, { 0, 3, 0.45f, 0.7f, 1.0f, r.cutoffHz, 0.05f, 0.0f, 400.0f, 0.0f }); break;
        }
        specLayer (x, air, Role::air, { 8, 6, 0.3f, 2.0f, 0.0f, 0.5f, 0.0f, 0.0f, 2, 0.5f, 1.0f });
        level (x, air, -9.0f);
        ampAll (x, r.attackMs, 1000.0f, 100.0f, r.releaseMs);
        x.set (p::layerChorus (mid), r.chorus);
        space (x, r.reverb, r.reverbSize);
        return x;
    }

    const PadRow padRows[] = {
        { "Mash Pad",           400.0f, 2500.0f, 0, 0, 6, 0.3f, 0.2f, 30.0f, 40.0f, 3.5f, 8000.0f },
        { "Aurora Wash",        900.0f, 4000.0f, 0, 8, 6, 0.5f, 0.6f, 40.0f, 60.0f, 6.0f, 9000.0f },
        { "Velvet Cloud",       600.0f, 3000.0f, 2, 0, 0, 0.0f, 0.0f, 50.0f, 45.0f, 4.0f, 4000.0f },
        { "Choir of Glass",     800.0f, 3500.0f, 0, 6, 8, 0.4f, 0.3f, 25.0f, 55.0f, 5.0f, 10000.0f },
        { "Digital Horizon",    500.0f, 3000.0f, 1, 5, 0, 0.5f, 0.0f, 20.0f, 40.0f, 3.5f, 9000.0f },
        { "Warm Blanket",       700.0f, 2800.0f, 2, 0, 0, 0.0f, 0.0f, 60.0f, 35.0f, 3.0f, 3000.0f },
        { "Breathing Vowels",   1000.0f, 4000.0f, 1, 2, 0, 0.4f, 0.0f, 30.0f, 50.0f, 5.0f, 7000.0f },
        { "Deep Space",         1500.0f, 6000.0f, 0, 4, 8, 0.6f, 0.8f, 20.0f, 70.0f, 9.0f, 6000.0f },
        { "Organ Halo",         300.0f, 2000.0f, 0, 5, 8, 0.3f, 0.2f, 40.0f, 45.0f, 4.0f, 8000.0f },
        { "Supersaw Pad",       350.0f, 2500.0f, 2, 0, 0, 0.0f, 0.0f, 30.0f, 40.0f, 3.5f, 9000.0f },
        { "Frozen Lake",        1200.0f, 5000.0f, 0, 8, 4, 0.5f, 0.7f, 20.0f, 65.0f, 8.0f, 12000.0f },
        { "PWM Strings",        450.0f, 2200.0f, 1, 1, 0, 0.5f, 0.0f, 50.0f, 40.0f, 3.0f, 6000.0f },
        { "Dusk Atmos",         1100.0f, 4500.0f, 0, 7, 6, 0.5f, 0.5f, 30.0f, 60.0f, 6.5f, 5000.0f },
        { "Reso Swell",         800.0f, 3000.0f, 1, 3, 0, 0.3f, 0.0f, 30.0f, 45.0f, 4.0f, 8000.0f },
        { "Heaven Stack",       600.0f, 4000.0f, 2, 0, 0, 0.0f, 0.0f, 40.0f, 55.0f, 6.0f, 12000.0f },
        { "Soft Focus",         700.0f, 3000.0f, 0, 2, 4, 0.5f, 0.3f, 35.0f, 50.0f, 4.5f, 5000.0f },
        { "Drawbar Dream",      400.0f, 2500.0f, 1, 4, 0, 0.6f, 0.0f, 45.0f, 45.0f, 4.0f, 7000.0f },
        { "Nebula",             1400.0f, 5500.0f, 0, 0, 8, 0.7f, 0.9f, 25.0f, 70.0f, 8.0f, 9000.0f },
        { "Analog Strings",     500.0f, 2400.0f, 2, 0, 0, 0.0f, 0.0f, 55.0f, 40.0f, 3.5f, 5000.0f },
        { "Crystal Garden",     900.0f, 4000.0f, 0, 8, 5, 0.4f, 0.4f, 30.0f, 55.0f, 5.5f, 14000.0f },
        { "Vowel Drift",        1000.0f, 4000.0f, 0, 6, 7, 0.5f, 0.4f, 30.0f, 50.0f, 5.0f, 6000.0f },
        { "Dark Matter",        1300.0f, 5000.0f, 1, 3, 0, 0.6f, 0.0f, 25.0f, 60.0f, 7.0f, 3000.0f },
        { "Golden Hour",        650.0f, 3200.0f, 2, 0, 0, 0.0f, 0.0f, 45.0f, 50.0f, 4.5f, 7000.0f },
        { "Ocean Floor",        1500.0f, 6000.0f, 0, 4, 7, 0.4f, 0.8f, 30.0f, 65.0f, 8.5f, 3500.0f },
        { "Glass Cathedral",    800.0f, 5000.0f, 0, 8, 5, 0.6f, 0.5f, 20.0f, 70.0f, 10.0f, 12000.0f },
        { "Retro Strings",      450.0f, 2000.0f, 1, 1, 0, 0.3f, 0.0f, 60.0f, 35.0f, 3.0f, 5500.0f },
        { "Shimmer Veil",       1000.0f, 4500.0f, 0, 0, 8, 0.8f, 0.6f, 30.0f, 60.0f, 7.0f, 15000.0f },
        { "Haze",               1200.0f, 4000.0f, 1, 5, 0, 0.7f, 0.0f, 35.0f, 55.0f, 6.0f, 4500.0f },
        { "Big Room Pad",       300.0f, 2000.0f, 2, 0, 0, 0.0f, 0.0f, 30.0f, 35.0f, 3.0f, 10000.0f },
        { "Afterglow",          900.0f, 5000.0f, 0, 2, 8, 0.6f, 0.6f, 30.0f, 65.0f, 7.5f, 8000.0f },
    };

    // ==== Pluck / Stab =======================================================================================
    struct PluckRow
    {
        const char* name;
        float decayMs;
        int bodyKind;         // 0 Genome body strike, 1 Genome Jupiter, 2 Dirt, 3 Genome FM
        float bodyTone;       // genome body tone / dirt cutoff scale
        int unison;
        float cutoffHz, envSemis, envDecayMs;
        float delay, reverb;
    };

    Preset pluck (const PluckRow& r)
    {
        auto x = make (r.name, Category::pluck);
        subLayer (x, 0.0f);
        level (x, sub, -8.0f);
        switch (r.bodyKind)
        {
            case 0:  genomeLayer (x, body, Role::body, { 1.0f, 0.0f, 0.1f, 0.35f, 0.25f, 0.3f, 0.25f, 0.5f, 0.5f, 0.45f, r.bodyTone, 0.7f }); break;
            case 1:  genomeLayer (x, body, Role::body, { 0.0f, 0.0f, 0.3f, 0.35f, 0.25f, 0.3f, 0.25f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, r.cutoffHz * 0.7f }); break;
            case 2:  dirtLayer (x, body, Role::body, { 0.5f, 0.2f, 0.0f, 0.0f, 0.2f, r.cutoffHz * 0.3f * (0.5f + r.bodyTone), 0.3f, r.envSemis, r.envDecayMs, 0.0f }); break;
            default: genomeLayer (x, body, Role::body, { 0.25f, 0.0f, 0.15f, fmIndex (2.5f), fmRatio (r.bodyTone < 0.5f ? 1.0f : 3.0f), 0.2f, 0.8f, 0.3f }); break;
        }
        level (x, body, -3.0f);
        jpLayer (x, mid, Role::mid, { 0, r.unison, 0.45f, 0.7f, 0.8f, r.cutoffHz, 0.25f, r.envSemis, r.envDecayMs, 0.0f });
        specLayer (x, air, Role::air, { 8, 0, 0.2f, 2.0f, 0.0f, 0.1f });
        level (x, air, -10.0f);
        ampAll (x, 1.0f, r.decayMs, 0.0f, r.decayMs * 0.6f);
        space (x, r.reverb, 2.0f, r.delay, 6, 35.0f);
        return x;
    }

    const PluckRow pluckRows[] = {
        { "Mash Pluck",        450.0f, 0, 0.0f, 1, 600.0f,  48.0f, 250.0f, 20.0f, 20.0f },
        { "Future Pluck",      350.0f, 1, 0.0f, 2, 500.0f,  54.0f, 200.0f, 25.0f, 25.0f },
        { "Marimba Mash",      600.0f, 0, 1.0f, 0, 2000.0f, 24.0f, 300.0f, 15.0f, 15.0f },
        { "Trance Stab",       300.0f, 2, 0.6f, 3, 900.0f,  42.0f, 180.0f, 25.0f, 20.0f },
        { "Bell Pluck",        900.0f, 3, 0.7f, 0, 3000.0f, 24.0f, 400.0f, 20.0f, 30.0f },
        { "Kalimba Drop",      700.0f, 0, 0.6f, 0, 1500.0f, 30.0f, 250.0f, 18.0f, 18.0f },
        { "Chord Stab",        250.0f, 2, 0.5f, 2, 1200.0f, 36.0f, 150.0f, 20.0f, 25.0f },
        { "Harp Glint",        800.0f, 0, 0.2f, 1, 2500.0f, 24.0f, 350.0f, 25.0f, 35.0f },
        { "Zap Pluck",         200.0f, 1, 0.0f, 1, 400.0f,  66.0f, 90.0f,  20.0f, 15.0f },
        { "Glass Mallet",      650.0f, 0, 0.9f, 0, 4000.0f, 18.0f, 300.0f, 20.0f, 30.0f },
        { "House Organ Stab",  300.0f, 3, 0.2f, 0, 3000.0f, 12.0f, 200.0f, 15.0f, 20.0f },
        { "Big Room Stab",     280.0f, 2, 0.7f, 4, 1500.0f, 36.0f, 160.0f, 20.0f, 25.0f },
        { "Soft Pluck",        500.0f, 1, 0.0f, 1, 700.0f,  30.0f, 300.0f, 25.0f, 30.0f },
        { "Nylon Mash",        700.0f, 0, 0.1f, 0, 1800.0f, 24.0f, 350.0f, 15.0f, 20.0f },
        { "Pizz Strings",      260.0f, 2, 0.4f, 1, 1000.0f, 36.0f, 120.0f, 15.0f, 25.0f },
        { "Digital Pluck",     400.0f, 3, 0.8f, 2, 1200.0f, 42.0f, 200.0f, 25.0f, 20.0f },
        { "Dub Stab",          350.0f, 2, 0.3f, 1, 800.0f,  30.0f, 220.0f, 40.0f, 25.0f },
        { "Ice Pick",          220.0f, 3, 0.9f, 0, 5000.0f, 24.0f, 120.0f, 20.0f, 20.0f },
        { "Tropical Pluck",    450.0f, 0, 0.4f, 1, 1200.0f, 36.0f, 220.0f, 25.0f, 25.0f },
        { "Wide Stab",         300.0f, 1, 0.0f, 4, 900.0f,  42.0f, 170.0f, 20.0f, 30.0f },
        { "Woody Pluck",       550.0f, 0, 0.0f, 0, 1000.0f, 24.0f, 280.0f, 15.0f, 15.0f },
        { "Rave Stab",         260.0f, 2, 0.8f, 3, 1400.0f, 48.0f, 140.0f, 25.0f, 20.0f },
        { "Crystal Pluck",     750.0f, 3, 0.6f, 1, 3500.0f, 30.0f, 300.0f, 25.0f, 35.0f },
        { "Muted Guitar",      200.0f, 0, 0.2f, 0, 700.0f,  24.0f, 100.0f, 10.0f, 10.0f },
        { "Laser Pluck",       300.0f, 1, 0.0f, 2, 450.0f,  66.0f, 110.0f, 25.0f, 20.0f },
        { "Steel Drum",        600.0f, 0, 0.8f, 0, 2500.0f, 18.0f, 250.0f, 15.0f, 25.0f },
        { "Hollow Stab",       320.0f, 2, 0.2f, 2, 700.0f,  30.0f, 200.0f, 20.0f, 30.0f },
        { "FM Pluck",          400.0f, 3, 0.3f, 1, 2000.0f, 24.0f, 200.0f, 20.0f, 20.0f },
        { "Echo Drop",         350.0f, 1, 0.0f, 1, 600.0f,  48.0f, 180.0f, 45.0f, 25.0f },
        { "Chime Stab",        500.0f, 0, 1.0f, 2, 3000.0f, 24.0f, 250.0f, 20.0f, 30.0f },
    };

    // ==== Chord / Keys =======================================================================================
    struct KeysRow
    {
        const char* name;
        int kind;              // 0 FM e-piano, 1 Spectral organ, 2 Wavetable drawbars, 3 Genome bell, 4 JP poly
        float decayMs, sustain, brightness, reverb;
        IT midInsert;
    };

    Preset keys (const KeysRow& r)
    {
        auto x = make (r.name, Category::keys);
        subLayer (x, 0.0f);
        level (x, sub, -10.0f);
        switch (r.kind)
        {
            case 0:  genomeLayer (x, body, Role::body, { 0.25f, 0.0f, 0.05f, fmIndex (1.2f + 2.0f * r.brightness), fmRatio (1.0f), 0.1f, 0.7f, 0.45f }); break;
            case 1:  specLayer (x, body, Role::body, { 5, 4, 0.2f, -1.0f }); break;
            case 2:  wtLayer (x, body, Role::body, { 4, 0.3f + 0.5f * r.brightness, 0.0f, 500.0f, 0.0f, 0, 0.0f, 0.5f, 0.0f }); break;
            case 3:  genomeLayer (x, body, Role::body, { 1.0f, 0.0f, 0.0f, 0.35f, 0.25f, 0.3f, 0.25f, 0.5f, 0.5f, 0.6f, 1.0f, 0.6f }); break;
            default: jpLayer (x, body, Role::body, { 0, 0, 0.0f, 0.6f, 0.4f, 1000.0f + 6000.0f * r.brightness, 0.15f, 24.0f, 500.0f, 0.3f }); break;
        }
        level (x, body, -3.0f);
        genomeLayer (x, mid, Role::mid, { r.kind == 1 || r.kind == 2 ? 0.75f : 0.25f, 0.0f, 0.15f, fmIndex (0.8f + 2.0f * r.brightness), fmRatio (2.0f),
                                          0.15f, 0.6f, 0.4f, 0.6f });
        level (x, mid, -6.0f);
        specLayer (x, air, Role::air, { 8, 5, 0.3f, 1.0f });
        level (x, air, -12.0f);
        ampAll (x, 2.0f, r.decayMs, r.sustain, 500.0f);
        x.set (p::layerChorus (body), 25.0f);
        if (r.midInsert != IT::off)
            insert (x, body, 0, r.midInsert, 30.0f);
        space (x, r.reverb, 2.2f);
        return x;
    }

    const KeysRow keysRows[] = {
        { "Mash Keys", 0,        1500.0f, 40.0f, 0.4f, 25.0f, IT::off },
        { "Electric Tines", 0,   2500.0f, 30.0f, 0.5f, 25.0f, IT::phaser },
        { "Gospel Organ", 1,     800.0f,  90.0f, 0.5f, 25.0f, IT::off },
        { "Glass Bells", 3,      3000.0f, 10.0f, 0.7f, 40.0f, IT::off },
        { "Drawbar Classic", 2,  1000.0f, 100.0f, 0.4f, 20.0f, IT::drive },
        { "House Chords", 4,     600.0f,  50.0f, 0.6f, 25.0f, IT::off },
        { "Soft Rhodes", 0,      2000.0f, 35.0f, 0.2f, 30.0f, IT::off },
        { "Digital Piano", 0,    2500.0f, 20.0f, 0.6f, 25.0f, IT::off },
        { "Church Pipes", 1,     1000.0f, 95.0f, 0.3f, 50.0f, IT::off },
        { "Music Box", 3,        1800.0f, 0.0f,  0.9f, 35.0f, IT::off },
        { "Lo-Fi Keys", 0,       1500.0f, 30.0f, 0.3f, 20.0f, IT::crush },
        { "Poly Brass Keys", 4,  700.0f,  70.0f, 0.5f, 20.0f, IT::off },
        { "Vibe Mallets", 3,     2200.0f, 15.0f, 0.5f, 30.0f, IT::phaser },
        { "Combo Organ", 2,      800.0f,  100.0f, 0.7f, 20.0f, IT::off },
        { "Clav Funk", 4,        500.0f,  20.0f, 0.8f, 15.0f, IT::off },
        { "Detroit Chords", 4,   900.0f,  45.0f, 0.5f, 30.0f, IT::off },
        { "Celesta Mash", 3,     2000.0f, 15.0f,  0.8f, 35.0f, IT::off },
        { "Warm Poly", 4,        1200.0f, 60.0f, 0.3f, 30.0f, IT::off },
        { "Tonewheel Drive", 2,  1000.0f, 100.0f, 0.6f, 20.0f, IT::drive },
        { "Dream Keys", 0,       2500.0f, 40.0f, 0.4f, 45.0f, IT::off },
    };

    // ==== Arp / Sequence =====================================================================================
    struct ArpRow
    {
        const char* name;
        int rate;              // delayDivisionNames index (1/16 = 2, 1/8 = 5, 1/16T = 1)
        int direction, octaves;
        float gate, swing;
        int sound;             // 0 pluck saw, 1 genome body, 2 FM, 3 dirt bass, 4 supersaw
        float delay;
        NoteGenerator::Mode mode = NoteGenerator::Mode::arp;
        // The step pattern, one token per step (empty keeps the default 16 even steps): a note offset in
        // semitones (the arp ignores it), "-" a rest, "_" a tie. Suffixes: "!" accent, "?" plays half the
        // time, "x2".."x4" ratchet. With accents in the pattern the other steps play softer.
        const char* pattern = "";
    };

    std::vector<NoteGenerator::Step> parseSteps (const char* pattern)
    {
        const auto tokens = juce::StringArray::fromTokens (pattern, " ", {});
        const bool accents = juce::String (pattern).containsChar ('!');
        std::vector<NoteGenerator::Step> steps;
        for (auto t : tokens)
        {
            NoteGenerator::Step st;
            st.velocity = accents && ! t.containsChar ('!') ? 0.75f : 1.0f;
            if (t.containsChar ('?'))
                st.probability = 0.5f;
            if (const int x = t.indexOfChar ('x'); x > 0)
                st.ratchet = t.substring (x + 1).getIntValue();
            t = t.removeCharacters ("!?").upToFirstOccurrenceOf ("x", false, false);
            if (t == "-")
                st.on = false;
            else if (t == "_")
                st.tie = true;
            else
                st.offset = t.getIntValue();
            steps.push_back (st);
        }
        return steps;
    }

    Preset arp (const ArpRow& r)
    {
        auto x = make (r.name, Category::arp);
        subLayer (x, 0.0f);
        level (x, sub, -9.0f);
        switch (r.sound)
        {
            case 0:  jpLayer (x, body, Role::body, { 0, 0, 0.0f, 0.6f, 0.4f, 700.0f, 0.3f, 48.0f, 180.0f, 0.0f }); break;
            case 1:  genomeLayer (x, body, Role::body, { 1.0f, 0.0f, 0.0f, 0.35f, 0.25f, 0.3f, 0.25f, 0.5f, 0.5f, 0.4f, 0.5f, 0.7f }); break;
            case 2:  genomeLayer (x, body, Role::body, { 0.25f, 0.0f, 0.1f, fmIndex (2.5f), fmRatio (2.0f), 0.2f, 0.8f, 0.3f }); break;
            case 3:  dirtLayer (x, body, Role::body, { 0.5f, 0.3f, 0.0f, 0.0f, 0.3f, 500.0f, 0.45f, 42.0f, 160.0f, 0.0f }); break;
            default: jpLayer (x, body, Role::body, { 0, 1, 0.5f, 0.7f, 0.6f, 2000.0f, 0.15f, 30.0f, 200.0f, 0.2f }); break;
        }
        level (x, body, -3.0f);
        jpLayer (x, mid, Role::mid, { 0, r.sound == 4 ? 4 : 1, 0.45f, 0.65f, 0.8f, 900.0f, 0.25f, 42.0f, 160.0f, 0.0f });
        level (x, mid, -4.0f);
        specLayer (x, air, Role::air, { 8, 0, 0.2f, 2.0f });
        level (x, air, -12.0f);
        ampAll (x, 1.0f, 350.0f, 20.0f, 250.0f);
        x.set (p::arpMode, (float) (int) r.mode).set (p::arpRate, (float) r.rate).set (p::arpDirection, (float) r.direction)
         .set (p::arpOctaves, (float) r.octaves).set (p::arpGate, r.gate).set (p::arpSwing, r.swing);
        if (*r.pattern != 0)
        {
            x.steps = parseSteps (r.pattern);
            x.set (p::arpLength, (float) x.steps.size());
        }
        space (x, 20.0f, 2.0f, r.delay, 6, 40.0f);
        return x;
    }

    constexpr auto seqMode = NoteGenerator::Mode::seq, arpMode = NoteGenerator::Mode::arp;
    const ArpRow arpRows[] = {
        { "Mash Arp",          2, 0, 2, 60.0f, 0.0f,  0, 25.0f },
        { "Trance Runner",     2, 2, 3, 50.0f, 0.0f,  0, 30.0f },
        { "Bell Cascade",      2, 1, 2, 70.0f, 0.0f,  1, 30.0f },
        { "FM Sparkle",        1, 0, 3, 45.0f, 0.0f,  2, 25.0f },
        { "Bassline Seq",      2, 4, 1, 55.0f, 10.0f, 3, 10.0f },
        { "Supersaw Gate",     2, 5, 1, 80.0f, 0.0f,  4, 20.0f },
        { "Swing Pluck",       2, 0, 2, 50.0f, 35.0f, 0, 25.0f },
        { "Random Glass",      2, 3, 3, 40.0f, 0.0f,  1, 35.0f },
        { "Eighth Pulse",      5, 0, 1, 80.0f, 0.0f,  0, 30.0f },
        { "Up Down Bells",     2, 2, 2, 65.0f, 10.0f, 1, 30.0f },
        { "Acid Seq",          2, 4, 2, 50.0f, 0.0f,  3, 15.0f },
        { "Chord Pump",        5, 5, 1, 70.0f, 0.0f,  4, 15.0f },
        { "Triplet Rain",      1, 1, 3, 45.0f, 0.0f,  2, 35.0f },
        { "Progressive Arp",   2, 0, 3, 55.0f, 0.0f,  0, 30.0f },
        { "Bounce Bass Arp",   2, 2, 1, 40.0f, 20.0f, 3, 10.0f },
        { "Crystal Steps",     2, 0, 4, 50.0f, 0.0f,  1, 30.0f },
        { "Retro Sequence",    2, 4, 1, 60.0f, 0.0f,  0, 20.0f },
        { "Hyper Arp",         1, 2, 4, 40.0f, 0.0f,  0, 25.0f },
        { "Garage Skip",       2, 0, 2, 55.0f, 45.0f, 2, 20.0f },
        { "Anthem Arp",        2, 2, 2, 60.0f, 0.0f,  4, 25.0f },
        // Arps on a step pattern: accents, ghost notes, ties, rests and ratchets
        { "Accent Arp",        2, 0, 2, 55.0f, 0.0f,  0, 25.0f, arpMode, "0! 0 0 0! 0 0 0! 0" },
        { "Ratchet Run",       2, 2, 2, 50.0f, 0.0f,  4, 20.0f, arpMode, "0 0 0x2 0 0 0x3 0 0x2" },
        { "Ghost Note Arp",    2, 0, 3, 45.0f, 15.0f, 1, 30.0f, arpMode, "0! 0? 0 0? 0! 0? 0 0?" },
        { "Tied Pulse",        5, 0, 1, 90.0f, 0.0f,  4, 20.0f, arpMode, "0 _ 0 _ 0 0 _ 0" },
        { "Broken Chords",     2, 4, 2, 60.0f, 0.0f,  2, 25.0f, arpMode, "0 - 0 0 - 0 0 -" },
        { "Gallop Arp",        1, 0, 2, 50.0f, 0.0f,  0, 20.0f, arpMode, "0! 0 0" },
        { "Stutter Chords",    2, 5, 1, 40.0f, 0.0f,  4, 15.0f, arpMode, "0! - 0 - 0x2 - 0 0" },
        { "Probability Rain",  2, 3, 4, 40.0f, 0.0f,  1, 35.0f, arpMode, "0? 0? 0? 0! 0? 0? 0? 0?" },
        // Step sequences, played from the last held key
        { "Acid Line",         2, 0, 1, 50.0f, 0.0f,  3, 10.0f, seqMode, "0! 0 12 0 0! 3 0 15 0! 0 10 0 0! 7 _ 12" },
        { "Offbeat House Bass",2, 0, 1, 60.0f, 0.0f,  3, 10.0f, seqMode, "- 0 - 0! - 0 - 12" },
        { "Trance Gate Seq",   2, 0, 1, 55.0f, 0.0f,  4, 25.0f, seqMode, "0 0 12 0 0 7 0 12 0 0 12 0 5 7 0 12" },
        { "Minor Melody Seq",  2, 0, 1, 70.0f, 0.0f,  0, 30.0f, seqMode, "0 3 7 10 12 10 7 3" },
        { "Rolling Bass Seq",  2, 0, 1, 45.0f, 0.0f,  3, 10.0f, seqMode, "0! 0 0 0 0! 0 7 0" },
        { "Octave Bounce",     2, 0, 1, 50.0f, 0.0f,  0, 20.0f, seqMode, "0! 12 0 12 0! 12 0 12x2" },
        { "Pentatonic Run",    1, 0, 1, 50.0f, 0.0f,  1, 30.0f, seqMode, "0 3 5 7 10 12 15 17 19 17 15 12" },
        { "Dub Echo Seq",      5, 0, 1, 40.0f, 0.0f,  2, 45.0f, seqMode, "0! - - 0 - - 7 -" },
        { "Funk Slap Seq",     2, 0, 1, 45.0f, 30.0f, 3, 10.0f, seqMode, "0! - 12 0 - 10 - 0! 7 - 0 - 12 10 7 5" },
        { "Arcade Seq",        1, 0, 1, 50.0f, 0.0f,  2, 20.0f, seqMode, "0 12 7 12 0 12 7 19" },
        { "Dark Techno Seq",   2, 0, 1, 50.0f, 0.0f,  3, 15.0f, seqMode, "0! 0 1 0 0! 0 -2 0" },
        { "Euphoric Climb",    2, 0, 1, 60.0f, 0.0f,  4, 25.0f, seqMode, "0 2 3 5 7 8 10 12 14 15 17 19 20 22 24 _" },
    };

    // ==== FX / Riser / Impact ================================================================================
    struct FxRow
    {
        const char* name;
        int kind;              // 0 Shepard rise, 1 Shepard fall, 2 Impact, 3 Sweep, 4 Shifted drone, 5 Noise wash
        float timeMs, reverb;
    };

    Preset fx (const FxRow& r)
    {
        auto x = make (r.name, Category::fx);
        subLayer (x, 0.0f, 0.2f, r.kind == 2 ? 12.0f : 0.0f);
        level (x, sub, r.kind == 2 ? -6.0f : -12.0f);
        switch (r.kind)
        {
            case 0:
            case 1:
                specLayer (x, body, Role::body, { 0, 8, 0.4f, 0.0f, 0.0f, 0.3f, 0.8f, r.kind == 0 ? 0.85f : 0.15f, 2, 0.4f, 0.8f });
                specLayer (x, mid, Role::mid, { 8, 6, 0.5f, 1.0f, 0.0f, 0.4f, 0.8f, r.kind == 0 ? 0.9f : 0.1f, 2, 0.5f, 1.0f });
                break;
            case 2:
                dirtLayer (x, body, Role::body, { 0.5f, 0.5f, 0.3f, 0.6f, 0.6f, 300.0f, 0.3f, 60.0f, 300.0f, 0.0f });
                genomeLayer (x, mid, Role::mid, { 1.0f, 0.0f, 0.0f, 0.35f, 0.25f, 0.3f, 0.25f, 0.5f, 0.5f, 0.7f, 1.0f, 1.0f });
                break;
            case 3:
                wtLayer (x, body, Role::body, { 3, 0.0f, 1.0f, r.timeMs, 0.0f, 3, 0.4f, 0.7f, 1.0f });
                wtLayer (x, mid, Role::mid, { 5, 0.0f, 1.0f, r.timeMs, 0.4f, 3, 0.5f, 0.7f, 1.0f });
                break;
            case 4:
                jpLayer (x, body, Role::body, { 0, 1, 0.5f, 0.6f, 0.6f, 3000.0f, 0.1f });
                specLayer (x, mid, Role::mid, { 5, 8, 0.5f, 0.0f, 0.0f, 0.6f, 0.0f, 0.0f, 2, 0.5f, 1.0f });
                insert (x, mid, 0, IT::freqShift, 60.0f, { 0.62f, 0.5f, 0.6f });
                break;
            default:
                dirtLayer (x, body, Role::body, { 0.5f, 0.0f, 0.0f, 1.0f, 0.2f, 2500.0f, 0.4f, 0.0f, 450.0f, 1.0f });
                specLayer (x, mid, Role::mid, { 0, 8, 0.6f, 2.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2, 0.6f, 1.0f });
                insert (x, mid, 0, IT::phaser, 60.0f, { 0.3f, 0.8f, 0.7f });
                break;
        }
        level (x, body, -4.0f);
        level (x, mid, -4.0f);
        specLayer (x, air, Role::air, { 8, 0, 0.5f, 3.0f, 0.0f, 0.8f });
        level (x, air, -10.0f);
        if (r.kind == 2)
            ampAll (x, 3.0f, r.timeMs, 0.0f, r.timeMs);
        else
            ampAll (x, r.kind == 3 ? 20.0f : r.timeMs * 0.5f, 1000.0f, 100.0f, r.timeMs * 0.5f);
        space (x, r.reverb, 6.0f, r.kind == 2 ? 0.0f : 20.0f, 6, 45.0f);
        return x;
    }

    const FxRow fxRows[] = {
        { "Endless Riser",     0, 3000.0f, 50.0f },
        { "Endless Faller",    1, 3000.0f, 50.0f },
        { "Mash Impact",       2, 3000.0f, 40.0f },
        { "Filter Sweep Up",   3, 4000.0f, 40.0f },
        { "Alien Drone",       4, 2000.0f, 50.0f },
        { "White Wash",        5, 2500.0f, 60.0f },
        { "Tension Builder",   0, 6000.0f, 55.0f },
        { "Downlifter",        1, 2000.0f, 45.0f },
        { "Sub Drop Impact",   2, 3500.0f, 30.0f },
        { "Digital Sweep",     3, 2000.0f, 35.0f },
        { "Shifted Space",     4, 3000.0f, 65.0f },
        { "Storm Noise",       5, 4000.0f, 55.0f },
        { "Climb",             0, 2000.0f, 45.0f },
        { "Big Hit",           2, 3000.0f, 50.0f },
        { "Reso Riser",        3, 6000.0f, 50.0f },
        { "Ghost Drone",       4, 5000.0f, 70.0f },
        { "Ocean Wash",        5, 5000.0f, 65.0f },
        { "Spiral Down",       1, 5000.0f, 60.0f },
        { "Boom",              2, 3000.0f, 35.0f },
        { "Pre-Drop Sweep",    3, 3000.0f, 40.0f },
    };

    // Each preset's level trim, from the Potato Ready calibration (name, dB)
    struct Trim { const char* name; float db; };
    const std::vector<Trim> trims {
#include "PresetTrims.inc"
    };

    // Master takes the trim up to its +6 dB top; the rest raises every layer the preset uses
    void applyTrim (Preset& x)
    {
        // The master stays at unity or under (10 dB over its default); a preset that needs more gets it in its layers
        constexpr float ceiling = p::masterDefaultDb + 10.0f;
        const float db = trimDb (x.name);
        x.set (p::master, juce::jmin (ceiling, db));
        if (db <= ceiling)
            return;
        for (int l = 0; l < p::numLayers; ++l)
        {
            if (x.engines[(size_t) l].isEmpty())
                continue;
            float current = 0.0f;
            for (const auto& [id, v] : x.values)
                if (id == p::layerLevel (l).getParamID())
                    current = v;
            level (x, l, juce::jmin (6.0f, current + db - ceiling));
        }
    }
}

float trimDb (const juce::String& name)
{
    for (const auto& t : trims)
        if (name == t.name)
            return t.db;
    return 0.0f;
}

const std::vector<Preset>& factory()
{
    static const std::vector<Preset> bank = []
    {
        std::vector<Preset> b;
        auto init = make ("Init", Category::init);
        init.engines = { "sub", "dirt", "jp", "spectral", "", "", "", "" };
        b.push_back (init);
        for (const auto& r : leadRows)   b.push_back (lead (r));
        for (const auto& r : bassRows)   b.push_back (bass (r));
        for (const auto& r : padRows)    b.push_back (pad (r));
        for (const auto& r : pluckRows)  b.push_back (pluck (r));
        for (const auto& r : keysRows)   b.push_back (keys (r));
        for (const auto& r : arpRows)    b.push_back (arp (r));
        for (const auto& r : fxRows)     b.push_back (fx (r));
        // Init stays the fresh-instance state, so it isn't trimmed
        for (size_t i = 1; i < b.size(); ++i)
            applyTrim (b[i]);
        return b;
    }();
    return bank;
}
}
