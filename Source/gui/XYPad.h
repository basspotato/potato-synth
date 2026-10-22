#pragma once
#include "Theme.h"

namespace ps
{
// Stack XY pad: one layer role per corner. The background blends the four role colours by
// the same bilinear weights the stack morph will use, so the colour under the puck shows the mix.
class XYPad : public juce::Component
{
public:
    XYPad() { setRepaintsOnMouseActivity (false); }

    juce::Point<float> getPosition() const noexcept { return pos; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 14.0f);

        if (background.getWidth() != getWidth() || background.getHeight() != getHeight())
            renderBackground();
        g.drawImageAt (background, 0, 0);

        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.75f), 14.0f, 1.5f);

        // Grid
        g.setColour (theme::text.withAlpha (0.06f));
        for (int i = 1; i < 4; ++i)
        {
            const float fx = r.getX() + r.getWidth() * (float) i / 4.0f;
            const float fy = r.getY() + r.getHeight() * (float) i / 4.0f;
            g.drawVerticalLine ((int) fx, r.getY() + 8.0f, r.getBottom() - 8.0f);
            g.drawHorizontalLine ((int) fy, r.getX() + 8.0f, r.getRight() - 8.0f);
        }

        // Corner labels
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        const auto inner = r.reduced (12.0f, 8.0f);
        g.setColour (theme::sub);  g.drawText ("SUB",  inner, juce::Justification::topLeft);
        g.setColour (theme::body); g.drawText ("BODY", inner, juce::Justification::topRight);
        g.setColour (theme::mid);  g.drawText ("MID",  inner, juce::Justification::bottomLeft);
        g.setColour (theme::air);  g.drawText ("AIR",  inner, juce::Justification::bottomRight);

        // Puck with halo
        const auto p = toScreen (pos);
        const auto c = blend (pos);
        for (int i = 4; i >= 1; --i)
        {
            const float rr = 8.0f + (float) i * 6.0f;
            g.setColour (c.withAlpha (0.07f * (float) (5 - i)));
            g.fillEllipse (p.x - rr, p.y - rr, rr * 2.0f, rr * 2.0f);
        }
        g.setColour (juce::Colours::white);
        g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);
        g.setColour (c);
        g.drawEllipse (p.x - 10.0f, p.y - 10.0f, 20.0f, 20.0f, 2.0f);
    }

    void resized() override { background = {}; }
    void mouseDown (const juce::MouseEvent& e) override { setFromMouse (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setFromMouse (e); }
    void mouseDoubleClick (const juce::MouseEvent&) override { pos = { 0.5f, 0.5f }; repaint(); }

private:
    juce::Point<float> pos { 0.5f, 0.5f };
    juce::Image background;

    static juce::Colour blend (juce::Point<float> p)
    {
        const float w[] = { (1 - p.x) * (1 - p.y), p.x * (1 - p.y), (1 - p.x) * p.y, p.x * p.y };
        const juce::Colour c[] = { theme::sub, theme::body, theme::mid, theme::air };
        float rr = 0, gg = 0, bb = 0;
        for (int i = 0; i < 4; ++i)
        {
            rr += w[i] * c[i].getFloatRed();
            gg += w[i] * c[i].getFloatGreen();
            bb += w[i] * c[i].getFloatBlue();
        }
        return juce::Colour::fromFloatRGBA (rr, gg, bb, 1.0f);
    }

    juce::Point<float> toScreen (juce::Point<float> p) const
    {
        const auto r = getLocalBounds().toFloat().reduced (16.0f);
        return { r.getX() + p.x * r.getWidth(), r.getY() + p.y * r.getHeight() };
    }

    void setFromMouse (const juce::MouseEvent& e)
    {
        const auto r = getLocalBounds().toFloat().reduced (16.0f);
        pos = { juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth()),
                juce::jlimit (0.0f, 1.0f, (e.position.y - r.getY()) / r.getHeight()) };
        repaint();
    }

    // The blended field is rendered once per size at low resolution and scaled up (soft by design)
    void renderBackground()
    {
        const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
        juce::Image small (juce::Image::ARGB, 48, 48, true);
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 48; ++x)
                small.setPixelAt (x, y, blend ({ (float) x / 47.0f, (float) y / 47.0f })
                                            .withMultipliedBrightness (0.55f).withAlpha (0.55f));

        background = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics g (background);
        juce::Path clip;
        clip.addRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 13.0f);
        g.reduceClipRegion (clip);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (small, getLocalBounds().toFloat());
    }
};
}
