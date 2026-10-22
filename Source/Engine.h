#pragma once
#include <juce_core/juce_core.h>
#include <functional>

namespace ps
{
// One generic per-layer parameter slot as an engine describes it. Each layer has a fixed set of
// host parameters; every engine maps its own controls onto these slots, so adding an engine never
// adds, removes or renames host parameters.
// What a slot does, for controls that act on every engine at once (the chain knobs). A chain knob moves
// "detune" or "drive" on whichever engines have it, in the slot's own normalised units.
enum class SlotRole
{
    none,
    detune,        // unison detune amount
    unisonMix,     // level of the side voices
    stereoSpread,  // unison pan spread
    drive,         // saturation amount
    even,          // saturation asymmetry (even harmonics)
    translate,     // sub harmonics for small speakers
    subOctave,     // -1 octave reinforcement
    brightness,    // main filter cutoff or tone
    position,      // wavetable position
    warp,          // phase warp (bipolar, 0.5 = none)
    positionSpread // wavetable position spread across unison
};
inline constexpr int numSlotRoles = (int) SlotRole::positionSpread + 1;

struct SlotInfo
{
    juce::String name;
    float defaultValue = 0.0f;                       // normalised 0..1
    std::function<juce::String (float)> toText;      // normalised value to display text; percent if empty
    SlotRole role = SlotRole::none;
};

// A note starting (or moving) on a voice.
struct NoteEvent
{
    int note = 60;
    float velocity = 1.0f;
    // Glide: when > 0 the voice glides to `note` over this time, from its current pitch if it is sounding,
    // otherwise from `glideFrom` (a MIDI note; negative = no glide for an idle voice).
    double glideSeconds = 0.0;
    float glideFrom = -1.0f;
    // Legato: the voice keeps its velocity, phase and any internal envelopes; only the pitch moves.
    bool legato = false;
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
//
// The layer owns each voice's amp envelope and its lifetime: an engine renders a voice at full level from
// noteOn until endVoice, and the layer shapes, fades and steals it.
class Engine
{
public:
    static constexpr int numSlots = 24;

    virtual ~Engine() = default;

    virtual const EngineInfo& info() const = 0;

    // Allocate everything here (message thread or prepareToPlay).
    virtual void prepare (double sampleRate, int maxBlockSize, int numVoices) = 0;
    virtual void reset() = 0;

    // Starting a voice that is still sounding must be click-free (the engine glides from its current state).
    // Pitch glide should use PitchGlide (Glide.h) so every engine glides the same way.
    virtual void noteOn (int voice, const NoteEvent& event) = 0;
    // Key released: the layer's envelope is now releasing. Engines with their own envelopes release them here.
    virtual void noteOff (int voice) = 0;
    // The layer's envelope has reached silence: drop the voice's state.
    virtual void endVoice (int voice) = 0;

    // Layer-wide pitch offset in semitones (octave + semitone + fine). Must stay phase-continuous.
    virtual void setTune (float semitones) = 0;

    virtual void setSlot (int slot, float normalisedValue) { juce::ignoreUnused (slot, normalisedValue); }

    // Add one voice's output into left/right for numSamples.
    virtual void render (int voice, float* left, float* right, int numSamples) = 0;
};
}
