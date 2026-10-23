#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "LayerStack.h"
#include "StackBus.h"
#include "ChainKnobs.h"
#include "Space.h"
#include "StackMorph.h"
#include "LayerLink.h"
#include "Arp.h"
#include "ModLanes.h"
#include "MixHealth.h"
#include "Presets.h"

class PotatoSynthProcessor : public juce::AudioProcessor,
                             private juce::Timer
{
public:
    PotatoSynthProcessor();
    ~PotatoSynthProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Potato Masher"; }
    bool acceptsMidi() const override { return true; }
    bool supportsMPE() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Presets are deliberately not exposed as host programs: Cubase re-sends the track's program when a
    // track is selected, which would replace the user's sound. Presets live in the plugin's own browser.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Potato Masher"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // The arp / sequencer step pattern (message thread; saved with the state)
    void setPatternStep (int index, const ps::NoteGenerator::Step& step) noexcept { noteGenerator.setStep (index, step); }
    ps::NoteGenerator::Step getPatternStep (int index) const noexcept { return noteGenerator.getStep (index); }
    void setLaneValue (int lane, int step, float value) noexcept { modLanes.setValue (lane, step, value); }
    float getLaneValue (int lane, int step) const noexcept { return modLanes.getValue (lane, step); }
    // Where each chain knob (with SUPER, BUILD/DROP and the lanes) and the XY pad actually are, for the GUI
    // What the chain knobs add to one layer's LEVEL (dB) and engine slot roles (normalised), for the GUI's map rings
    float getChainLevelDb (int layer) const noexcept { return ghostLevelDb[(size_t) layer].load(); }
    float getChainSlotOffset (int layer, int role) const noexcept { return ghostSlot[(size_t) layer][(size_t) role].load(); }
    float getEffectiveKnob (int k) const noexcept { return effectiveKnob[(size_t) juce::jlimit (0, ps::params::numChainKnobs - 1, k)].load(); }
    float getEffectivePadX() const noexcept { return effectivePad[0].load(); }
    float getEffectivePadY() const noexcept { return effectivePad[1].load(); }

private:
    // Each layer's current engine description, read by slot parameters to show engine-specific text.
    // Declared before apvts, whose parameters refer to it.
    std::array<std::atomic<const ps::EngineInfo*>, ps::params::numLayers> engineInfos {};

public:
    juce::AudioProcessorValueTreeState apvts;
    // Group/Link editing: controls follow between linked layers while being edited (LayerLink.h)
    ps::LayerLink layerLink { apvts, [this] (int a, int b) { return stack.layer (a).getEngineId() == stack.layer (b).getEngineId(); } };

    // Message thread: put an engine (by registry ID, empty string = none) into a layer
    void setLayerEngine (int layerIndex, const juce::String& engineId);
    juce::String getLayerEngine (int layerIndex) const { return stack.layer (layerIndex).getEngineId(); }
    // The engine description behind a layer's slot parameters (nullptr when the layer is empty)
    const ps::EngineInfo* getLayerEngineInfo (int layerIndex) const { return engineInfos[(size_t) layerIndex].load(); }
    // The Harmony Engine's current loudness trim (linear gain), for meters and tests
    float getLoudnessTrim() const { return loudnessTrim.getGain(); }
    const ps::LayerStack& getStack() const { return stack; }
    // Message thread: loads a factory preset (everything else returns to its default first)
    void loadPreset (const ps::presets::Preset& preset);
    juce::String getPresetName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }
    // Mix Health meters and listen modes (the editor turns the meters on while it is open)
    ps::MixHealth& getMixHealth() noexcept { return mixHealth; }
    // Message thread: the XY pad's Snapshot corners (0 top left, 1 top right, 2 bottom left, 3 bottom right).
    // Capture takes the sound as it is heard now; an empty corner stands for the live settings.
    void captureSnapshot (int corner) { snapshot.capture (corner, layerEngineIds()); }
    void clearSnapshot (int corner) { snapshot.clear (corner); }
    bool hasSnapshot (int corner) const { return snapshot.isFilled (corner); }
    // Message thread: frees engines the audio thread has finished with (also runs on a timer)
    void collectRetiredEngines() { stack.collectGarbage(); }

