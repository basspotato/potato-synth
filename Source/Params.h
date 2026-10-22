#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Every APVTS parameter ID, range and default lives here. IDs are permanent once released:
// add new IDs, never rename or remove one.
namespace ps::params
{
    // 1: master + layer levels. 2: layer pan, tune and amp envelope. 3: voice mode and glide.
    // Parameters missing from an older save load at their defaults.
    inline constexpr int stateVersion = 3;

    inline const juce::ParameterID master { "master", 1 };

    // Per-layer parameters follow the pattern L{n}_name with n = 1..4 (expandable to 8)
    inline juce::ParameterID layerParam (int layerIndex, const char* name, int version)
    {
        return { "L" + juce::String (layerIndex + 1) + "_" + name, version };
    }
    inline juce::ParameterID layerLevel (int i)   { return layerParam (i, "level", 1); }
    inline juce::ParameterID layerPan (int i)     { return layerParam (i, "pan", 2); }
    inline juce::ParameterID layerOctave (int i)  { return layerParam (i, "oct", 2); }
    inline juce::ParameterID layerSemi (int i)    { return layerParam (i, "semi", 2); }
    inline juce::ParameterID layerFine (int i)    { return layerParam (i, "fine", 2); }
    inline juce::ParameterID layerAttack (int i)  { return layerParam (i, "attack", 2); }
    inline juce::ParameterID layerDecay (int i)   { return layerParam (i, "decay", 2); }
    inline juce::ParameterID layerSustain (int i) { return layerParam (i, "sustain", 2); }
    inline juce::ParameterID layerRelease (int i) { return layerParam (i, "release", 2); }
    inline juce::ParameterID layerVoiceMode (int i) { return layerParam (i, "voice", 3); }
    inline juce::ParameterID layerGlide (int i)   { return layerParam (i, "glide", 3); }

    // Order is permanent: it is saved as an index.
    enum class VoiceMode { poly, mono, legato };
    inline const juce::StringArray voiceModeNames { "Poly", "Mono", "Legato" };

    inline constexpr int numLayers = 4;
    inline const char* const layerRoles[numLayers] = { "Sub", "Body", "Mid", "Air" };

    namespace detail
    {
        inline juce::NormalisableRange<float> timeRange (float minMs, float maxMs, float centreMs)
        {
            juce::NormalisableRange<float> r (minMs, maxMs, 0.01f);
            r.setSkewForCentre (centreMs);
            return r;
        }

        inline juce::AudioParameterFloatAttributes msAttributes()
        {
            return juce::AudioParameterFloatAttributes().withLabel ("ms")
                .withStringFromValueFunction ([] (float v, int) {
                    return v >= 1000.0f ? juce::String (v * 0.001f, 2) + " s" : juce::String (v, v < 10.0f ? 2 : 0) + " ms"; })
                .withValueFromStringFunction ([] (const juce::String& t) {
                    const auto v = t.getFloatValue();
                    return t.trimEnd().endsWithIgnoreCase ("s") && ! t.trimEnd().endsWithIgnoreCase ("ms") ? v * 1000.0f : v; });
        }
    }

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

        for (int i = 0; i < numLayers; ++i)
        {
            const juce::String role (layerRoles[i]);
            using Float = juce::AudioParameterFloat;
            using Int = juce::AudioParameterInt;

            layout.add (std::make_unique<Float> (layerPan (i), role + " Pan", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                    const int p = juce::roundToInt (v * 100.0f);
                    return p == 0 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (std::abs (p)); })));
            layout.add (std::make_unique<Int> (layerOctave (i), role + " Octave", -3, 3, 0));
            layout.add (std::make_unique<Int> (layerSemi (i), role + " Semitone", -12, 12, 0));
            layout.add (std::make_unique<Float> (layerFine (i), role + " Fine", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                juce::AudioParameterFloatAttributes().withLabel ("ct")
                    .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " ct"; })));
            layout.add (std::make_unique<Float> (layerAttack (i), role + " Attack", detail::timeRange (0.5f, 20000.0f, 150.0f), 2.0f,
                detail::msAttributes()));
            layout.add (std::make_unique<Float> (layerDecay (i), role + " Decay", detail::timeRange (1.0f, 20000.0f, 400.0f), 400.0f,
                detail::msAttributes()));
            layout.add (std::make_unique<Float> (layerSustain (i), role + " Sustain", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                juce::AudioParameterFloatAttributes().withLabel ("%")
                    .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " %"; })));
            layout.add (std::make_unique<Float> (layerRelease (i), role + " Release", detail::timeRange (1.0f, 30000.0f, 500.0f), 300.0f,
                detail::msAttributes()));
            layout.add (std::make_unique<juce::AudioParameterChoice> (layerVoiceMode (i), role + " Voice Mode", voiceModeNames, 0));
            layout.add (std::make_unique<Float> (layerGlide (i), role + " Glide", detail::timeRange (0.0f, 5000.0f, 250.0f), 0.0f,
                detail::msAttributes()));
        }

        return layout;
    }
}
