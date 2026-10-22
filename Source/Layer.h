#pragma once
#include "Engine.h"
#include "Envelope.h"
#include "Filter.h"
#include "Drive.h"
#include "Inserts.h"
#include "PhaseAlign.h"
#include "RoleBand.h"
#include "HarmonyEngine.h"
#include "Params.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

namespace ps
{
// Cached host parameter pointers for one layer (see Params.h for units and ranges).
struct LayerParams
{
    std::atomic<float>* levelDb = nullptr;
    std::atomic<float>* pan = nullptr;
    std::atomic<float>* octave = nullptr;
    std::atomic<float>* semi = nullptr;
    std::atomic<float>* fineCents = nullptr;
    std::atomic<float>* attackMs = nullptr;
    std::atomic<float>* decayMs = nullptr;
    std::atomic<float>* sustainPercent = nullptr;
    std::atomic<float>* releaseMs = nullptr;
    std::atomic<float>* voiceMode = nullptr;   // params::VoiceMode index
    std::atomic<float>* glideMs = nullptr;
    std::atomic<float>* role = nullptr;            // params::Role index
    std::atomic<float>* crossoverLowHz = nullptr;  // global, shared by every layer
    std::atomic<float>* crossoverHighHz = nullptr;
    std::array<std::atomic<float>*, Engine::numSlots> slots {};   // engine slots e1..e24, meaning set by the engine
    std::atomic<float>* filterType = nullptr;          // params::FilterType index
    std::atomic<float>* cutoffHz = nullptr;
    std::atomic<float>* resonance = nullptr;           // percent
    std::atomic<float>* filterEnvSemis = nullptr;
    std::atomic<float>* keyTrack = nullptr;            // percent
    std::atomic<float>* filterAttackMs = nullptr;
    std::atomic<float>* filterDecayMs = nullptr;
    std::atomic<float>* filterSustainPercent = nullptr;
    std::atomic<float>* filterReleaseMs = nullptr;
    std::atomic<float>* drive = nullptr;               // percent
    std::atomic<float>* chorus = nullptr;              // percent
    std::atomic<float>* chorusRateHz = nullptr;
    std::atomic<float>* chorusDepth = nullptr;         // percent
};

// One slot of the stack: an engine plus its per-layer controls.
//
// Engine handover is lock-free. The message thread prepares a new engine and posts it to `pending`.
// The audio thread picks it up at the start of a block, keeps the old engine rendering while its voices
// fade out, then hands it to a small `retired` queue, which the message thread empties. Nothing is allocated or
// freed on the audio thread.
//
// The layer owns each voice's amp envelope, so every engine gets the same click-free attack, release and
// stealing. The outgoing engine keeps its own copy of the envelopes while it fades.
//
// Voice modes: Poly plays note slot i on voice i. Mono and Legato play every note on voice 0 with last-note
// priority; releasing the newest key returns to the previous held one. Mono retriggers the envelope on every
// note and glides whenever glide is up; Legato only glides between overlapping notes and does not retrigger.
class Layer
{
public:
    ~Layer()
    {
        delete pending.exchange (nullptr);
        collectGarbage();
        delete outgoing;
        delete active;
    }

    // ---- Message thread ----------------------------------------------------------------------------
    void requestEngine (std::unique_ptr<Engine> engine, const juce::String& id)
    {
        engineId = id;
        if (engine == nullptr)
            engine = std::make_unique<Empty>();   // an explicit "no engine" so the old one still fades out
        engine->prepare (sampleRate.load(), blockSize.load(), numVoices);
        engine->setTune (tune.load());
        for (int k = 0; k < Engine::numSlots; ++k)
            if (params.slots[(size_t) k] != nullptr)
                engine->setSlot (k, params.slots[(size_t) k]->load());
        delete pending.exchange (engine.release());
    }

    void collectGarbage()
    {
        for (auto& r : retired)
            delete r.exchange (nullptr);
    }

    const juce::String& getEngineId() const noexcept { return engineId; }

