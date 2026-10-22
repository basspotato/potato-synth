#pragma once
#include "Theme.h"
#include "../Params.h"

namespace ps
{
// Draws the selected layer's amp envelope from its parameters. Segment widths follow the square root
// of each time, so short and long envelopes both stay readable.
class EnvelopeView : public juce::Component
{
public:
    void setColour (juce::Colour c) { colour = c; repaint(); }

    void setValues (float attackMs, float decayMs, float sustain, float releaseMs)
    {
        const std::array<float, 4> v { attackMs, decayMs, sustain, releaseMs };
        if (v != values)
        {
            values = v;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::bgBottom.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        r = r.reduced (14.0f, 12.0f);
        const float a = std::sqrt (values[0]), d = std::sqrt (values[1]), rel = std::sqrt (values[3]);
        const float hold = (a + d + rel) * 0.25f + 1.0f;
        const float scale = r.getWidth() / (a + d + hold + rel);
        const float s = juce::jlimit (0.0f, 1.0f, values[2]);
        auto yFor = [&r] (float level) { return r.getBottom() - level * r.getHeight(); };

        const float x0 = r.getX(), x1 = x0 + a * scale, x2 = x1 + d * scale, x3 = x2 + hold * scale, x4 = x3 + rel * scale;
        juce::Path p;
        p.startNewSubPath (x0, yFor (0.0f));
        p.quadraticTo (x0 + (x1 - x0) * 0.35f, yFor (0.85f), x1, yFor (1.0f));             // convex attack
        p.quadraticTo (x1 + (x2 - x1) * 0.15f, yFor (s), x2, yFor (s));                    // exponential decay
        p.lineTo (x3, yFor (s));
        p.quadraticTo (x3 + (x4 - x3) * 0.12f, yFor (0.0f), x4, yFor (0.0f));              // exponential release

        juce::Path fill (p);
        fill.lineTo (x0, yFor (0.0f));
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f), 0.0f, r.getY(),
                                                 colour.withAlpha (0.02f), 0.0f, r.getBottom(), false));
        g.fillPath (fill);
        for (int i = 2; i >= 1; --i)
        {
            g.setColour (colour.withAlpha (0.12f * (float) (3 - i)));
            g.strokePath (p, juce::PathStrokeType (2.0f + 3.0f * (float) i));
        }
        g.setColour (colour.interpolatedWith (juce::Colours::white, 0.2f));
        g.strokePath (p, juce::PathStrokeType (2.0f));

        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("AMP ENVELOPE", getLocalBounds().reduced (12, 8), juce::Justification::topRight);
    }

private:
    std::array<float, 4> values { 2.0f, 400.0f, 1.0f, 300.0f };
    juce::Colour colour { theme::mid };
};

