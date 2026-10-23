#pragma once
#include "Theme.h"

namespace ps
{
// The potato emblem from Potato-Kicker: yellow-orange line art with a calm face. The eyes glow brighter
// while the synth is sounding (setActivity 0..1).
class PotatoBadge : public juce::Component
{
public:
    void setActivity (float a)
    {
        a = juce::jlimit (0.0f, 1.0f, a);
        if (std::abs (a - act) > 0.01f)
        {
            act = a;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        using juce::Point;
        const auto b = getLocalBounds().toFloat();
        const Point<float> c (b.getCentreX(), b.getCentreY());
        const float R = juce::jmin (b.getWidth() / 2.6f, b.getHeight() / 2.0f) * 0.92f;
        const float k = R / 60.0f;   // the strokes were drawn at a 60 px radius
        const Path spud = potatoPath (c, R);

        g.setGradientFill (juce::ColourGradient (orange.withAlpha (0.16f + 0.14f * act), c.x, c.y, orange.withAlpha (0.0f), c.x + R * 1.7f, c.y, true));
        g.fillEllipse (juce::Rectangle<float> (R * 3.4f, R * 2.7f).withCentre (c));

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a200c), c.x - R * 0.35f, c.y - R * 0.45f, juce::Colour (0xff0d0704), c.x + R * 0.95f, c.y + R * 0.8f, true));
        g.fillPath (spud);
        const auto sb = spud.getBounds();
        auto ramp = [&] (float a) { return juce::ColourGradient (orange.withAlpha (a), sb.getX(), sb.getBottom(), yellow.withAlpha (a), sb.getRight(), sb.getY(), false); };
        g.setGradientFill (ramp (0.16f)); g.strokePath (spud, juce::PathStrokeType (9.0f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill (ramp (0.40f)); g.strokePath (spud, juce::PathStrokeType (4.0f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill (ramp (1.0f));  g.strokePath (spud, juce::PathStrokeType (juce::jmax (1.0f, 1.7f * k), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Skin highlight and the little sprout spots
        Path hl;
        hl.addCentredArc (c.x - R * 0.1f, c.y + R * 0.02f, R * 1.0f, R * 0.66f, juce::degreesToRadians (-14.0f), juce::degreesToRadians (-78.0f), juce::degreesToRadians (-22.0f), true);
        g.setColour (juce::Colour (0xffffc27a).withAlpha (0.35f));
        g.strokePath (hl, juce::PathStrokeType (juce::jmax (0.8f, 1.2f * k), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (orange.withAlpha (0.5f));
        for (auto p : { Point<float> (-0.86f, 0.34f), Point<float> (0.88f, -0.40f), Point<float> (0.66f, 0.52f), Point<float> (-0.48f, 0.62f), Point<float> (-0.10f, -0.70f) })
            g.drawEllipse (juce::Rectangle<float> (3.4f * k, 2.2f * k).withCentre (c + p * R), juce::jmax (0.6f, k));

        // Face: calm round eyes with glowing pupils, small smile
        for (int i = 0; i < 2; ++i)
        {
            const Point<float> e = c + Point<float> (i == 0 ? -0.34f : 0.30f, i == 0 ? -0.16f : -0.19f) * R;
            const auto socket = juce::Rectangle<float> (R * 0.30f, R * 0.36f).withCentre (e);
            g.setColour (juce::Colour (0xff080402)); g.fillEllipse (socket);
            g.setColour (orange.withAlpha (0.85f)); g.drawEllipse (socket, juce::jmax (0.8f, 1.3f * k));
            const float pr = R * (0.075f + 0.02f * act);
            const Point<float> pc = e + Point<float> (R * 0.025f, R * 0.03f);
            g.setColour (orange.withAlpha (0.25f + 0.4f * act)); g.fillEllipse (juce::Rectangle<float> (pr * 4.0f, pr * 4.0f).withCentre (pc));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff3c8), pc.x - pr * 0.3f, pc.y - pr * 0.3f, yellow, pc.x + pr, pc.y + pr, true));
            g.fillEllipse (juce::Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (pc));
            g.setColour (juce::Colours::white.withAlpha (0.85f)); g.fillEllipse (juce::Rectangle<float> (pr * 0.6f, pr * 0.6f).withCentre (pc - Point<float> (pr * 0.35f, pr * 0.4f)));
        }
        Path mouth;
        mouth.startNewSubPath (c.x - R * 0.22f, c.y + R * 0.30f);
        mouth.quadraticTo (c.x - R * 0.02f, c.y + R * 0.46f, c.x + R * 0.22f, c.y + R * 0.27f);
        g.setColour (orange.withAlpha (0.3f)); g.strokePath (mouth, juce::PathStrokeType (4.5f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (yellow);                  g.strokePath (mouth, juce::PathStrokeType (juce::jmax (1.0f, 1.9f * k), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The title and the logo share this ramp
    static inline const juce::Colour orange { 0xffff7212 }, yellow { 0xffffd23f };

private:
    using Path = juce::Path;

    static Path potatoPath (juce::Point<float> c, float R)
    {
        const float radii[16] { 1.02f, 0.97f, 0.90f, 0.93f, 0.99f, 0.95f, 0.91f, 0.98f, 1.06f, 1.03f, 0.94f, 0.90f, 0.96f, 1.04f, 1.00f, 1.05f };
        const float tilt = juce::degreesToRadians (-10.0f), ct = std::cos (tilt), st = std::sin (tilt);
        juce::Point<float> pts[16];
        for (int i = 0; i < 16; ++i)
        {
            const float a = juce::degreesToRadians ((float) i * 22.5f);
            const float x = std::cos (a) * R * 1.22f * radii[i], y = std::sin (a) * R * 1.0f * radii[i];
            pts[i] = c + juce::Point<float> (x * ct - y * st, x * st + y * ct);
        }
        Path p;
        auto mid = [&] (int i, int j) { return (pts[i] + pts[j]) * 0.5f; };
        p.startNewSubPath (mid (15, 0));
        for (int i = 0; i < 16; ++i)
            p.quadraticTo (pts[i], mid (i, (i + 1) % 16));
        p.closeSubPath();
        return p;
    }

    float act = 0.0f;
};
}
