#pragma once
#include "PluginProcessor.h"
#include "gui/Look.h"
#include "gui/XYPad.h"
#include "gui/LayerCard.h"
#include "gui/LayerPage.h"

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
    void showPage (bool layerPageVisible);
    int layerRole (int layerIndex) const;
    void setUpBusKnob (Knob&, const juce::String& name, const juce::ParameterID&,
                       std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>&);
    void timerCallback() override;

    PotatoSynthProcessor& processor;
    ps::Look look;

    std::array<std::unique_ptr<ps::LayerCard>, 4> layers;
    ps::XYPad xyPad;
    ps::LayerPage layerPage;
    juce::TextButton stackTab { "STACK" }, layerTab { "LAYER" };
    int selectedLayer = 2;

    // Chain knobs, SUPER, BUILD and DROP are a layout preview until Phase 3 wires them to the engine
    std::array<Knob, 8> chain;
    Knob super, master;
    juce::TextButton build { "BUILD" }, drop { "DROP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;
    // Stack bus controls in the header
    Knob crossoverLow, crossoverHigh;
    juce::TextButton monoBass { "MONO BASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> crossoverLowAttachment, crossoverHighAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoBassAttachment;
    std::array<int, 4> shownRoles { -1, -1, -1, -1 };
    float shownLow = 0.0f, shownHigh = 0.0f;

    std::array<Knob, 4> layerLevels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> layerLevelAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> chainAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> superAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PotatoSynthEditor)
};
