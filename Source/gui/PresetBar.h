#pragma once
#include "Theme.h"
#include "../Presets.h"

namespace ps
{
// The preset browser in the header: previous / next, and the name, which opens the bank by category
class PresetBar : public juce::Component
{
public:
    // load: puts preset i of the factory bank in. currentName: the loaded preset's name.
    PresetBar (std::function<void (int)> load, std::function<juce::String()> currentName)
        : loadPreset (std::move (load)), nameOf (std::move (currentName))
    {
        for (auto* b : { &prev, &next, &name })
            addAndMakeVisible (*b);
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };
        name.onClick = [this] { showMenu(); };
        refresh();
    }

    // Called from the editor's timer, so a state load or a host recall shows up
    void refresh()
    {
        const auto shown = nameOf();
        const auto& bank = presets::factory();
        const int i = indexOf (shown);
        const auto text = i > 0 ? juce::String (presets::categoryNames[(int) bank[(size_t) i].category]).toUpperCase() + "  |  " + shown
                                 : (shown.isEmpty() ? juce::String ("Init") : shown);
        if (name.getButtonText() != text)
            name.setButtonText (text);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int h = r.getHeight();
        prev.setBounds (r.removeFromLeft (h + 4));
        next.setBounds (r.removeFromRight (h + 4));
        name.setBounds (r.reduced (4, 0));
    }

private:
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

    void showMenu()
    {
        const auto& bank = presets::factory();
        const int current = indexOf (nameOf());
        juce::PopupMenu menu;
        for (int c = 0; c < (int) presets::Category::count; ++c)
        {
            juce::PopupMenu sub;
            for (size_t i = 0; i < bank.size(); ++i)
                if ((int) bank[i].category == c)
                    sub.addItem ((int) i + 1, bank[i].name, true, (int) i == current);
            if (c == (int) presets::Category::init)
                menu.addItem (1, bank[0].name, true, current == 0);
            else
                menu.addSubMenu (presets::categoryNames[c], sub, true, nullptr,
                                 current >= 0 && (int) bank[(size_t) current].category == c);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name),
                            [safe = juce::Component::SafePointer<PresetBar> (this)] (int chosen)
                            {
                                if (safe != nullptr && chosen > 0)
                                {
                                    safe->loadPreset (chosen - 1);
                                    safe->refresh();
                                }
                            });
    }

    std::function<void (int)> loadPreset;
    std::function<juce::String()> nameOf;
    juce::TextButton prev { "<" }, next { ">" }, name;
};
}