    // ---- Called while audio is stopped -----------------------------------------------------------
    void prepare (double sr, int maxBlock, int voices)
    {
        sampleRate = sr;
        blockSize = maxBlock;
        numVoices = voices;
        scratchL.assign ((size_t) maxBlock, 0.0f);
        scratchR.assign ((size_t) maxBlock, 0.0f);
        voiceL.assign ((size_t) maxBlock, 0.0f);
        voiceR.assign ((size_t) maxBlock, 0.0f);
        startOrder.assign ((size_t) voices, 0);
        envelopes.assign ((size_t) voices, {});
        outgoingEnvelopes.assign ((size_t) voices, {});
        voiceFilters.assign ((size_t) voices, {});
        outgoingFilters.assign ((size_t) voices, {});
        filterType = currentFilterType();
        filterFade = 0;
        layerDrive.prepare (maxBlock);
        chorus.prepare (sr);
        probe.prepare (sr);
        alignDelay.prepare (sr);
        clearAlignment();
        level.reset (sr, 0.02);
        level.setCurrentAndTargetValue (targetGain());
        for (auto* g : { &panL, &panR })
            g->reset (sr, 0.02);
        updateControls (true);
        updateEnvelopeSettings();
        band.prepare (sr);
        updateBand (true);
        meter.prepare (sr, false);
        carvedMeter.prepare (sr, false);
        carve.prepare (sr);
        duckRate = (float) sr;
        duck.attackSamples = 0.002f * duckRate;

        for (auto* e : { active, outgoing })
            if (e != nullptr)
                e->prepare (sr, maxBlock, voices);

        if (auto* p = pending.exchange (nullptr))
        {
            p->prepare (sr, maxBlock, voices);
            delete pending.exchange (p);
        }
    }

    void setParameters (const LayerParams& p) { params = p; }

    // Audio thread, before beginBlock: the chain knobs' offsets for this layer (ChainKnobs.h)
    void setChainOffsets (float levelDb, const std::array<float, numSlotRoles>& slotOffsets) noexcept
    {
        chainLevelDb = levelDb;
        chainSlots = slotOffsets;
    }

    // BLOOM's pitch drift on top of the layer's tune, in cents (the engines' setTune is phase-continuous)
    void setChainDrift (float cents) noexcept { chainDriftCents = cents; }

    // The chain knobs' drive and chorus on top of the patch's (0..1 each)
    void setChainInserts (float drive, float chorusAmount) noexcept
    {
        chainDrive = drive;
        chainChorus = chorusAmount;
    }

    // GROOVE's inter-layer ducking: each new note dips this layer by depthDb for about windowMs, so the
    // transient layers' attacks read through it (forward masking). 0 dB turns it off.
    void setOnsetDuck (float depthDb, float windowMs) noexcept
    {
        duck.depth = 1.0f - juce::Decibels::decibelsToGain (-juce::jmax (0.0f, depthDb));
        duck.releaseSamples = juce::jmax (1.0f, windowMs * 0.001f * duckRate);
    }

    // The patch's own level and the chain knobs' extra gain, for the Harmony Engine's loudness model
    float patchGain() const noexcept { return targetPatchGain(); }
    float knobGain() const noexcept  { return juce::Decibels::decibelsToGain (chainLevelDb); }
    // K-weighted power of this layer at unity gain (after its band limit), averaged over 400 ms
    float measuredPower() const noexcept { return meter.getPower(); }

    // CLARITY's band ownership (HarmonyEngine.h): the dips the solver asks for, the band energies it reads,
    // and the power the dips take (P_out / P_in, 1 while flat), which the loudness trim counts as the knobs'
    void setCarveGains (const std::array<float, harmony::CarveBank::numBands>& gains, bool analyse) noexcept
    {
        carveTargets = gains;
        carveAnalyse = analyse;
    }
    const std::array<float, harmony::CarveBank::numBands>& carveEnergy() const noexcept { return carve.bandEnergy(); }
    float carvePowerRatio() const noexcept
    {
        return carving ? 1.0f / (float) harmony::stageRatio (meter, carvedMeter) : 1.0f;
    }
    // The drive's oversampler delay, the same on every layer (each sounding layer runs through it)
    float driveLatencySamples() const noexcept { return layerDrive.getLatencySamples(); }

    // ---- Sub/body phase alignment (PhaseAlign.h), driven by the stack ----
    // The stack's running sample count at the next sample this layer renders
    void setClock (int64_t c) noexcept { clock = c; }
    void setProbing (bool on, double crossoverHz) noexcept
    {
        if (on && ! probing)
            probe.reset();
        probing = on;
        if (on)
            probe.setCrossover (crossoverHz);
    }
    const align::Probe& getProbe() const noexcept { return probe; }
    float tuneSemitones() const noexcept { return tune.load(); }
    double crossoverLow() const noexcept { return load (params.crossoverLowHz, 120.0f); }
    // 0..1; the correction applies to this layer (the Sub) from its next start from silence
    void setAlignStrength (float s) noexcept { alignStrength = s; }
    void clearAlignment() noexcept
    {
        alignCache.fill ({});
        freshNote = -1;
    }
    // The sub note that last started from silence and when, until it has been measured
    bool pendingMeasurement (int& note, int64_t& startClock) const noexcept
    {
        note = freshNote;
        startClock = freshClock;
        return freshNote >= 0 && ! freshMeasured && anyVoiceSounding();
    }
    void storeAlignment (int note, const align::Correction& c) noexcept
    {
        alignCache[(size_t) juce::jlimit (0, 127, note)] = c;
        freshMeasured = true;
    }
    const align::Correction& alignmentFor (int note) const noexcept { return alignCache[(size_t) juce::jlimit (0, 127, note)]; }

