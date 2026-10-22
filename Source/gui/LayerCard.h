#pragma once
#include "Theme.h"

namespace ps
{
// One slot in the layer strip. Phase 0 shows the role and an empty engine slot.
class LayerCard : public juce::Component
{
public:
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
        g.drawText ("ENGINE: EMPTY", text.removeFromTop (16), juce::Justification::centredLeft);
    }

private:
    juce::String role, band;
    juce::Colour colour;
};
}
