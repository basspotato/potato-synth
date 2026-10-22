#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "LayerStack.h"
#include "StackBus.h"

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
    // Message thread: frees engines the audio thread has finished with (also runs on a timer)
    void collectRetiredEngines() { stack.collectGarbage(); }

private:
    void timerCallback() override { collectRetiredEngines(); }
    void applyLayerEnginesFromState();
    juce::String slotText (int layer, int slot, float value) const;
    void migrateState (juce::ValueTree& state) const;

    ps::LayerStack stack;
    ps::StackBus bus;
    std::atomic<float>* masterDb = nullptr;
    std::atomic<float>* crossoverLowHz = nullptr;
    std::atomic<float>* monoBassOn = nullptr;
    std::atomic<float>* safeOutputOn = nullptr;
    ps::TruePeakClipper clipper;
    ps::TruePeakLimiter limiter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> masterGain { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PotatoSynthProcessor)
};
