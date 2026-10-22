#pragma once
#include <juce_core/juce_core.h>

namespace ps
{
// One generic per-layer parameter slot as an engine describes it. Each layer has a fixed set of
// host parameters; every engine maps its own controls onto these slots, so adding an engine never
// adds, removes or renames host parameters.
struct SlotInfo
{
    juce::String name;
    float defaultValue = 0.0f;   // normalised 0..1
};

struct EngineInfo
{
    juce::String id;             // permanent: saved in state
    juce::String name;           // shown in the GUI
    int maxVoices = 16;          // per-layer voice budget for this engine (heavier engines get fewer)
    std::vector<SlotInfo> slots; // at most Engine::numSlots
};

// A synth engine owns a fixed bank of voices, indexed by the stack's note slots. Engines are created
// and destroyed on the message thread; every method below except info() is called on the audio thread
// and must not allocate, lock or block.
class Engine
{
public:
    static constexpr int numSlots = 16;

    virtual ~Engine() = default;

    virtual const EngineInfo& info() const = 0;

    // Allocate everything here (message thread or prepareToPlay).
    virtual void prepare (double sampleRate, int maxBlockSize, int numVoices) = 0;
    virtual void reset() = 0;

    // Starting a voice that is still sounding must be click-free (the engine glides from its current state).
    virtual void noteOn (int voice, int midiNote, float velocity) = 0;
    virtual void noteOff (int voice) = 0;
    // Fade the voice out within a few milliseconds and go idle (voice-cap stealing).
    virtual void steal (int voice) = 0;
    virtual bool isActive (int voice) const = 0;

    virtual void setSlot (int slot, float normalisedValue) { juce::ignoreUnused (slot, normalisedValue); }

    // Add one voice's output into left/right for numSamples.
    virtual void render (int voice, float* left, float* right, int numSamples) = 0;
};
}