    // Changes whenever something that moves this layer's phase changes (engine, tune, slots, filter, drive,
    // role, crossovers, the knobs' slot offsets); level, pan and envelopes leave it alone
    double soundSignature() const noexcept
    {
        double h = (double) (engineId.hashCode() & 0xffff);
        int k = 1;
        auto mix = [&] (const std::atomic<float>* p) { h += (p != nullptr ? (double) p->load() : 0.0) * (1.0 + 0.618 * k++); };
        for (auto* p : { params.octave, params.semi, params.fineCents, params.role, params.crossoverLowHz, params.crossoverHighHz,
                         params.filterType, params.cutoffHz, params.resonance, params.filterEnvSemis, params.keyTrack, params.drive })
            mix (p);
        for (auto* p : params.slots)
            mix (p);
        for (float v : chainSlots)
            h += (double) v * (1.0 + 0.618 * k++);
        return h;
    }

    params::Role role() const noexcept
    {
        return (params::Role) juce::jlimit (0, 4, juce::roundToInt (load (params.role, (float) params::Role::full)));
    }
    void setSlotParameter (int slot, std::atomic<float>* p) { params.slots[(size_t) slot] = p; }

    void reset()
    {
        for (auto& e : envelopes)
            e.reset();
        for (auto& f : voiceFilters)
            f.reset();
        if (active != nullptr)
            active->reset();
        clearMonoNotes();
    }

    // ---- Audio thread ----------------------------------------------------------------------------
    void beginBlock()
    {
        meter.update();
        if (carving)
            carvedMeter.update();
        // Swap only when the previous engine has finished fading and there is room to retire this one
        if (outgoing == nullptr && freeRetireSlots() >= 2)
        {
            if (auto* next = pending.exchange (nullptr))
            {
                if (active != nullptr)
                {
                    // The old engine fades out on its own copy of the envelopes; the new one starts silent
                    for (int v = 0; v < numVoices; ++v)
                    {
                        outgoingEnvelopes[(size_t) v] = envelopes[(size_t) v];
                        outgoingEnvelopes[(size_t) v].steal();
                        envelopes[(size_t) v].reset();
                        outgoingFilters[(size_t) v] = voiceFilters[(size_t) v];
                        voiceFilters[(size_t) v].reset();
                    }
                    outgoing = active;
                }
                clearMonoNotes();
                active = isEmpty (next) ? (retireNow (next), nullptr) : next;
                slotsSent = false;
            }
        }

        level.setTargetValue (targetGain());
        updateControls (false);
        updateVoiceMode();
        updateBand (false);
        updateEnvelopeSettings();
        updateFilterType();
        if (active != nullptr)
        {
            active->setTune (tune.load() + 0.01f * chainDriftCents);
            updateSlots();
        }

        // Once the level has faded fully down, drop the voices (silent at zero gain) so they free their slots
        if (! isMuted())
            voicesClearedWhileMuted = false;
        else if (! voicesClearedWhileMuted)
        {
            reset();
            voicesClearedWhileMuted = true;
        }
    }

    // A layer that is empty, or muted with its level fully down, is skipped entirely.
    bool isMuted() const noexcept { return level.getTargetValue() <= 0.0f && ! level.isSmoothing(); }
    bool canPlay() const noexcept { return active != nullptr && level.getTargetValue() > 0.0f; }
    bool isIdle() const noexcept  { return (active == nullptr || isMuted()) && outgoing == nullptr; }

    Engine* engine() const noexcept { return active; }

