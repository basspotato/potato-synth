#pragma once
#include "Theme.h"

namespace ps
{
// Rotary knobs with an accent ring that glows brighter as the value rises.
// The accent colour comes from the slider's rotarySliderFillColourId.
class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::Slider::textBoxTextColourId, theme::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::TextButton::buttonColourId, theme::panel);
        setColour (juce::TextButton::textColourOffId, theme::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        const auto area = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
        const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto centre = area.getCentre();
        const float ringW = juce::jmax (3.0f, radius * 0.14f);
        const float arcR = radius - ringW;
        const float angle = startAngle + pos * (endAngle - startAngle);

        // Glow: a few widening, fading strokes behind the value arc
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        const float glow = 0.15f + 0.85f * pos;
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (accent.withAlpha (0.10f * glow * (float) (4 - i)));
            g.strokePath (valueArc, juce::PathStrokeType (ringW + (float) i * ringW * 0.9f,
                                                          juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (accent.withAlpha (0.22f).overlaidWith (theme::panelEdge.withAlpha (0.5f)));
        g.strokePath (track, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Chain-knob map: where the chain knobs actually put this control, as a bridge from the dialled value to a dot
        if (const auto ghost = s.getProperties().getWithDefault ("ghost", -1.0); (double) ghost >= 0.0 && std::abs ((float) (double) ghost - pos) > 0.004f)
        {
            const float gp = juce::jlimit (0.0f, 1.0f, (float) (double) ghost);
            const float ga = startAngle + gp * (endAngle - startAngle);
            juce::Path bridge;
            bridge.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, angle, ga, true);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.strokePath (bridge, juce::PathStrokeType (ringW * 0.45f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (juce::Colours::white);
            g.fillEllipse (juce::Rectangle<float> (ringW * 1.3f, ringW * 1.3f).withCentre (centre.getPointOnCircumference (arcR, ga)));
        }

        // Value arc, brighter with value
        g.setColour (accent.interpolatedWith (juce::Colours::white, 0.25f * pos));
        g.strokePath (valueArc, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Body
        const float bodyR = arcR - ringW * 1.2f;
        juce::ColourGradient grad (theme::knobBody.brighter (0.5f), centre.x - bodyR * 0.4f, centre.y - bodyR * 0.6f,
                                   theme::bgBottom, centre.x + bodyR * 0.6f, centre.y + bodyR, true);
        g.setGradientFill (grad);
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (accent.withAlpha (0.25f + 0.5f * pos));
        g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.2f);

        // Pointer
        const auto tip = centre.getPointOnCircumference (bodyR * 0.8f, angle);
        const auto base = centre.getPointOnCircumference (bodyR * 0.25f, angle);
        g.setColour (theme::text);
        g.drawLine ({ base, tip }, juce::jmax (2.0f, radius * 0.06f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& accent,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        const float lit = down ? 1.0f : (highlighted ? 0.6f : 0.3f);
        juce::ColourGradient grad (accent.withAlpha (0.35f + 0.5f * lit), r.getX(), r.getY(),
                                   theme::panel, r.getX(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (accent.withAlpha (0.5f + 0.5f * lit));
        g.drawRoundedRectangle (r, 8.0f, 1.5f);
    }
};
}
