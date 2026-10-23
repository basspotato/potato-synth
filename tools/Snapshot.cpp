// Renders the plugin editor to a PNG without a host, so GUI work can be checked headlessly.
// Usage: PotatoSynthSnapshot out.png [width] [page] [height]   (page: the start of a button's text, comma-separated to click several, e.g. LAYER,FILTER;
// "BROWSE" opens the preset browser, "PRESET=Name" loads a factory preset first)
#include "PluginProcessor.h"
#include "gui/PresetBar.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshot.png"));

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());
    if (argc > 2)
    {
        const int w = juce::String (argv[2]).getIntValue();
        editor->setSize (w, argc > 4 ? juce::String (argv[4]).getIntValue() : w * editor->getHeight() / editor->getWidth());
    }

    // Clicks each comma-separated button in turn, searching the whole editor (e.g. LAYER,FILTER)
    std::function<bool (juce::Component&, const juce::String&)> click = [&click] (juce::Component& c, const juce::String& text)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* bar = dynamic_cast<ps::PresetBar*> (child); bar != nullptr && text == "BROWSE" && bar->onBrowse)
            {
                bar->onBrowse();
                return true;
            }
            if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText().startsWith (text) && b->onClick)
            {
                b->onClick();
                return true;
            }
            if (click (*child, text))
                return true;
        }
        return false;
    };
    if (argc > 3)
        for (auto& text : juce::StringArray::fromTokens (argv[3], ",", {}))
        {
            if (text.startsWith ("PRESET="))
            {
                for (const auto& preset : ps::presets::factory())
                    if (preset.name == text.fromFirstOccurrenceOf ("=", false, false))
                        static_cast<PotatoSynthProcessor&> (*processor).loadPreset (preset);
                continue;
            }
            click (*editor, text);
        }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    editor.reset();

    juce::FileOutputStream stream (out);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
        return 1;
    return 0;
}
