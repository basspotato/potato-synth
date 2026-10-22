#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "LayerStack.h"
#include "StackBus.h"
#include "ChainKnobs.h"
#include "Space.h"

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

    const juce::String getName() const override { return "Potato-Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Presets are deliberately not exposed as host programs: Cubase re-sends the track's program when a
    // track is selected, which would replace the user's sound. Presets live in the plugin's own browser.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Potato-Synth"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    // Each layer's current engine description, read by slot parameters to show engine-specific text.
    // Declared before apvts, whose parameters refer to it.
    std::array<std::atomic<const ps::EngineInfo*>, ps::params::numLayers> engineInfos {};

public:
    juce::AudioProcessorValueTreeState apvts;

    // Message thread: put an engine (by registry ID, empty string = none) into a layer
    void setLayerEngine (int layerIndex, const juce::String& engineId);
    juce::String getLayerEngine (int layerIndex) const { return stack.layer (layerIndex).getEngineId(); }
    // The engine description behind a layer's slot parameters (nullptr when the layer is empty)
    const ps::EngineInfo* getLayerEngineInfo (int layerIndex) const { return engineInfos[(size_t) layerIndex].load(); }
    // The Harmony Engine's current loudness trim (linear gain), for meters and tests
    float getLoudnessTrim() const { return loudnessTrim.getGain(); }
    const ps::LayerStack& getStack() const { return stack; }
    // Message thread: frees engines the audio thread has finished with (also runs on a timer)
    void collectRetiredEngines() { stack.collectGarbage(); }

private:
    void timerCallback() override { collectRetiredEngines(); }
    void applyLayerEnginesFromState();
    juce::String slotText (int layer, int slot, float value) const;
    void migrateState (juce::ValueTree& state) const;
    ps::chain::Offsets updateChainKnobs (int numSamples);

    ps::LayerStack stack;
    ps::StackBus bus;
    std::atomic<float>* masterDb = nullptr;
    std::atomic<float>* crossoverLowHz = nullptr;
    std::atomic<float>* monoBassOn = nullptr;
    std::atomic<float>* safeOutputOn = nullptr;
    std::array<std::atomic<float>*, ps::params::numChainKnobs> chainKnobs {};
    std::atomic<float>* superKnob = nullptr;
    std::array<float, ps::params::numChainKnobs> knobPosition {};   // smoothed, audio thread
    ps::LowFocus lowFocus;
    ps::Pressure pressure;
    ps::Tilt tilt;
    ps::DeHarsh deHarsh;
    ps::Space space;
    // BLOOM's motion: the stack's drift (Ornstein-Uhlenbeck, then smoothed) and slow slot LFO phases
    float driftState = 0.0f, driftSmoothed = 0.0f;
    std::array<std::array<float, ps::numSlotRoles>, ps::params::numLayers> motionPhase {};
    juce::Random driftRandom { 0x5eed };
    ps::TempoPump pump;
    ps::harmony::KPowerMeter knobStagesIn, knobStagesOut;   // around the bus stages the knobs drive
    bool knobStagesActive = false;
    ps::harmony::LoudnessTrim loudnessTrim;
    ps::TruePeakClipper clipper;
    ps::TruePeakLimiter limiter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> masterGain { 1.0f };
    juce::SmoothedValue<float> stackGain { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PotatoSynthProcessor)
};
