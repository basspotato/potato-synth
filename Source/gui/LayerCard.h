#pragma once
#include "Theme.h"

namespace ps
{
// One slot in the layer strip: role, band and engine. Clicking the engine name opens the engine menu;
// clicking anywhere else selects the layer.
class LayerCard : public juce::Component
{
public:
    std::function<void()> onSelect, onEngineClick;

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

    LayerCard (juce::String roleName, juce::String bandText, juce::Colour roleColour)
        : role (std::move (roleName)), band (std::move (bandText)), colour (roleColour) {}

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
        g.drawText (role, text.removeFromTop (22), juce::Justification::centredLeft);
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
};
}
