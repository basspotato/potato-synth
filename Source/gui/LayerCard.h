#pragma once
#include "Theme.h"
#include "../Params.h"

namespace ps
{
// One slot in the layer strip: role, band, engine, and link / mute / solo. Clicking the engine name opens the engine
// menu; clicking anywhere else selects the layer.
class LayerCard : public juce::Component
{
public:
    std::function<void()> onSelect, onEngineClick;

    void setRole (const juce::String& roleName, const juce::String& bandText, juce::Colour roleColour)
    {
        if (roleName != role || bandText != band || roleColour != colour)
        {
            role = roleName;
            band = bandText;
            colour = roleColour;
            repaint();
        }
    }

    void setSelected (bool shouldBeSelected)
    {
        if (shouldBeSelected != selected)
        {
            selected = shouldBeSelected;
            repaint();
        }
    }

    void setEngineName (const juce::String& name)
    {
        if (name != engineName)
        {
            engineName = name;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition()))
            return;
        auto& callback = engineArea().contains (e.getPosition()) ? onEngineClick : onSelect;
        if (callback != nullptr)
            callback();
    }

    LayerCard (juce::String roleName, juce::String bandText, juce::Colour roleColour,
               juce::AudioProcessorValueTreeState& state, int layerIndex)
        : role (std::move (roleName)), band (std::move (bandText)), colour (roleColour), number (layerIndex + 1)
    {
        for (auto* b : { &mute, &solo, &link })
        {
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonColourId, theme::panel);
            b->setColour (juce::TextButton::textColourOffId, theme::textDim);
            b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            addAndMakeVisible (*b);
        }
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff5a5a));
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffd84a));
        link.setColour (juce::TextButton::buttonOnColourId, theme::mid);
        link.setTooltip ("Link: edits to a linked layer's controls move the other linked layers too");
        linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::layerLink (layerIndex).getParamID(), link);
        mute.setTooltip ("Mute this layer");
        solo.setTooltip ("Solo: only soloed layers sound");
        muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::layerMute (layerIndex).getParamID(), mute);
        soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::layerSolo (layerIndex).getParamID(), solo);
    }

    void resized() override
    {
        // Left of the LEVEL knob the editor places over the card's right end
        auto r = getLocalBounds().reduced (10, 8).removeFromTop (20).withTrimmedRight (80);
        solo.setBounds (r.removeFromRight (24));
        r.removeFromRight (4);
        mute.setBounds (r.removeFromRight (24));
        r.removeFromRight (4);
        link.setBounds (r.removeFromRight (24));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        juce::ColourGradient grad (colour.withAlpha (selected ? 0.5f : 0.28f), r.getX(), r.getY(),
                                   theme::panel, r.getRight(), r.getY(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 10.0f);
        if (selected)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.drawRoundedRectangle (r.reduced (1.5f), 10.0f, 4.0f);
        }
        g.setColour (colour.withAlpha (selected ? 1.0f : 0.7f));
        g.drawRoundedRectangle (r, 10.0f, selected ? 2.0f : 1.2f);

        // Role bar
        g.setColour (colour);
        g.fillRoundedRectangle (r.removeFromLeft (6.0f).reduced (0.0f, 6.0f), 3.0f);

        auto text = getLocalBounds().reduced (16, 8);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        auto title = text.removeFromTop (22);
        g.drawText (role, title, juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("L" + juce::String (number), title.withTrimmedLeft (juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (17.0f, juce::Font::bold), role) + 8),
                    juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (band, text.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (engineName.isEmpty() ? theme::textDim : colour.interpolatedWith (theme::text, 0.4f));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (engineName.isEmpty() ? juce::String ("+ ADD ENGINE") : engineName.toUpperCase() + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")),
                    engineArea(), juce::Justification::centredLeft);
    }

private:
    juce::Rectangle<int> engineArea() const
    {
        return getLocalBounds().reduced (16, 8).withTrimmedTop (22 + 16 + 6).withHeight (18).withTrimmedRight (70);
    }

    bool selected = false;
    juce::String role, band, engineName;
    juce::Colour colour;
    int number = 1;
    juce::TextButton mute { "M" }, solo { "S" }, link { "L" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment, soloAttachment, linkAttachment;
};
}
