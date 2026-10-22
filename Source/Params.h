#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Every APVTS parameter ID, range and default lives here. IDs are permanent once released:
// add new IDs, never rename or remove one.
namespace ps::params
{
    inline constexpr int stateVersion = 1;

    inline const juce::ParameterID master { "master", 1 };

    // Per-layer parameters follow the pattern L{n}_name with n = 1..4 (expandable to 8)
    inline juce::ParameterID layerLevel (int layerIndex) { return { "L" + juce::String (layerIndex + 1) + "_level", 1 }; }

    inline constexpr int numLayers = 4;
    inline const char* const layerRoles[numLayers] = { "Sub", "Body", "Mid", "Air" };

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            master, "Master",
            juce::NormalisableRange<float> (-48.0f, 6.0f, 0.01f, 2.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        for (int i = 0; i < numLayers; ++i)
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                layerLevel (i), juce::String (layerRoles[i]) + " Level",
                juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f, 2.5f), 0.0f,
                juce::AudioParameterFloatAttributes().withLabel ("dB")
                    .withStringFromValueFunction ([] (float v, int) {
                        return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1); })));

        return layout;
    }
}
