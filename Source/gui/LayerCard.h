#pragma once
#include "Theme.h"

namespace ps
{
// One slot in the layer strip: role, band and engine. Clicking it opens the engine menu.
class LayerCard : public juce::Component
{
public:
    std::function<void()> onClick;

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
        if (onClick != nullptr && getLocalBounds().contains (e.getPosition()))
            onClick();
    }

    LayerCard (juce::String roleName, juce::String bandText, juce::Colour roleColour)
        : role (std::move (roleName)), band (std::move (bandText)), colour (roleColour) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        juce::ColourGradient grad (colour.withAlpha (0.28f), r.getX(), r.getY(),
                                   theme::panel, r.getRight(), r.getY(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (colour.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 10.0f, 1.2f);

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
        text.removeFromTop (6);
        g.setColour (engineName.isEmpty() ? theme::textDim : colour.interpolatedWith (theme::text, 0.4f));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (engineName.isEmpty() ? juce::String ("+ ADD ENGINE") : engineName.toUpperCase(),
                    text.removeFromTop (18), juce::Justification::centredLeft);
    }

private:
    juce::String role, band, engineName;
    juce::Colour colour;
};
}
