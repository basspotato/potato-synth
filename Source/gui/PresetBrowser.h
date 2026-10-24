#pragma once
#include "Theme.h"
#include "../Presets.h"

namespace ps
{
// The preset browser, laid out like Potato-Kicker's: the categories down the left, the category's presets as
// tiles on the right. It covers the editor with a dim backdrop; picking a tile loads the preset and keeps the
// browser open to audition the next, and the X, Esc or a click outside closes it.
class PresetBrowser : public juce::Component
{
public:
    std::function<void (int)> onPick;

    PresetBrowser()
    {
        setWantsKeyboardFocus (true);
        setVisible (false);
    }

    // The editor's scale (its frame width over the base width), so the panel scales with the rest of the GUI
    void setScale (float s) { scale = s; repaint(); }

    void show (int currentPreset)
    {
        const auto& bank = presets::factory();
        for (auto& list : byCategory)
            list.clear();
        for (size_t i = 0; i < bank.size(); ++i)
            byCategory[(size_t) bank[i].category].push_back ((int) i);
        current = currentPreset;
        category = current >= 0 ? (int) bank[(size_t) current].category : 0;
        hoverIdx = -1;
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        const auto panel = panelBounds();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (panel.translated (0.0f, 12.0f * scale), 12.0f * scale);

        juce::Graphics::ScopedSaveState state (g);
        g.setOrigin (panel.getPosition().roundToInt());
        g.addTransform (juce::AffineTransform::scale (scale));

        const juce::Rectangle<float> b (0.0f, 0.0f, panelW, panelH);
        g.setGradientFill (juce::ColourGradient (theme::bgTop.brighter (0.05f), 0.0f, 0.0f, theme::bgBottom, 0.0f, panelH, false));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.0f);
        g.setColour (theme::panelEdge.withAlpha (0.55f));
        g.drawVerticalLine (listW, 14.0f, panelH - 14.0f);

        g.setColour (theme::text);
        g.setFont (fonts::bold (15.0f, 0.3f));
        g.drawText ("PRESETS", juce::Rectangle<float> (22.0f, 14.0f, 170.0f, 28.0f), juce::Justification::centredLeft, false);

        for (int i = 0; i < (int) presets::Category::count; ++i)
        {
            const auto r = categoryRect (i);
            const auto accent = theme::chainAccent (i % 8);
            const bool on = i == category;
            if (on)
            {
                g.setColour (accent.withAlpha (0.16f));
                g.fillRect (r);
                g.setColour (accent);
                g.fillRect (r.withWidth (3.0f));
            }
            g.setFont (fonts::semi (13.5f, 0.08f));
            const auto label = juce::String (presets::categoryNames[i]).toUpperCase();
            if (on)
                glowText (g, label, r.withTrimmedLeft (22.0f), accent, 0.2f);
            else
            {
                g.setColour (theme::textDim);
                g.drawText (label, r.withTrimmedLeft (22.0f).withTrimmedRight (36.0f), juce::Justification::centredLeft, true);
            }
            g.setColour (theme::textDim.withAlpha (0.7f));
            g.setFont (fonts::mono (11.5f));
            g.drawText (juce::String ((int) byCategory[(size_t) i].size()), r.withTrimmedRight (16.0f), juce::Justification::centredRight, false);
        }

        g.setColour (theme::text);
        g.setFont (fonts::bold (15.0f, 0.22f));
        g.drawText (juce::String (presets::categoryNames[category]).toUpperCase(),
                    juce::Rectangle<float> (gridX, 14.0f, 420.0f, 28.0f), juce::Justification::centredLeft, false);
        const auto xr = closeRect();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (xr, 4.0f);
        g.setColour (theme::text);
        g.drawLine (xr.getX() + 10.0f, xr.getY() + 10.0f, xr.getRight() - 10.0f, xr.getBottom() - 10.0f, 1.6f);
        g.drawLine (xr.getRight() - 10.0f, xr.getY() + 10.0f, xr.getX() + 10.0f, xr.getBottom() - 10.0f, 1.6f);

