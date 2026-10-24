#pragma once
#include "Theme.h"
#include "../Params.h"
#include "WaveformView.h"
#include "../Inserts.h"

namespace ps
{
// The MIX page: the patch's master chain (OTT, glue, width, Sub Duck) over a waveform of the output.
class MixPage : public juce::Component
{
public:
    MixPage (juce::AudioProcessorValueTreeState& state, OutputScope& scope)
        : apvts (state), waveform (scope)
    {
        struct Def { const char* name; juce::ParameterID id; juce::Colour colour; };
        const Def defs[] = { { "OTT", params::ottAmount, theme::mid }, { "OTT TIME", params::ottTime, theme::mid },
                             { "UPWARD", params::ottUpward, theme::mid }, { "GLUE", params::glueAmount, theme::body },
                             { "RELEASE", params::glueRelease, theme::body }, { "WIDTH", params::stackWidth, theme::air },
                             { "SUB DUCK", params::subDuck, theme::sub } };
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& k = knobs[i];
            k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
            k.slider.setColour (juce::Slider::rotarySliderFillColourId, defs[i].colour);
            addAndMakeVisible (k.slider);
            k.label.setText (defs[i].name, juce::dontSendNotification);
            k.label.setJustificationType (juce::Justification::centred);
            k.label.setFont (ps::fonts::bold (11.0f));
            k.label.setColour (juce::Label::textColourId, defs[i].colour.interpolatedWith (theme::text, 0.35f));
            addAndMakeVisible (k.label);
            k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, defs[i].id.getParamID(), k.slider);
            auto* p = apvts.getParameter (defs[i].id.getParamID());
            k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        }

        addAndMakeVisible (waveform);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.75f), 14.0f, 1.5f);

        g.setColour (theme::text);
        g.setFont (ps::fonts::bold (20.0f));
        g.drawText ("MASTER CHAIN", titleArea, juce::Justification::centredLeft);
        g.drawText ("OUTPUT", scopeTitle, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 12);
        titleArea = r.removeFromTop (28);
        auto row = r.removeFromTop (juce::jmin (120, r.getHeight() / 3));
        const int w = row.getWidth() / (int) knobs.size();
        for (auto& k : knobs)
        {
            auto cell = row.removeFromLeft (w).reduced (4, 0);
            k.label.setBounds (cell.removeFromTop (14));
            k.slider.setBounds (cell);
        }
        r.removeFromTop (12);
        scopeTitle = r.removeFromTop (28);
        r.removeFromTop (6);
        waveform.setBounds (r);
    }

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    juce::AudioProcessorValueTreeState& apvts;
    WaveformView waveform;
    std::array<Knob, 7> knobs;
    juce::Rectangle<int> titleArea, scopeTitle;
};
}
