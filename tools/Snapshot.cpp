// Renders the plugin editor to a PNG without a host, so GUI work can be checked headlessly.
// Usage: PotatoSynthSnapshot out.png [width] [page]   (page: the start of a tab button's text, e.g. LAYER)
#include "PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshot.png"));

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());
    if (argc > 2)
    {
        const int w = juce::String (argv[2]).getIntValue();
        editor->setSize (w, w * editor->getHeight() / editor->getWidth());
    }

    if (argc > 3)
        for (auto* child : editor->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText().startsWith (argv[3]) && b->onClick)
                b->onClick();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    editor.reset();

    juce::FileOutputStream stream (out);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
        return 1;
    return 0;
}