        const auto& bank = presets::factory();
        const auto accent = theme::chainAccent (category % 8);
        const auto& list = byCategory[(size_t) category];
        for (int n = 0; n < (int) list.size(); ++n)
        {
            const int idx = list[(size_t) n];
            const auto& p = bank[(size_t) idx];
            const auto r = tileRect (n);
            const bool on = idx == current, over = idx == hoverIdx;
            g.setColour (on ? accent.withAlpha (0.18f) : over ? theme::panelEdge.withAlpha (0.55f) : theme::panel.withAlpha (0.8f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (on ? accent : theme::panelEdge.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, on ? 1.4f : 1.0f);
            g.setColour (accent);
            g.setFont (fonts::mono (14.0f));
            g.drawText (juce::String (n + 1).paddedLeft ('0', 2), juce::Rectangle<float> (r.getX() + 8.0f, r.getY(), 24.0f, r.getHeight()),
                        juce::Justification::centredLeft, false);
            g.setColour (theme::text);
            g.setFont (fonts::semi (13.5f, 0.03f));
            g.drawText (p.name, juce::Rectangle<float> (r.getX() + 34.0f, r.getY() + 5.0f, r.getWidth() - 40.0f, 20.0f), juce::Justification::centredLeft, true);
            g.setColour (theme::textDim);
            g.setFont (fonts::mono (11.0f));
            g.drawText (engineSummary (p), juce::Rectangle<float> (r.getX() + 34.0f, r.getBottom() - 19.0f, r.getWidth() - 40.0f, 14.0f),
                        juce::Justification::centredLeft, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto p = toPanel (e.position);
        int h = -1;
        const auto& list = byCategory[(size_t) category];
        for (int n = 0; n < (int) list.size(); ++n)
            if (tileRect (n).contains (p))
                h = list[(size_t) n];
        if (h != hoverIdx)
        {
            hoverIdx = h;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverIdx != -1)
        {
            hoverIdx = -1;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = toPanel (e.position);
        if (! juce::Rectangle<float> (0.0f, 0.0f, panelW, panelH).contains (p) || closeRect().contains (p))
        {
            setVisible (false);
            return;
        }
        for (int i = 0; i < (int) presets::Category::count; ++i)
            if (categoryRect (i).contains (p))
            {
                category = i;
                hoverIdx = -1;
                repaint();
                return;
            }
        const auto& list = byCategory[(size_t) category];
        for (int n = 0; n < (int) list.size(); ++n)
            if (tileRect (n).contains (p))
            {
                current = list[(size_t) n];
                if (onPick)
                    onPick (current);
                repaint();
                return;
            }
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            setVisible (false);
            return true;
        }
        return false;
    }

private:
    static constexpr float panelW = 1040.0f, panelH = 590.0f, listW = 250.0f, gridX = 272.0f, gridTop = 60.0f;
    static constexpr int columns = 4;

    juce::Rectangle<float> panelBounds() const
    {
        return juce::Rectangle<float> (panelW * scale, panelH * scale).withCentre (getLocalBounds().toFloat().getCentre());
    }
    juce::Point<float> toPanel (juce::Point<float> p) const { return (p - panelBounds().getPosition()) / scale; }

    static juce::Rectangle<float> categoryRect (int i) { return { 0.0f, 60.0f + (float) i * 40.0f, listW, 40.0f }; }
    static juce::Rectangle<float> closeRect() { return { panelW - 50.0f, 14.0f, 30.0f, 30.0f }; }
    juce::Rectangle<float> tileRect (int n) const
    {
        const int rows = ((int) byCategory[(size_t) category].size() + columns - 1) / columns;
        const float step = juce::jmin (56.0f, (panelH - gridTop - 24.0f) / (float) juce::jmax (1, rows));
        const float gap = 10.0f, w = (panelW - gridX - 22.0f - gap * (float) (columns - 1)) / (float) columns;
        return { gridX + (float) (n % columns) * (w + gap), gridTop + (float) (n / columns) * step, w, step - 8.0f };
    }

    static void glowText (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c, float glow)
    {
        g.setColour (c.withAlpha (glow));
        for (auto d : { juce::Point<float> (-1, 0), juce::Point<float> (1, 0), juce::Point<float> (0, -1), juce::Point<float> (0, 1) })
            g.drawText (s, r.translated (d.x, d.y), juce::Justification::centredLeft, false);
        g.setColour (c);
        g.drawText (s, r, juce::Justification::centredLeft, false);
    }

    // The engines the preset uses, e.g. "jp · sub · wavetable"
    static juce::String engineSummary (const presets::Preset& p)
    {
        juce::StringArray ids;
        for (const auto& e : p.engines)
            if (e.isNotEmpty())
                ids.addIfNotAlreadyThere (e == "wavetable" ? "wave" : e == "spectral" ? "spec" : e == "genome" ? "gene" : e);
        return ids.joinIntoString (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    }

    std::array<std::vector<int>, (size_t) presets::Category::count> byCategory;
    int category = 0, current = 0, hoverIdx = -1;
    float scale = 1.0f;
};
}
