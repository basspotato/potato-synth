#pragma once
#include "PluginProcessor.h"
#include "gui/Look.h"
#include "gui/XYPad.h"
#include "gui/LayerCard.h"
#include "gui/LayerPage.h"
#include "gui/ArpPage.h"
#include "gui/MixPage.h"
#include "gui/PresetBar.h"

class PotatoSynthEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit PotatoSynthEditor (PotatoSynthProcessor&);
    ~PotatoSynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label label;
    };

    void setUpKnob (Knob&, const juce::String& name, juce::Colour accent);
    void showEngineMenu (int layerIndex);
    void selectLayer (int layerIndex);
    enum class Page { stack, layer, arp, mix };
    void showPage (Page);
    int layerRole (int layerIndex) const;
    void setUpBusKnob (Knob&, const juce::String& name, const juce::ParameterID&,
                       std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>&);
    void timerCallback() override;

    PotatoSynthProcessor& processor;
    ps::Look look;

    std::array<std::unique_ptr<ps::LayerCard>, ps::params::numLayers> layers;
    ps::XYPad xyPad;
    ps::LayerPage layerPage;
    ps::ArpPage arpPage;
    ps::MixPage mixPage;
    ps::PresetBar presetBar { [this] (int i) { processor.loadPreset (ps::presets::factory()[(size_t) i]); },
                              [this] { return processor.getPresetName(); } };
    juce::TextButton stackTab { "STACK" }, layerTab { "LAYER" }, arpTab { "ARP / SEQ" }, mixTab { "MIX" };
    int selectedLayer = 2;
    // The strip shows four layers at a time: bank 0 is layers 1-4, bank 1 is layers 5-8
    int layerBank = 0;
    std::array<juce::TextButton, 2> bankButtons { juce::TextButton ("LAYERS 1-4"), juce::TextButton ("LAYERS 5-8") };
    void showBank (int bank);

    // Chain knobs, SUPER, and the momentary BUILD and DROP
    std::array<Knob, 8> chain;
    Knob super, master, cutoff, reso;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cutoffAttachment, resoAttachment;
    juce::TextButton build { "BUILD" }, drop { "DROP" }, hold { "HOLD" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;
    // Stack bus controls in the header
    Knob crossoverLow, crossoverHigh;
    juce::TextButton monoBass { "MONO BASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> crossoverLowAttachment, crossoverHighAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoBassAttachment;
    std::array<int, ps::params::numLayers> shownRoles { -1, -1, -1, -1, -1, -1, -1, -1 };
    float shownLow = 0.0f, shownHigh = 0.0f;

    std::array<Knob, ps::params::numLayers> layerLevels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, ps::params::numLayers> layerLevelAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> chainAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> superAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PotatoSynthEditor)
};
