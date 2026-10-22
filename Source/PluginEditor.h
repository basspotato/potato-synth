#pragma once
#include "PluginProcessor.h"
#include "gui/Look.h"
#include "gui/XYPad.h"
#include "gui/LayerCard.h"

class PotatoSynthEditor : public juce::AudioProcessorEditor
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

    PotatoSynthProcessor& processor;
    ps::Look look;

    std::array<std::unique_ptr<ps::LayerCard>, 4> layers;
    ps::XYPad xyPad;

    // Chain knobs, SUPER, BUILD and DROP are a layout preview until Phase 3 wires them to the engine
    std::array<Knob, 8> chain;
    Knob super, master;
    juce::TextButton build { "BUILD" }, drop { "DROP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PotatoSynthEditor)
};
