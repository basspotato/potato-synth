#pragma once
#include "Theme.h"

namespace ps
{
// Stack XY pad: one layer role per corner. The background blends the four role colours by
// the same bilinear weights the stack morph uses, so the colour under the puck shows the mix.
// Bound to the xy_x / xy_y parameters (y is 1 at the top there, 0 at the top on screen).
// The MIX / SNAPSHOT chip at the top switches xy_mode; in Snapshot mode each corner (A-D) shows STORE, which
// captures the sound as it is heard into that corner, and CLR once it holds one.
class XYPad : public juce::Component
{
public:
    XYPad() { setRepaintsOnMouseActivity (false); }

    juce::Point<float> getPosition() const noexcept { return pos; }

    void attach (juce::RangedAudioParameter& paramX, juce::RangedAudioParameter& paramY)
    {
        xAttachment = std::make_unique<juce::ParameterAttachment> (paramX, [this] (float v) { pos.x = v; repaint(); });
        yAttachment = std::make_unique<juce::ParameterAttachment> (paramY, [this] (float v) { pos.y = 1.0f - v; repaint(); });
        xAttachment->sendInitialUpdate();
        yAttachment->sendInitialUpdate();
    }

    // Snapshot corners: 0 top left, 1 top right, 2 bottom left, 3 bottom right
    std::function<void (int)> onStore, onClear;
    std::function<bool (int)> isStored;

    void attachMode (juce::RangedAudioParameter& paramMode)
    {
        modeAttachment = std::make_unique<juce::ParameterAttachment> (paramMode, [this] (float v) { snapshotMode = v > 0.5f; repaint(); });
        modeAttachment->sendInitialUpdate();
    }

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

        // Corner labels: the layer roles (Mix) or the snapshot corners with their buttons (Snapshot)
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        const auto inner = r.reduced (12.0f, 8.0f);
        if (! snapshotMode)
        {
            g.setColour (theme::sub);  g.drawText ("SUB",  inner, juce::Justification::topLeft);
            g.setColour (theme::body); g.drawText ("BODY", inner, juce::Justification::topRight);
            g.setColour (theme::mid);  g.drawText ("MID",  inner, juce::Justification::bottomLeft);
            g.setColour (theme::air);  g.drawText ("AIR",  inner, juce::Justification::bottomRight);
        }
        else
        {
            static const juce::Justification just[] = { juce::Justification::topLeft, juce::Justification::topRight,
                                                        juce::Justification::bottomLeft, juce::Justification::bottomRight };
            for (int c = 0; c < 4; ++c)
            {
                const bool stored = isStored != nullptr && isStored (c);
                g.setColour (stored ? theme::text : theme::textDim);
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + c)) + (stored ? juce::String (juce::CharPointer_UTF8 (" \xe2\x97\x8f")) : juce::String()),
                            inner, just[c]);
                drawChip (g, storeChip (c), "STORE", stored ? theme::textDim : theme::text);
                if (stored)
                    drawChip (g, clearChip (c), "CLR", theme::textDim);
            }
        }
        drawChip (g, modeChip(), snapshotMode ? "SNAPSHOT" : "MIX", theme::text);

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
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        if (modeChip().contains (e.position) && modeAttachment != nullptr)
        {
            modeAttachment->setValueAsCompleteGesture (snapshotMode ? 0.0f : 1.0f);
            return;
        }
        if (snapshotMode)
            for (int c = 0; c < 4; ++c)
            {
                const bool stored = isStored != nullptr && isStored (c);
                if (storeChip (c).contains (e.position))
                {
                    if (onStore != nullptr) onStore (c);
                    repaint();
                    return;
                }
                if (stored && clearChip (c).contains (e.position))
                {
                    if (onClear != nullptr) onClear (c);
                    repaint();
                    return;
                }
            }
        dragging = true;
        if (xAttachment != nullptr) { xAttachment->beginGesture(); yAttachment->beginGesture(); }
        setFromMouse (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) setFromMouse (e); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging && xAttachment != nullptr) { xAttachment->endGesture(); yAttachment->endGesture(); }
        dragging = false;
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        pos = { 0.5f, 0.5f };
        send();
        repaint();
    }

private:
    juce::Point<float> pos { 0.5f, 0.5f };
    juce::Image background;
    std::unique_ptr<juce::ParameterAttachment> xAttachment, yAttachment, modeAttachment;
    bool snapshotMode = false, dragging = false;

    juce::Rectangle<float> modeChip() const { return { getWidth() * 0.5f - 42.0f, 8.0f, 84.0f, 18.0f }; }
    // Below the corner's label at the top, above it at the bottom
    juce::Rectangle<float> storeChip (int c) const
    {
        const auto inner = getLocalBounds().toFloat().reduced (12.0f, 8.0f);
        const bool right = (c & 1) != 0, bottom = c >= 2;
        const float x = right ? inner.getRight() - 46.0f : inner.getX();
        const float y = bottom ? inner.getBottom() - 40.0f : inner.getY() + 20.0f;
        return { x, y, 46.0f, 18.0f };
    }
    juce::Rectangle<float> clearChip (int c) const
    {
        const auto s = storeChip (c);
        return s.translated ((c & 1) != 0 ? -38.0f : 50.0f, 0.0f).withWidth (34.0f);
    }
    static void drawChip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour colour)
    {
        g.setColour (theme::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (colour);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (text, r, juce::Justification::centred);
    }

    void send()
    {
        if (xAttachment == nullptr)
            return;
        xAttachment->setValueAsPartOfGesture (pos.x);
        yAttachment->setValueAsPartOfGesture (1.0f - pos.y);
    }

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
        send();
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