    // `slot` is the stack's note slot; in Poly it is also the voice.
    void startVoice (int slot, int note, float velocity, uint32_t order)
    {
        if (! canPlay())
            return;

        duck.trigger();
        if (! anyVoiceSounding())
            startFromSilence (note);
        if (mode == params::VoiceMode::poly)
        {
            enforceVoiceCap (slot);
            startOrder[(size_t) slot] = order;
            active->noteOn (slot, { note, velocity, glideSeconds(), lastNote, false, fixedPhaseRole() });
            voiceFilters[(size_t) slot].start (note, envelopes[(size_t) slot].isActive());
            envelopes[(size_t) slot].trigger();
        }
        else
        {
            removeMonoNote (slot);
            const bool keyAlreadyHeld = numMonoNotes > 0;
            if (numMonoNotes == (int) monoNotes.size())
                removeMonoNote (monoNotes[0].slot);   // more held keys than slots: forget the oldest
            monoNotes[(size_t) numMonoNotes++] = { slot, note, velocity };
            playMono (monoNotes[(size_t) numMonoNotes - 1], keyAlreadyHeld);
        }
        lastNote = (float) note;
    }

    void releaseVoice (int slot)
    {
        if (active == nullptr)
            return;

        if (mode == params::VoiceMode::poly)
        {
            auto& env = envelopes[(size_t) slot];
            if (env.isActive() && ! env.isStealing())
            {
                env.release();
                voiceFilters[(size_t) slot].env.release();
                active->noteOff (slot);
            }
            return;
        }

        if (! removeMonoNote (slot) || slot != monoOwner)
            return;
        if (numMonoNotes > 0)
        {
            const auto& previous = monoNotes[(size_t) numMonoNotes - 1];
            playMono (previous, true);
            lastNote = (float) previous.note;
        }
        else if (envelopes[0].isActive() && ! envelopes[0].isStealing())
        {
            envelopes[0].release();
            voiceFilters[0].env.release();
            active->noteOff (0);
        }
    }

    bool isVoiceActive (int slot) const
    {
        if (active == nullptr || isMuted())
            return false;
        if (mode == params::VoiceMode::poly)
            return envelopes[(size_t) slot].isActive();
        return slot == monoOwner && envelopes[0].isActive();
    }

    // Adds this layer's output into outL/outR. numSamples must not exceed the prepared block size.
    void render (float* outL, float* outR, int numSamples)
    {
        if (isIdle())
        {
            meter.addSilence (numSamples);
            if (carving)
                carvedMeter.addSilence (numSamples);
            return;
        }

        std::fill_n (scratchL.data(), numSamples, 0.0f);
        std::fill_n (scratchR.data(), numSamples, 0.0f);

        if (active != nullptr && ! isMuted())
            renderVoices (*active, envelopes, voiceFilters, numSamples);

        if (outgoing != nullptr && ! renderVoices (*outgoing, outgoingEnvelopes, outgoingFilters, numSamples))
            retireNow (std::exchange (outgoing, nullptr));
        if (filterFade > 0)
            filterFade = std::max (0, filterFade - numSamples);

        layerDrive.process (scratchL.data(), scratchR.data(), numSamples, juce::jlimit (0.0f, 1.0f, load (params.drive, 0.0f) * 0.01f + chainDrive));

        band.process (scratchL.data(), scratchR.data(), numSamples);
        chorus.process (scratchL.data(), scratchR.data(), numSamples, juce::jlimit (0.0f, 1.0f, load (params.chorus, 0.0f) * 0.01f + chainChorus),
                        load (params.chorusRateHz, 0.4f), load (params.chorusDepth, 50.0f) * 0.01f);
        meter.process (scratchL.data(), scratchR.data(), numSamples);
        const bool carved = carve.process (scratchL.data(), scratchR.data(), numSamples, carveTargets, carveAnalyse);
        if (carved != carving)
        {
            carvedMeter.reset();
            carvedMeter.seed (meter.getPower());
            carving = carved;
        }
        if (carving)
            carvedMeter.process (scratchL.data(), scratchR.data(), numSamples);
        if (probing)
            probe.process (scratchL.data(), scratchR.data(), numSamples, clock);
        alignDelay.process (scratchL.data(), scratchR.data(), numSamples);

        clock += numSamples;
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = level.getNextValue() * duck.next();
            outL[i] += scratchL[(size_t) i] * g * panL.getNextValue();
            outR[i] += scratchR[(size_t) i] * g * panR.getNextValue();
        }
    }

private:
    // A dip that falls over 2 ms (from wherever it is, so a retrigger never steps) and recovers along a half
    // cosine over the window. Gain = 1 - depth * shape.
    struct OnsetDuck
    {
        float depth = 0.0f, releaseSamples = 1440.0f, attackSamples = 96.0f;
        float shape = 0.0f, from = 0.0f, t = 0.0f;
        bool attacking = false, releasing = false;

