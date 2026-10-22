#pragma once
#include "Engine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

namespace ps
{
// One slot of the stack: an engine plus its per-layer controls.
//
// Engine handover is lock-free. The message thread prepares a new engine and posts it to `pending`.
// The audio thread picks it up at the start of a block, keeps the old engine rendering while its voices
// fade out, then hands it to a small `retired` queue, which the message thread empties. Nothing is allocated or
// freed on the audio thread.
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
        startOrder.assign ((size_t) voices, 0);
        level.reset (sr, 0.02);
        level.setCurrentAndTargetValue (targetGain());

        for (auto* e : { active, outgoing })
            if (e != nullptr)
                e->prepare (sr, maxBlock, voices);

        if (auto* p = pending.exchange (nullptr))
        {
            p->prepare (sr, maxBlock, voices);
            delete pending.exchange (p);
        }
    }

    void setLevelParameter (std::atomic<float>* levelDb) { levelParam = levelDb; }

    // ---- Audio thread ----------------------------------------------------------------------------
    void beginBlock()
    {
        // Swap only when the previous engine has finished fading and there is room to retire this one
        if (outgoing == nullptr && freeRetireSlots() >= 2)
        {
            if (auto* next = pending.exchange (nullptr))
            {
                if (active != nullptr)
                {
                    for (int v = 0; v < numVoices; ++v)
                        if (active->isActive (v))
                            active->steal (v);
                    outgoing = active;
                }
                active = isEmpty (next) ? (retireNow (next), nullptr) : next;
            }
        }

        level.setTargetValue (targetGain());

        // Once the level has faded fully down, drop the voices (silent at zero gain) so they free their slots
        if (! isMuted())
            voicesClearedWhileMuted = false;
        else if (! voicesClearedWhileMuted && active != nullptr)
        {
            active->reset();
            voicesClearedWhileMuted = true;
        }
    }

    // A layer that is empty, or muted with its level fully down, is skipped entirely.
    bool isMuted() const noexcept { return level.getTargetValue() <= 0.0f && ! level.isSmoothing(); }
    bool canPlay() const noexcept { return active != nullptr && level.getTargetValue() > 0.0f; }
    bool isIdle() const noexcept  { return (active == nullptr || isMuted()) && outgoing == nullptr; }

    Engine* engine() const noexcept { return active; }

    void startVoice (int voice, int note, float velocity, uint32_t order)
    {
        if (! canPlay())
            return;

        enforceVoiceCap (voice);
        startOrder[(size_t) voice] = order;
        active->noteOn (voice, note, velocity);
    }

    void releaseVoice (int voice)
    {
        if (active != nullptr && active->isActive (voice))
            active->noteOff (voice);
    }

    bool isVoiceActive (int voice) const
    {
        return active != nullptr && ! isMuted() && active->isActive (voice);
    }

    // Adds this layer's output into outL/outR. numSamples must not exceed the prepared block size.
    void render (float* outL, float* outR, int numSamples)
    {
        if (isIdle())
            return;

        std::fill_n (scratchL.data(), numSamples, 0.0f);
        std::fill_n (scratchR.data(), numSamples, 0.0f);

        if (active != nullptr && ! isMuted())
            for (int v = 0; v < numVoices; ++v)
                if (active->isActive (v))
                    active->render (v, scratchL.data(), scratchR.data(), numSamples);

        if (outgoing != nullptr)
        {
            bool stillSounding = false;
            for (int v = 0; v < numVoices; ++v)
                if (outgoing->isActive (v))
                {
                    outgoing->render (v, scratchL.data(), scratchR.data(), numSamples);
                    stillSounding = true;
                }

            if (! stillSounding)
                retireNow (std::exchange (outgoing, nullptr));
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = level.getNextValue();
            outL[i] += scratchL[(size_t) i] * g;
            outR[i] += scratchR[(size_t) i] * g;
        }
    }

private:
    // Placeholder posted when a layer is cleared; never becomes the active engine.
    struct Empty final : Engine
    {
        const EngineInfo& info() const override { static const EngineInfo i { "", "Empty", 0, {} }; return i; }
        void prepare (double, int, int) override {}
        void reset() override {}
        void noteOn (int, int, float) override {}
        void noteOff (int) override {}
        void steal (int) override {}
        bool isActive (int) const override { return false; }
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

    float targetGain() const
    {
        if (levelParam == nullptr)
            return 1.0f;
        const float db = levelParam->load();
        return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
    }

    // When the engine's voice budget is full, the oldest sounding voice fades out to make room.
    void enforceVoiceCap (int newVoice)
    {
        const int cap = active->info().maxVoices;
        int count = 0, oldest = -1;
        for (int v = 0; v < numVoices; ++v)
        {
            if (v == newVoice || ! active->isActive (v))
                continue;
            ++count;
            if (oldest < 0 || startOrder[(size_t) v] < startOrder[(size_t) oldest])
                oldest = v;
        }
        if (count >= cap && oldest >= 0)
            active->steal (oldest);
    }

    Engine* active = nullptr;     // audio thread only
    Engine* outgoing = nullptr;   // audio thread only: previous engine fading out
    std::atomic<Engine*> pending { nullptr };
    std::array<std::atomic<Engine*>, 4> retired {};
    juce::String engineId;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> blockSize { 512 };
    int numVoices = 16;

    std::atomic<float>* levelParam = nullptr;
    bool voicesClearedWhileMuted = false;
    juce::SmoothedValue<float> level { 1.0f };
    std::vector<float> scratchL, scratchR;
    std::vector<uint32_t> startOrder;
};
}
