#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Every APVTS parameter ID, range and default lives here. IDs are permanent once released:
// add new IDs, never rename or remove one.
namespace ps::params
{
    // 1: master + layer levels. 2: layer pan, tune and amp envelope. 3: voice mode and glide.
    // 4: layer roles, crossovers and mono bass. 5: safe output. 6: engine slots. 7: chain knobs and SUPER.
    // Parameters missing from an older save load at their defaults.
    inline constexpr int stateVersion = 7;

    inline const juce::ParameterID master { "master", 1 };
    inline const juce::ParameterID crossoverLow { "xover_low", 4 };
    inline const juce::ParameterID crossoverHigh { "xover_high", 4 };
    inline const juce::ParameterID monoBass { "mono_bass", 4 };
    inline const juce::ParameterID safeOutput { "safe_output", 5 };

    // The 8 chain knobs (ChainKnobs.h), in chain::Knob order, and SUPER above them
    inline constexpr int numChainKnobs = 8;
    inline juce::ParameterID chainKnob (int k)
    {
        static const char* const ids[numChainKnobs] = { "chain_mass", "chain_scale", "chain_pressure", "chain_groove",
                                                        "chain_clarity", "chain_shine", "chain_morph", "chain_bloom" };
        return { ids[k], 7 };
    }
    inline const char* const chainKnobNames[numChainKnobs] = { "MASS", "SCALE", "PRESSURE", "GROOVE", "CLARITY", "SHINE", "MORPH", "BLOOM" };
    inline const juce::ParameterID super { "super", 7 };

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
    inline juce::ParameterID layerRole (int i)    { return layerParam (i, "role", 4); }

    // Engine slots: 16 generic 0..1 parameters per layer. Each engine says what its slots mean.
    inline constexpr int numEngineSlots = 24;
    inline juce::ParameterID layerSlot (int i, int slot)
    {
        return { "L" + juce::String (i + 1) + "_e" + juce::String (slot + 1), 6 };
    }

    // Display text for a slot value, supplied by the processor from the layer's current engine
    using SlotText = std::function<juce::String (int layer, int slot, float normalised)>;

    // Role tags set a layer's band. Order is permanent: it is saved as an index.
    //   Sub: low-pass at the low crossover.   Body: low crossover to high crossover.
    //   Mid: high-pass at twice the low crossover (about 250 Hz, where stereo width starts).
    //   Air: high-pass at the high crossover.   Full: no band limit.
    enum class Role { sub, body, mid, air, full };
    inline const juce::StringArray roleNames { "Sub", "Body", "Mid", "Air", "Full" };

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

        inline juce::String formatMs (float v)
        {
            return v >= 1000.0f ? juce::String (v * 0.001f, 2) + " s" : juce::String (v, v < 10.0f ? 2 : 0) + " ms";
        }

        inline juce::AudioParameterFloatAttributes msAttributes()
        {
            return juce::AudioParameterFloatAttributes().withLabel ("ms")
                .withStringFromValueFunction ([] (float v, int) { return formatMs (v); })
                .withValueFromStringFunction ([] (const juce::String& t) {
                    const auto v = t.getFloatValue();
                    return t.trimEnd().endsWithIgnoreCase ("s") && ! t.trimEnd().endsWithIgnoreCase ("ms") ? v * 1000.0f : v; });
        }
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout (SlotText slotText = {})
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            master, "Master",
            juce::NormalisableRange<float> (-48.0f, 6.0f, 0.01f, 2.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        auto hz = [] (float v, int) { return v >= 1000.0f ? juce::String (v * 0.001f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
        juce::NormalisableRange<float> lowRange (40.0f, 300.0f, 0.1f);
        lowRange.setSkewForCentre (120.0f);
        juce::NormalisableRange<float> highRange (1000.0f, 8000.0f, 1.0f);
        highRange.setSkewForCentre (2500.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (crossoverLow, "Low Crossover", lowRange, 120.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hz)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (crossoverHigh, "High Crossover", highRange, 2500.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hz)));
        layout.add (std::make_unique<juce::AudioParameterBool> (monoBass, "Mono Bass", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (safeOutput, "Safe Output", true));

        auto percentText = juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
        for (int k = 0; k < numChainKnobs; ++k)
            layout.add (std::make_unique<juce::AudioParameterFloat> (chainKnob (k), chainKnobNames[k], juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentText));
        layout.add (std::make_unique<juce::AudioParameterFloat> (super, "SUPER", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentText));

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
            layout.add (std::make_unique<juce::AudioParameterChoice> (layerRole (i), role + " Role", roleNames, i));
            for (int k = 0; k < numEngineSlots; ++k)
                layout.add (std::make_unique<Float> (layerSlot (i, k), role + " Engine " + juce::String (k + 1),
                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction ([slotText, i, k] (float v, int) {
                        return slotText ? slotText (i, k, v) : juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));
            layout.add (std::make_unique<Float> (layerGlide (i), role + " Glide", detail::timeRange (0.0f, 5000.0f, 250.0f), 0.0f,
                detail::msAttributes().withStringFromValueFunction ([] (float v, int) {
                    return v < 0.05f ? juce::String ("OFF") : detail::formatMs (v); })));
        }

        return layout;
    }
}
