// Renders the plugin editor to a PNG without a host, so GUI work can be checked headlessly.
// Usage: PotatoSynthSnapshot out.png [width]
#include "PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshot.png"));

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorIfNeeded());
    if (argc > 2)
    {
        const int w = juce::String (argv[2]).getIntValue();
        editor->setSize (w, w * editor->getHeight() / editor->getWidth());
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    editor.reset();

    juce::FileOutputStream stream (out);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
        return 1;
    return 0;
}