private:
    void timerCallback() override { collectRetiredEngines(); }
    void applyLayerEnginesFromState();
    std::array<juce::String, ps::params::numLayers> layerEngineIds() const;
    juce::String slotText (int layer, int slot, float value) const;
    void migrateState (juce::ValueTree& state) const;
    // BUILD and DROP (momentary): build ramps up over two bars while held; drop engages on the next bar line
    // while the host plays (at once when it doesn't) and lets go on release
    struct Performance { float build = 0.0f, drop = 0.0f; bool dropArmed = false, dropOn = false; };
    void updatePerformance (int numSamples, double bpm, double ppq, double barStart, double barLength);
    ps::chain::Offsets updateChainKnobs (int numSamples);

    ps::LayerStack stack;
    ps::StackBus bus;
    std::atomic<float>* masterDb = nullptr;
    std::atomic<float>* crossoverLowHz = nullptr;
    std::atomic<float>* monoBassOn = nullptr;
    std::atomic<float>* safeOutputOn = nullptr;
    std::atomic<float>* phaseAlign = nullptr;
    ps::NoteGenerator noteGenerator;
    juce::MidiBuffer generatedMidi, heldMidi;
    std::vector<int> noteOnPositions;   // this block's note-ons, for the pump's punch-through
    // HOLD acts as a sustain pedal that stays down: the pedal the stack and arp see is the real pedal OR HOLD
    const juce::MidiBuffer& applyHold (const juce::MidiBuffer& midi);
    std::atomic<float>* holdButton = nullptr;
    std::atomic<float>* cutoffKnob = nullptr, *resoKnob = nullptr;
    // Expression: pitch bend settings, and the mod wheel and aftertouch as offsets on the lane targets
    struct ExpressionParams { std::atomic<float>* bendRange, *mpe, *mpeRange, *wheelTarget, *wheelDepth, *touchTarget, *touchDepth; };
    ExpressionParams expressionParams {};
    void trackExpression (const juce::MidiBuffer& midi) noexcept;
    float modWheel = 0.0f;
    std::array<float, 17> channelPressure {};
    std::array<float, 128> notePressure {};
    ps::SweepFilter sweep;
    bool realPedal = false, pedalSent = false;
    struct ArpParams { std::atomic<float>* mode, *rate, *direction, *octaves, *gate, *swing, *latch, *length, *trigger; };
    ArpParams arpParams {};
    ps::ModLanes modLanes;
    struct LaneParams { std::atomic<float>* target, *depth, *rate, *length, *smooth; };
    std::array<LaneParams, ps::ModLanes::numLanes> laneParams {};
    std::array<float, ps::ModLanes::numTargets> laneOffsets {};
    std::array<std::atomic<float>, ps::params::numChainKnobs> effectiveKnob {};
    std::array<std::atomic<float>, ps::params::numLayers> ghostLevelDb {};
    std::array<std::array<std::atomic<float>, ps::numSlotRoles>, ps::params::numLayers> ghostSlot {};
    std::array<std::atomic<float>, 2> effectivePad {};
    std::array<std::atomic<float>*, ps::params::numChainKnobs> chainKnobs {};
    std::atomic<float>* superKnob = nullptr;
    std::atomic<float>* buildButton = nullptr;
    std::atomic<float>* padX = nullptr;
    std::atomic<float>* padY = nullptr;
    std::atomic<float>* padMode = nullptr;
    ps::morph::SnapshotMorph snapshot;   // the layers read their parameters through it
    std::array<std::atomic<float>*, ps::params::numLayers> anchors {};
    std::array<std::atomic<float>*, ps::params::numLayers> mutes {}, solos {};   // monitoring (Params.h)
    float padXSmoothed = 0.5f, padYSmoothed = 0.5f, superSmoothed = 0.0f;
    std::atomic<float>* dropButton = nullptr;
    Performance performance;
    float dropSmoothed = 0.0f;
    std::array<float, ps::params::numChainKnobs> knobPosition {};   // smoothed, audio thread
    ps::LowFocus lowFocus;
    ps::BassHarmonics bassHarmonics;
    ps::Pressure pressure;
    ps::Pressure masterDynamics;   // the patch's own OTT and glue (bus_ott, bus_glue)
    ps::BusWidth busWidth;
    struct MasterParams { std::atomic<float>* ott, *ottTime, *ottUpward, *glue, *glueRelease, *width, *subDuck; };
    MasterParams masterParams {};
    ps::Tilt tilt;
    ps::DeHarsh deHarsh;
    ps::Space space;
    ps::SyncDelay delay;
    // The sends' parameters, returns (one block) and last send levels (ramped per block)
    struct SendParams { std::atomic<float>* reverbAmount, *reverbSize, *reverbDamping, *reverbDuck,
                        *delayAmount, *delayTime, *delayFeedback, *delayTone, *delayPingPong, *delayDuck; };
    SendParams sendParams {};
    std::array<std::vector<float>, 2> reverbReturn, delayReturn;
    float reverbSend = 0.0f, bloomSend = 0.0f, delaySend = 0.0f;
    bool reverbRunning = false;
    void renderSends (float* left, float* right, int numSamples, const ps::chain::Offsets& offsets, double bpm);
    static void addRamped (float* out, const float* in, int numSamples, float from, float to) noexcept;
    ps::BuildStage buildStage;
    // BLOOM's motion: the stack's drift wobble (seconds since the stack started sounding) and slow slot LFO phases
    double driftTime = 0.0;
    std::array<std::array<float, ps::numSlotRoles>, ps::params::numLayers> motionPhase {};
    ps::TempoPump pump;
    ps::harmony::KPowerMeter knobStagesIn, knobStagesOut;   // around the bus stages the knobs drive
    bool knobStagesActive = false;
    ps::harmony::LoudnessTrim loudnessTrim;
    ps::HeldLevel trimLevel;   // the stack's held level, for the trim's playing gate
    ps::TruePeakClipper clipper;
    ps::TruePeakLimiter limiter;
    ps::MixHealth mixHealth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> masterGain { 1.0f };
    juce::SmoothedValue<float> stackGain { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PotatoSynthProcessor)
};
