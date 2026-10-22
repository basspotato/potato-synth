#pragma once
#include "Engine.h"
#include "Envelope.h"
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
        level.reset (sr, 0.02);
        level.setCurrentAndTargetValue (targetGain());
        for (auto* g : { &panL, &panR })
            g->reset (sr, 0.02);
        updateControls (true);
        updateEnvelopeSettings();
        band.prepare (sr);
        updateBand (true);
        meter.prepare (sr);

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

    // The patch's own level and the chain knobs' extra gain, for the Harmony Engine's loudness model
    float patchGain() const noexcept { return targetPatchGain(); }
    float knobGain() const noexcept  { return juce::Decibels::decibelsToGain (chainLevelDb); }
    // K-weighted power of this layer at unity gain (after its band limit), averaged over 400 ms
    float measuredPower() const noexcept { return meter.getPower(); }
    params::Role role() const noexcept
    {
        return (params::Role) juce::jlimit (0, 4, juce::roundToInt (load (params.role, (float) params::Role::full)));
    }
    void setSlotParameter (int slot, std::atomic<float>* p) { params.slots[(size_t) slot] = p; }

    void reset()
    {
        for (auto& e : envelopes)
            e.reset();
        if (active != nullptr)
            active->reset();
        clearMonoNotes();
    }

    // ---- Audio thread ----------------------------------------------------------------------------
    void beginBlock()
    {
        meter.update();
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
        if (active != nullptr)
        {
            active->setTune (tune.load());
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

        if (mode == params::VoiceMode::poly)
        {
            enforceVoiceCap (slot);
            startOrder[(size_t) slot] = order;
            active->noteOn (slot, { note, velocity, glideSeconds(), lastNote, false });
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
            return;
        }

        std::fill_n (scratchL.data(), numSamples, 0.0f);
        std::fill_n (scratchR.data(), numSamples, 0.0f);

        if (active != nullptr && ! isMuted())
            renderVoices (*active, envelopes, numSamples);

        if (outgoing != nullptr && ! renderVoices (*outgoing, outgoingEnvelopes, numSamples))
            retireNow (std::exchange (outgoing, nullptr));

        band.process (scratchL.data(), scratchR.data(), numSamples);
        meter.process (scratchL.data(), scratchR.data(), numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = level.getNextValue();
            outL[i] += scratchL[(size_t) i] * g * panL.getNextValue();
            outR[i] += scratchR[(size_t) i] * g * panR.getNextValue();
        }
    }

private:
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

    // Renders every sounding voice through its envelope into the scratch sum. Returns false once all are silent.
    bool renderVoices (Engine& e, std::vector<AmpEnvelope>& envs, int numSamples)
    {
        bool anySounding = false;
        for (int v = 0; v < numVoices; ++v)
        {
            auto& env = envs[(size_t) v];
            if (! env.isActive())
                continue;

            std::fill_n (voiceL.data(), numSamples, 0.0f);
            std::fill_n (voiceR.data(), numSamples, 0.0f);
            e.render (v, voiceL.data(), voiceR.data(), numSamples);
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

        active->noteOn (0, { n.note, n.velocity, glide ? glideSeconds() : 0.0, lastNote, legatoMove });
        if (! legatoMove)
            env.trigger();
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
    harmony::KPowerMeter meter;
    bool slotsSent = false;
    bool voicesClearedWhileMuted = false;
    juce::SmoothedValue<float> level { 1.0f }, panL { 1.0f }, panR { 1.0f };
    std::atomic<float> tune { 0.0f };   // semitones; written by the audio thread, read when a new engine is prepared
    AdsrSettings adsr;
    RoleBandLimit band;
    std::vector<AmpEnvelope> envelopes, outgoingEnvelopes;
    std::vector<float> scratchL, scratchR, voiceL, voiceR;
    std::vector<uint32_t> startOrder;

    params::VoiceMode mode = params::VoiceMode::poly;
    std::array<MonoNote, 16> monoNotes {};
    int numMonoNotes = 0, monoOwner = -1;
    float lastNote = -1.0f;   // MIDI note last played, where poly and mono glides start from
};
}
