#pragma once
#include "Theme.h"
#include "../Presets.h"

namespace ps
{
// The preset field in the header: previous / next, and the name with its category, which opens the browser
class PresetBar : public juce::Component
{
public:
    // load: puts preset i of the factory bank in. currentName: the loaded preset's name. onBrowse: opens the
    // browser, which gets the current index from currentIndex()
    PresetBar (std::function<void (int)> load, std::function<juce::String()> currentName)
        : loadPreset (std::move (load)), nameOf (std::move (currentName))
    {
        for (auto* b : { &prev, &next })
            addAndMakeVisible (*b);
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        refresh();
    }

    std::function<void()> onBrowse;

    int currentIndex() const { return indexOf (nameOf()); }

    // Called from the editor's timer, so a state load or a host recall shows up
    void refresh()
    {
        const auto shown = nameOf();
        const auto& bank = presets::factory();
        const int i = indexOf (shown);
        const auto newName = shown.isEmpty() ? juce::String ("Init") : shown;
        // The category's first word: "Bass" for "Bass / Reese / Growl"
        const auto newCategory = i >= 0 ? juce::String (presets::categoryNames[(int) bank[(size_t) i].category]).upToFirstOccurrenceOf (" /", false, false).toUpperCase()
                                        : juce::String();
        const int newIndex = i >= 0 ? (int) bank[(size_t) i].category : 0;
        if (newName != displayName || newCategory != displayCategory || newIndex != categoryIndex)
        {
            displayName = newName;
            displayCategory = newCategory;
            categoryIndex = newIndex;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = nameArea().toFloat();
        g.setColour (theme::bgBottom);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (isMouseOver() ? theme::chainAccent (categoryIndex).withAlpha (0.8f) : theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setFont (fonts::mono (10.5f));
        const int catW = juce::GlyphArrangement::getStringWidthInt (fonts::mono (10.5f), displayCategory) + 4;
        g.setColour (theme::chainAccent (categoryIndex));
        g.drawText (displayCategory, r.withTrimmedRight (10.0f), juce::Justification::centredRight, false);
        g.setColour (theme::text);
        g.setFont (fonts::bold (14.0f, 0.04f));
        g.drawText (displayName, r.withTrimmedLeft (12.0f).withTrimmedRight ((float) catW + 20.0f), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int h = r.getHeight();
        prev.setBounds (r.removeFromLeft (h + 4));
        next.setBounds (r.removeFromRight (h + 4));
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && nameArea().contains (e.getPosition()) && onBrowse)
            onBrowse();
    }

private:
    juce::Rectangle<int> nameArea() const
    {
        const int h = getHeight();
        return getLocalBounds().withTrimmedLeft (h + 8).withTrimmedRight (h + 8);
    }

    static int indexOf (const juce::String& n)
    {
        const auto& bank = presets::factory();
        for (size_t i = 0; i < bank.size(); ++i)
            if (bank[i].name == n)
                return (int) i;
        return -1;
    }

    void step (int by)
    {
        const int size = (int) presets::factory().size();
        const int i = indexOf (nameOf());
        loadPreset (((i < 0 ? 0 : i) + by + size) % size);
        refresh();
    }

    std::function<void (int)> loadPreset;
    std::function<juce::String()> nameOf;
    juce::TextButton prev { "<" }, next { ">" };
    juce::String displayName, displayCategory;
    int categoryIndex = 0;
};
}