        void trigger() noexcept
        {
            if (depth <= 0.0f && shape <= 0.0f)
                return;
            from = shape;
            t = 0.0f;
            attacking = true;
            releasing = false;
        }

        float next() noexcept
        {
            if (attacking)
            {
                t += 1.0f;
                const float x = std::min (1.0f, t / attackSamples);
                shape = from + (1.0f - from) * 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * x));
                if (x >= 1.0f)
                {
                    attacking = false;
                    releasing = true;
                    t = 0.0f;
                }
            }
            else if (releasing)
            {
                t += 1.0f;
                const float x = std::min (1.0f, t / releaseSamples);
                shape = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * x));
                if (x >= 1.0f)
                {
                    releasing = false;
                    shape = 0.0f;
                }
            }
            return 1.0f - depth * shape;
        }
    };

    bool fixedPhaseRole() const noexcept { const auto r = role(); return r == params::Role::sub || r == params::Role::body; }

    bool anyVoiceSounding() const noexcept
    {
        for (const auto& e : envelopes)
            if (e.isActive())
                return true;
        return false;
    }

    // Silent, so the alignment can switch at once: apply what was learned for this note (nothing if unknown)
    void startFromSilence (int note) noexcept
    {
        const auto& c = alignmentFor (note);
        const bool use = c.valid && alignStrength > 0.0f;
        const float sign = use && c.sign < 0.0f && alignStrength >= 0.5f ? -1.0f : 1.0f;
        const float delay = use ? c.delaySamples * alignStrength : 0.0f;
        if (sign > 0.0f && delay <= 0.0f)
        {
            if (! alignDelay.isNeutral())
                alignDelay.set (1.0f, 0.0f);
        }
        else
            alignDelay.set (sign, delay);
        freshNote = note;
        freshClock = clock;
        freshMeasured = false;
    }

    // Placeholder posted when a layer is cleared; never becomes the active engine.
    struct Empty final : Engine
    {
        const EngineInfo& info() const override { static const EngineInfo i { "", "Empty", 0, {} }; return i; }
        void prepare (double, int, int) override {}
        void reset() override {}
        void noteOn (int, const NoteEvent&) override {}
        void noteOff (int) override {}
        void endVoice (int) override {}
        void setTune (float) override {}
        void render (int, float*, float*, int) override {}
    };

    static bool isEmpty (Engine* e) { return e->info().id.isEmpty(); }

    // If the message thread has not emptied the queue yet, the engine stays `outgoing` (silent) and
    // retiring is retried on the next block.
    void retireNow (Engine* e)
    {
        for (auto& r : retired)
        {
            Engine* expected = nullptr;
            if (r.compare_exchange_strong (expected, e))
                return;
        }
        outgoing = e;
    }

    int freeRetireSlots() const
    {
        int n = 0;
        for (auto& r : retired)
            n += r.load() == nullptr ? 1 : 0;
        return n;
    }

    // One voice's filter: its own envelope, a ladder and an SVF per channel (each runs only while selected)
    struct VoiceFilter
    {
        AmpEnvelope env;
        std::array<ZdfLadder, 2> ladder {};
        std::array<ZdfSvf, 2> svf {};
        float note = 60.0f;

        // A fresh voice starts from clean filter states; a retrigger keeps them, so it glides without a click
        void start (int midiNote, bool sounding) noexcept
        {
            if (! sounding)
                reset();
            note = (float) midiNote;
            env.trigger();
        }

        void reset() noexcept
        {
            env.reset();
            for (auto& l : ladder) l.reset();
            for (auto& f : svf) f.reset();
        }
    };

    params::FilterType currentFilterType() const noexcept
    {
        return (params::FilterType) juce::jlimit (0, 4, juce::roundToInt (load (params.filterType, 0.0f)));
    }

    static bool isSvf (params::FilterType t) noexcept { return t != params::FilterType::off && t != params::FilterType::ladder24; }

    // A type change crossfades over 5 ms from the old response; the incoming filter starts from clean states
    void updateFilterType() noexcept
    {
        const auto t = currentFilterType();
        if (t == filterType)
            return;
        const bool ladderIn = t == params::FilterType::ladder24 && filterType != params::FilterType::ladder24;
        const bool svfIn = isSvf (t) && ! isSvf (filterType);
        for (auto* bank : { &voiceFilters, &outgoingFilters })
            for (auto& f : *bank)
            {
                if (ladderIn) for (auto& l : f.ladder) l.reset();
                if (svfIn)    for (auto& s : f.svf) s.reset();
            }
        previousFilterType = filterType;
        filterType = t;
        filterFade = filterFadeSamples();
    }

    int filterFadeSamples() const noexcept { return std::max (1, (int) (0.005 * sampleRate.load())); }

    struct FilterOutputs { float dry, ladder; ZdfSvf::Outputs svf; float bandGain; };

    static float select (params::FilterType t, const FilterOutputs& o) noexcept
    {
        switch (t)
        {
            case params::FilterType::off:       return o.dry;
            case params::FilterType::ladder24:  return o.ladder;
            case params::FilterType::lowPass12: return o.svf.low;
            case params::FilterType::bandPass:  return o.svf.band * o.bandGain;   // unity at the centre
            case params::FilterType::highPass:  return o.svf.high;
        }
        return o.dry;
    }

    // Filters one voice in place. The cutoff (patch + key track + envelope) is computed every 4 samples and
    // the TPT gain interpolated between, so envelope sweeps stay smooth at a quarter of the cost.
    void filterVoice (VoiceFilter& f, int numSamples) noexcept
    {
        const bool fading = filterFade > 0;
        if (filterType == params::FilterType::off && ! (fading && previousFilterType != params::FilterType::off))
        {
            for (int i = 0; i < numSamples; ++i)
                f.env.next (filterAdsr);   // keep the envelope running so switching the filter on mid-note is in step
            return;
        }
        const float sr = (float) sampleRate.load();
        const float baseSemis = 12.0f * std::log2 (juce::jlimit (20.0f, 20000.0f, load (params.cutoffHz, 20000.0f)) / 440.0f)
                              + load (params.keyTrack, 50.0f) * 0.01f * (f.note - 60.0f);
        const float envSemis = load (params.filterEnvSemis, 0.0f);
        const float r = juce::jlimit (0.0f, 1.0f, load (params.resonance, 10.0f) * 0.01f);
        const float ladderK = 3.9f * r, svfK = juce::MathConstants<float>::sqrt2 * (1.0f - 0.97f * r);
        auto gainFor = [&] (float envLevel)
        {
            const float semis = juce::jlimit (-60.0f, 60.0f, baseSemis + envLevel * envSemis);
            return tptGain (440.0f * fastExp2 (semis * (1.0f / 12.0f)), sr);
        };

        int fadeLeft = filterFade;
        const auto prev = fading ? previousFilterType : filterType;
        const bool runLadder = filterType == params::FilterType::ladder24 || prev == params::FilterType::ladder24;
        const bool runSvf = isSvf (filterType) || isSvf (prev);
        const float fadeStep = 1.0f / (float) filterFadeSamples();
        float g0 = gainFor (f.env.getLevel());
        for (int start = 0; start < numSamples; start += 4)
        {
            const int n = std::min (4, numSamples - start);
            float envLevel = 0.0f;
            for (int j = 0; j < n; ++j)
                envLevel = f.env.next (filterAdsr);
            const float g1 = gainFor (envLevel);
            for (int j = 0; j < n; ++j)
            {
                const float g = g0 + (g1 - g0) * (float) (j + 1) / (float) n;
                const auto lc = ZdfLadder::Coeffs::make (g, ladderK);
                const auto sc = ZdfSvf::Coeffs::make (g, svfK);
                const int i = start + j;
                float y[2] = { voiceL[(size_t) i], voiceR[(size_t) i] };
                const float w = fadeLeft > 0 ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (1.0f - (float) fadeLeft * fadeStep)) : 1.0f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    FilterOutputs o { y[ch], 0.0f, { 0.0f, 0.0f, 0.0f }, sc.k };
                    if (runLadder)
                        // Half level into the soft-clipped loop for headroom; part of the resonance's passband loss made up
                        o.ladder = 2.0f * f.ladder[(size_t) ch].process (0.5f * y[ch] * (1.0f + 0.5f * ladderK), lc);
                    if (runSvf)
                        o.svf = f.svf[(size_t) ch].process (y[ch], sc);
                    const float now = select (filterType, o);
                    y[ch] = fadeLeft > 0 ? select (previousFilterType, o) + w * (now - select (previousFilterType, o)) : now;
                }
                if (fadeLeft > 0)
                    --fadeLeft;
                const float yl = y[0], yr = y[1];
                voiceL[(size_t) i] = yl;
                voiceR[(size_t) i] = yr;
            }
            g0 = g1;
        }
    }

    // Renders every sounding voice through its filter and envelope into the scratch sum. Returns false once all are silent.
    bool renderVoices (Engine& e, std::vector<AmpEnvelope>& envs, std::vector<VoiceFilter>& filters, int numSamples)
    {
        e.beginBlock (numSamples);
        bool anySounding = false;
        for (int v = 0; v < numVoices; ++v)
        {
            auto& env = envs[(size_t) v];
            if (! env.isActive())
                continue;

            std::fill_n (voiceL.data(), numSamples, 0.0f);
            std::fill_n (voiceR.data(), numSamples, 0.0f);
            e.render (v, voiceL.data(), voiceR.data(), numSamples);
            filterVoice (filters[(size_t) v], numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                const float g = env.next (adsr);
                scratchL[(size_t) i] += voiceL[(size_t) i] * g;
                scratchR[(size_t) i] += voiceR[(size_t) i] * g;
            }

            if (env.isActive())
                anySounding = true;
            else
                e.endVoice (v);
        }
        return anySounding;
    }

    struct MonoNote { int slot = -1, note = 60; float velocity = 1.0f; };

    void playMono (const MonoNote& n, bool keyAlreadyHeld)
    {
        auto& env = envelopes[0];
        const bool sounding = env.isActive() && ! env.isStealing();
        const bool legatoMove = mode == params::VoiceMode::legato && keyAlreadyHeld && sounding;
        const bool glide = mode == params::VoiceMode::mono || legatoMove;

        active->noteOn (0, { n.note, n.velocity, glide ? glideSeconds() : 0.0, lastNote, legatoMove, fixedPhaseRole() });
        if (! legatoMove)
        {
            voiceFilters[0].start (n.note, env.isActive());
            env.trigger();
        }
        else
            voiceFilters[0].note = (float) n.note;
        monoOwner = n.slot;
    }

    bool removeMonoNote (int slot)
    {
        for (int i = 0; i < numMonoNotes; ++i)
            if (monoNotes[(size_t) i].slot == slot)
            {
                std::move (monoNotes.begin() + i + 1, monoNotes.begin() + numMonoNotes, monoNotes.begin() + i);
                --numMonoNotes;
                return true;
            }
        return false;
    }

    void clearMonoNotes()
    {
        numMonoNotes = 0;
        monoOwner = -1;
    }

    // A mode change fades out whatever is sounding, so voices never hang between the two note layouts
    void updateVoiceMode()
    {
        const auto newMode = (params::VoiceMode) juce::jlimit (0, 2, juce::roundToInt (load (params.voiceMode, 0.0f)));
        if (newMode == mode)
            return;
        mode = newMode;
        for (auto& env : envelopes)
            env.steal();
        clearMonoNotes();
    }

    void updateBand (bool snap)
    {
        const double low = load (params.crossoverLowHz, 120.0f), high = load (params.crossoverHighHz, 2500.0f);
        double hp = 0.0, lp = 0.0;
        switch ((params::Role) juce::jlimit (0, 4, juce::roundToInt (load (params.role, (float) params::Role::full))))
        {
            case params::Role::sub:  lp = low; break;
            case params::Role::body: hp = low; lp = high; break;
            case params::Role::mid:  hp = 2.0 * low; break;
            case params::Role::air:  hp = high; break;
            case params::Role::full: break;
        }
        band.setBand (hp, lp, snap);
    }

    double glideSeconds() const { return load (params.glideMs, 0.0f) * 0.001; }

    static float load (const std::atomic<float>* p, float fallback) { return p != nullptr ? p->load() : fallback; }

    float targetPatchGain() const
    {
        const float db = load (params.levelDb, 0.0f);
        return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
    }

    // A muted layer stays muted whatever the knobs do
    float targetGain() const { return targetPatchGain() * juce::Decibels::decibelsToGain (chainLevelDb); }

    // Pan uses a balance law: the centre is unity and a side never boosts, so stereo engines keep their image.
    void updateControls (bool snap)
    {
        const float p = juce::jlimit (-1.0f, 1.0f, load (params.pan, 0.0f));
        const float halfPi = juce::MathConstants<float>::halfPi;
        const float l = p > 0.0f ? std::cos (p * halfPi) : 1.0f;
        const float r = p < 0.0f ? std::cos (-p * halfPi) : 1.0f;
        if (snap) { panL.setCurrentAndTargetValue (l); panR.setCurrentAndTargetValue (r); }
        else      { panL.setTargetValue (l); panR.setTargetValue (r); }

        tune = load (params.octave, 0.0f) * 12.0f + load (params.semi, 0.0f) + load (params.fineCents, 0.0f) * 0.01f;
    }

    void updateEnvelopeSettings()
    {
        adsr.set (sampleRate.load(), load (params.attackMs, 2.0f), load (params.decayMs, 400.0f),
                  load (params.sustainPercent, 100.0f) * 0.01f, load (params.releaseMs, 300.0f));
        filterAdsr.set (sampleRate.load(), load (params.filterAttackMs, 2.0f), load (params.filterDecayMs, 600.0f),
                        load (params.filterSustainPercent, 0.0f) * 0.01f, load (params.filterReleaseMs, 300.0f));
    }

    // Sends slot values that moved since the last block (all of them after an engine swap): the patch's
    // value plus the chain knobs' offset for the slot's role
    void updateSlots()
    {
        const auto& infoSlots = active->info().slots;
        for (size_t k = 0; k < lastSlot.size(); ++k)
        {
            if (params.slots[k] == nullptr)
                continue;
            float v = params.slots[k]->load();
            if (k < infoSlots.size() && infoSlots[k].role != SlotRole::none)
                v = juce::jlimit (0.0f, 1.0f, v + chainSlots[(size_t) infoSlots[k].role]);
            if (! slotsSent || v < lastSlot[k] || v > lastSlot[k])
            {
                active->setSlot ((int) k, v);
                lastSlot[k] = v;
            }
        }
        slotsSent = true;
    }

    // When the engine's voice budget is full, the oldest sounding voice fades out to make room.
    void enforceVoiceCap (int newVoice)
    {
        const int cap = active->info().maxVoices;
        int count = 0, oldest = -1;
        for (int v = 0; v < numVoices; ++v)
        {
            const auto& env = envelopes[(size_t) v];
            if (v == newVoice || ! env.isActive() || env.isStealing())
                continue;
            ++count;
            if (oldest < 0 || startOrder[(size_t) v] < startOrder[(size_t) oldest])
                oldest = v;
        }
        if (count >= cap && oldest >= 0)
            envelopes[(size_t) oldest].steal();
    }

    Engine* active = nullptr;     // audio thread only
    Engine* outgoing = nullptr;   // audio thread only: previous engine fading out
    std::atomic<Engine*> pending { nullptr };
    std::array<std::atomic<Engine*>, 4> retired {};
    juce::String engineId;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> blockSize { 512 };
    int numVoices = 16;

    LayerParams params;
    std::array<float, Engine::numSlots> lastSlot {};   // audio thread only
    float chainLevelDb = 0.0f;
    std::array<float, numSlotRoles> chainSlots {};
    OnsetDuck duck;
    float chainDriftCents = 0.0f, chainDrive = 0.0f, chainChorus = 0.0f;
    harmony::CarveBank carve;
    harmony::KPowerMeter carvedMeter;
    std::array<float, harmony::CarveBank::numBands> carveTargets { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    bool carving = false, carveAnalyse = false;
    float duckRate = 48000.0f;
    harmony::KPowerMeter meter;
    bool slotsSent = false;
    bool voicesClearedWhileMuted = false;
    juce::SmoothedValue<float> level { 1.0f }, panL { 1.0f }, panR { 1.0f };
    std::atomic<float> tune { 0.0f };   // semitones; written by the audio thread, read when a new engine is prepared
    AdsrSettings adsr, filterAdsr;
    RoleBandLimit band;
    std::vector<AmpEnvelope> envelopes, outgoingEnvelopes;
    std::vector<VoiceFilter> voiceFilters, outgoingFilters;
    params::FilterType filterType = params::FilterType::off, previousFilterType = params::FilterType::off;
    int filterFade = 0;
    LayerDrive layerDrive;
    Chorus chorus;
    align::Probe probe;
    align::Delay alignDelay;
    std::array<align::Correction, 128> alignCache {};
    bool probing = false, freshMeasured = true;
    int freshNote = -1;
    int64_t clock = 0, freshClock = 0;
    float alignStrength = 1.0f;
    std::vector<float> scratchL, scratchR, voiceL, voiceR;
    std::vector<uint32_t> startOrder;

    params::VoiceMode mode = params::VoiceMode::poly;
    std::array<MonoNote, 16> monoNotes {};
    int numMonoNotes = 0, monoOwner = -1;
    float lastNote = -1.0f;   // MIDI note last played, where poly and mono glides start from
};
}