// Detail page for one layer: engine picker, voice mode, amp envelope, pan, tune and glide.
// Engine-specific controls join this page as the engines arrive.
class LayerPage : public juce::Component,
                  private juce::Timer
{
public:
    std::function<void()> onEngineClick;

    explicit LayerPage (juce::AudioProcessorValueTreeState& state) : apvts (state)
    {
        addAndMakeVisible (envelope);
        engineButton.onClick = [this] { if (onEngineClick != nullptr) onEngineClick(); };
        addAndMakeVisible (engineButton);

        for (int m = 0; m < (int) modeButtons.size(); ++m)
        {
            auto& b = modeButtons[(size_t) m];
            b.setButtonText (params::voiceModeNames[m].toUpperCase());
            b.setClickingTogglesState (false);
            b.onClick = [this, m] { setVoiceMode (m); };
            addAndMakeVisible (b);
        }

        const char* const names[] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "PAN", "OCTAVE", "SEMI", "FINE", "GLIDE" };
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& k = knobs[i];
            k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            addAndMakeVisible (k.slider);
            k.label.setText (names[i], juce::dontSendNotification);
            k.label.setJustificationType (juce::Justification::centred);
            k.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            addAndMakeVisible (k.label);
        }
        startTimerHz (15);
    }

    void showLayer (int index, const juce::String& role, juce::Colour colour)
    {
        layer = index;
        roleName = role;
        accent = colour;
        envelope.setColour (colour);
        engineButton.setColour (juce::TextButton::buttonColourId, colour);

        const juce::ParameterID ids[] = { params::layerAttack (index), params::layerDecay (index), params::layerSustain (index),
                                          params::layerRelease (index), params::layerPan (index), params::layerOctave (index),
                                          params::layerSemi (index), params::layerFine (index), params::layerGlide (index) };
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& k = knobs[i];
            k.attachment.reset();
            k.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
            k.label.setColour (juce::Label::textColourId, colour.interpolatedWith (theme::text, 0.35f));
            k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ids[i].getParamID(), k.slider);
            auto* p = apvts.getParameter (ids[i].getParamID());
            k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        }
        timerCallback();
        repaint();
    }

    void setEngineName (const juce::String& name)
    {
        engineButton.setButtonText (juce::String ("ENGINE:  ") + (name.isEmpty() ? juce::String ("EMPTY") : name.toUpperCase())
                                    + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (accent.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.75f), 14.0f, 1.5f);

        g.setColour (theme::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (roleName + " LAYER", getLocalBounds().reduced (18, 12).removeFromTop (28), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 12);
        auto header = r.removeFromTop (28);
        engineButton.setBounds (header.removeFromRight (juce::jmin (260, header.getWidth() / 2)));
        r.removeFromTop (10);

        auto rows = r;
        const int knobRowH = juce::jmin (110, rows.getHeight() / 3);
        auto bottomRow = rows.removeFromBottom (knobRowH);
        rows.removeFromBottom (6);
        auto envRow = rows.removeFromBottom (knobRowH);
        rows.removeFromBottom (10);
        envelope.setBounds (rows);

        auto place = [] (Knob& k, juce::Rectangle<int> cell)
        {
            k.label.setBounds (cell.removeFromTop (14));
            k.slider.setBounds (cell);
        };
        const int w = envRow.getWidth() / 4;
        for (size_t i = 0; i < 4; ++i)
            place (knobs[i], envRow.removeFromLeft (w).reduced (4, 0));
        // Bottom row: voice mode buttons stacked in the first cell, then pan, tune and glide
        const int bw = bottomRow.getWidth() / 6;
        auto modes = bottomRow.removeFromLeft (bw).reduced (4, 4);
        const int mh = juce::jmin (26, modes.getHeight() / 3);
        modes = modes.withSizeKeepingCentre (modes.getWidth(), mh * 3);
        for (auto& b : modeButtons)
            b.setBounds (modes.removeFromTop (mh).reduced (0, 2));
        for (size_t i = 4; i < knobs.size(); ++i)
            place (knobs[i], bottomRow.removeFromLeft (bw).reduced (4, 0));
    }

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override
    {
        auto value = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID())->load(); };
        envelope.setValues (value (params::layerAttack (layer)), value (params::layerDecay (layer)),
                            value (params::layerSustain (layer)) * 0.01f, value (params::layerRelease (layer)));

        const int mode = juce::roundToInt (value (params::layerVoiceMode (layer)));
        for (int m = 0; m < (int) modeButtons.size(); ++m)
        {
            auto& b = modeButtons[(size_t) m];
            b.setColour (juce::TextButton::buttonColourId, m == mode ? accent : theme::panel);
            b.setAlpha (m == mode ? 1.0f : 0.7f);
        }
    }

    void setVoiceMode (int mode)
    {
        auto* p = apvts.getParameter (params::layerVoiceMode (layer).getParamID());
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) mode));
        p->endChangeGesture();
        timerCallback();
    }

    juce::AudioProcessorValueTreeState& apvts;
    int layer = 0;
    juce::String roleName;
    juce::Colour accent { theme::mid };
    EnvelopeView envelope;
    juce::TextButton engineButton;
    std::array<Knob, 9> knobs;
    std::array<juce::TextButton, 3> modeButtons;
};
}
