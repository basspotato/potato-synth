#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Every APVTS parameter ID, range and default lives here. IDs are permanent once released:
// add new IDs, never rename or remove one.
namespace ps::params
{
    // 1: master + layer levels. 2: layer pan, tune and amp envelope. 3: voice mode and glide.
    // 4: layer roles, crossovers and mono bass. 5: safe output. 6: engine slots. 7: chain knobs and SUPER.
    // 8: BUILD, DROP, the XY pad, layer anchors, layer filter and drive, reverb and delay sends.
    // 9: the arpeggiator / step sequencer and the mod step lanes (their steps saved as "Steps" and "Lanes" children).
    // 10: HOLD, the main page's CUTOFF and RESO, pitch bend / MPE, mod wheel and aftertouch.
    // 11: layers 5-8.
    // Parameters missing from an older save load at their defaults.
    inline constexpr int stateVersion = 12;

    inline const juce::ParameterID master { "master", 1 };
    inline const juce::ParameterID crossoverLow { "xover_low", 4 };
    inline const juce::ParameterID crossoverHigh { "xover_high", 4 };
    inline const juce::ParameterID monoBass { "mono_bass", 4 };
    inline const juce::ParameterID safeOutput { "safe_output", 5 };
    inline const juce::ParameterID phaseAlign { "phase_align", 8 };   // Sub/body phase alignment strength (PhaseAlign.h)

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
    // Momentary performance buttons (ChainKnobs.h Performance)
    inline const juce::ParameterID build { "build", 8 };
    inline const juce::ParameterID drop { "drop", 8 };
    inline const juce::ParameterID hold { "hold", 10 };              // a latched sustain pedal
    inline const juce::ParameterID cutoff { "cutoff", 10 };          // stack low-pass sweep, percent (100 = open)
    inline const juce::ParameterID resonance { "reso", 10 };         // its resonance, percent
    // Expression (LayerStack.h): pitch bend, MPE, and where the mod wheel and aftertouch go (lane targets)
    inline const juce::ParameterID bendRange { "bend_range", 10 };   // semitones
    inline const juce::ParameterID mpe { "mpe", 10 };
    inline const juce::ParameterID mpeRange { "mpe_range", 10 };     // per-note bend range, semitones
    inline const juce::ParameterID modWheelTarget { "mw_target", 10 };
    inline const juce::ParameterID modWheelDepth { "mw_depth", 10 }; // percent, bipolar
    inline const juce::ParameterID aftertouchTarget { "at_target", 10 };
    inline const juce::ParameterID aftertouchDepth { "at_depth", 10 };
    // The stack XY pad (StackMorph.h): 0..1 each way, the centre is the full stack
    inline const juce::ParameterID padX { "xy_x", 8 };
    inline const juce::ParameterID padY { "xy_y", 8 };
    // The two shared sends (Space.h): plate/FDN reverb and tempo-synced delay. BLOOM moves the reverb on top.
    inline const juce::ParameterID reverbAmount { "fx_rev_amount", 8 };
    inline const juce::ParameterID reverbSize { "fx_rev_size", 8 };       // RT60, seconds
    inline const juce::ParameterID reverbDamping { "fx_rev_damp", 8 };    // percent
    inline const juce::ParameterID reverbDuck { "fx_rev_duck", 8 };       // percent
    inline const juce::ParameterID delayAmount { "fx_dly_amount", 8 };
    inline const juce::ParameterID delayTime { "fx_dly_time", 8 };        // DelayDivision index
    inline const juce::ParameterID delayFeedback { "fx_dly_feedback", 8 };
    inline const juce::ParameterID delayTone { "fx_dly_tone", 8 };        // feedback high cut, Hz
    inline const juce::ParameterID delayPingPong { "fx_dly_pingpong", 8 };
    inline const juce::ParameterID delayDuck { "fx_dly_duck", 8 };
    // The patch's master chain on the summed stack, before the sends (StackBus.h Pressure::processMaster, BusWidth)
    inline const juce::ParameterID ottAmount { "bus_ott", 12 };          // percent
    inline const juce::ParameterID ottTime { "bus_ott_time", 12 };       // percent of the default attack/release
    inline const juce::ParameterID ottUpward { "bus_ott_up", 12 };       // percent: how much the quiet parts are lifted
    inline const juce::ParameterID glueAmount { "bus_glue", 12 };        // dB of gain reduction
    inline const juce::ParameterID glueRelease { "bus_glue_rel", 12 };   // ms
    inline const juce::ParameterID stackWidth { "bus_width", 12 };       // percent of the side signal above 250 Hz
    inline const juce::ParameterID subDuck { "sub_duck", 12 };           // percent: the sub ducks the body's lows
    // The note generator (Arp.h): Off, Arp or Seq, on one step grid
    inline const juce::ParameterID arpMode { "arp_mode", 9 };
    inline const juce::ParameterID arpRate { "arp_rate", 9 };            // DelayDivision index
    inline const juce::ParameterID arpDirection { "arp_dir", 9 };
    inline const juce::ParameterID arpOctaves { "arp_oct", 9 };
    inline const juce::ParameterID arpGate { "arp_gate", 9 };            // percent of a step
    inline const juce::ParameterID arpSwing { "arp_swing", 9 };          // percent (100 = half a step)
    inline const juce::ParameterID arpLatch { "arp_latch", 9 };
    inline const juce::ParameterID arpLength { "arp_length", 9 };        // steps
    inline const juce::ParameterID seqTrigger { "seq_trigger", 9 };      // Key or Host
    // Mod step lanes (ModLanes.h): lane{n}_target / _depth / _rate / _length / _smooth; step values in the state
    inline constexpr int numModLanes = 4;
    inline juce::ParameterID laneParam (int lane, const char* name)
    {
        return { "lane" + juce::String (lane + 1) + "_" + name, 9 };
    }
    inline juce::ParameterID laneTarget (int i) { return laneParam (i, "target"); }
    inline juce::ParameterID laneDepth (int i)  { return laneParam (i, "depth"); }    // percent, bipolar
    inline juce::ParameterID laneRate (int i)   { return laneParam (i, "rate"); }     // DelayDivision index
    inline juce::ParameterID laneLength (int i) { return laneParam (i, "length"); }   // steps
    inline juce::ParameterID laneSmooth (int i) { return laneParam (i, "smooth"); }   // percent
    inline const juce::StringArray laneTargetNames { "Off", "MASS", "SCALE", "PRESSURE", "GROOVE", "CLARITY", "SHINE", "MORPH", "BLOOM",
                                                     "SUPER", "XY X", "XY Y", "CUTOFF" };
    inline const juce::StringArray arpModeNames { "Off", "Arp", "Seq" };
    inline const juce::StringArray arpDirectionNames { "Up", "Down", "Up-Down", "Random", "As Played", "Chord" };
    inline const juce::StringArray delayDivisionNames { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1" };
    inline constexpr double delayDivisionBeats[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };

    // Per-layer parameters follow the pattern L{n}_name with n = 1..8. Layers 5-8 arrived in version 11.
    inline juce::ParameterID layerParam (int layerIndex, const char* name, int version)
    {
        return { "L" + juce::String (layerIndex + 1) + "_" + name, layerIndex >= 4 ? juce::jmax (version, 11) : version };
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
    inline juce::ParameterID layerAnchor (int i)  { return layerParam (i, "anchor", 8); }   // XY pad leaves it at full
    // Per-layer filter (per voice, its own envelope) and drive (Layer.h)
    inline juce::ParameterID layerFilterType (int i)    { return layerParam (i, "flt_type", 8); }
    inline juce::ParameterID layerCutoff (int i)        { return layerParam (i, "flt_cutoff", 8); }
    inline juce::ParameterID layerResonance (int i)     { return layerParam (i, "flt_reso", 8); }
    inline juce::ParameterID layerFilterEnv (int i)     { return layerParam (i, "flt_env", 8); }
    inline juce::ParameterID layerKeyTrack (int i)      { return layerParam (i, "flt_key", 8); }
    inline juce::ParameterID layerFilterAttack (int i)  { return layerParam (i, "flt_attack", 8); }
    inline juce::ParameterID layerFilterDecay (int i)   { return layerParam (i, "flt_decay", 8); }
    inline juce::ParameterID layerFilterSustain (int i) { return layerParam (i, "flt_sustain", 8); }
    inline juce::ParameterID layerFilterRelease (int i) { return layerParam (i, "flt_release", 8); }
    inline juce::ParameterID layerDrive (int i)         { return layerParam (i, "drive", 8); }
    // Per-layer chorus insert (Inserts.h)
    inline juce::ParameterID layerChorus (int i)        { return layerParam (i, "chorus", 8); }
    inline juce::ParameterID layerChorusRate (int i)    { return layerParam (i, "chorus_rate", 8); }
    inline juce::ParameterID layerChorusDepth (int i)   { return layerParam (i, "chorus_depth", 8); }
    enum class FilterType { off, ladder24, lowPass12, bandPass, highPass };
    inline const juce::StringArray filterTypeNames { "Off", "Ladder 24", "LP 12", "BP", "HP" };

    // Engine slots: 16 generic 0..1 parameters per layer. Each engine says what its slots mean.
    inline constexpr int numEngineSlots = 24;
    inline juce::ParameterID layerSlot (int i, int slot)
    {
        return { "L" + juce::String (i + 1) + "_e" + juce::String (slot + 1), i >= 4 ? 11 : 6 };
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

    // All 8 layers exist from the first release, so the parameter list never changes; layers 5-8 start empty
    // (an empty layer costs no CPU) and get a role tag of Full until the user picks one.
    inline constexpr int numLayers = 8;
    inline const char* const layerRoles[numLayers] = { "Sub", "Body", "Mid", "Air", "Layer 5", "Layer 6", "Layer 7", "Layer 8" };

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
        layout.add (std::make_unique<juce::AudioParameterFloat> (phaseAlign, "Phase Align", juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })));

        auto percentText = juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
        for (int k = 0; k < numChainKnobs; ++k)
            layout.add (std::make_unique<juce::AudioParameterFloat> (chainKnob (k), chainKnobNames[k], juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentText));
        layout.add (std::make_unique<juce::AudioParameterFloat> (super, "SUPER", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentText));
        layout.add (std::make_unique<juce::AudioParameterBool> (build, "BUILD", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (drop, "DROP", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (hold, "HOLD", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (cutoff, "Cutoff", juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                const double hz = 180.0 * std::pow (20000.0 / 180.0, v * 0.01);
                return v >= 99.95f ? juce::String ("OPEN") : hz < 1000.0 ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz * 0.001, 2) + " kHz"; })));
        layout.add (std::make_unique<juce::AudioParameterInt> (bendRange, "Pitch Bend Range", 0, 48, 2));
        layout.add (std::make_unique<juce::AudioParameterBool> (mpe, "MPE", false));
        layout.add (std::make_unique<juce::AudioParameterInt> (mpeRange, "MPE Bend Range", 1, 96, 48));
        {
            auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; });
            // Mod wheel on MORPH by default: the wheel sweeps the sound's identity
            layout.add (std::make_unique<juce::AudioParameterChoice> (modWheelTarget, "Mod Wheel Target", laneTargetNames, 7));
            layout.add (std::make_unique<juce::AudioParameterFloat> (modWheelDepth, "Mod Wheel Depth", juce::NormalisableRange<float> (-100.0f, 100.0f), 100.0f, pct));
            layout.add (std::make_unique<juce::AudioParameterChoice> (aftertouchTarget, "Aftertouch Target", laneTargetNames, 0));
            layout.add (std::make_unique<juce::AudioParameterFloat> (aftertouchDepth, "Aftertouch Depth", juce::NormalisableRange<float> (-100.0f, 100.0f), 50.0f, pct));
        }
        layout.add (std::make_unique<juce::AudioParameterFloat> (resonance, "Resonance", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (padX, "XY X", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percentText));
        layout.add (std::make_unique<juce::AudioParameterFloat> (padY, "XY Y", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percentText));
        {
            using Float = juce::AudioParameterFloat;
            auto percent = juce::AudioParameterFloatAttributes().withLabel ("%")
                               .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; });
            juce::NormalisableRange<float> size (0.3f, 12.0f, 0.01f);
            size.setSkewForCentre (2.0f);
            juce::NormalisableRange<float> tone (1000.0f, 20000.0f, 1.0f);
            tone.setSkewForCentre (5000.0f);
            layout.add (std::make_unique<Float> (reverbAmount, "Reverb", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, percent));
            layout.add (std::make_unique<Float> (reverbSize, "Reverb Size", size, 2.2f,
                juce::AudioParameterFloatAttributes().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " s"; })));
            layout.add (std::make_unique<Float> (reverbDamping, "Reverb Damping", juce::NormalisableRange<float> (0.0f, 100.0f), 50.0f, percent));
            layout.add (std::make_unique<Float> (reverbDuck, "Reverb Duck", juce::NormalisableRange<float> (0.0f, 100.0f), 30.0f, percent));
            layout.add (std::make_unique<Float> (delayAmount, "Delay", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, percent));
            layout.add (std::make_unique<juce::AudioParameterChoice> (delayTime, "Delay Time", delayDivisionNames, 6));
            layout.add (std::make_unique<Float> (delayFeedback, "Delay Feedback", juce::NormalisableRange<float> (0.0f, 95.0f), 35.0f, percent));
            layout.add (std::make_unique<Float> (delayTone, "Delay Tone", tone, 6000.0f,
                juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
                    return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v * 0.001f, 1) + " kHz"; })));
            layout.add (std::make_unique<juce::AudioParameterBool> (delayPingPong, "Delay Ping-Pong", true));
            layout.add (std::make_unique<Float> (delayDuck, "Delay Duck", juce::NormalisableRange<float> (0.0f, 100.0f), 50.0f, percent));

            juce::NormalisableRange<float> ottTimeRange (25.0f, 400.0f, 1.0f);
            ottTimeRange.setSkewForCentre (100.0f);
            juce::NormalisableRange<float> glueReleaseRange (50.0f, 1000.0f, 1.0f);
            glueReleaseRange.setSkewForCentre (200.0f);
            auto ms = juce::AudioParameterFloatAttributes().withLabel ("ms")
                          .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; });
            layout.add (std::make_unique<Float> (ottAmount, "OTT", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, percent));
            layout.add (std::make_unique<Float> (ottTime, "OTT Time", ottTimeRange, 100.0f, percent));
            layout.add (std::make_unique<Float> (ottUpward, "OTT Upward", juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f, percent));
            layout.add (std::make_unique<Float> (glueAmount, "Glue", juce::NormalisableRange<float> (0.0f, 6.0f, 0.01f), 0.0f,
                juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
            layout.add (std::make_unique<Float> (glueRelease, "Glue Release", glueReleaseRange, 200.0f, ms));
            layout.add (std::make_unique<Float> (stackWidth, "Width", juce::NormalisableRange<float> (0.0f, 200.0f), 100.0f, percent));
            layout.add (std::make_unique<Float> (subDuck, "Sub Duck", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, percent));

            layout.add (std::make_unique<juce::AudioParameterChoice> (arpMode, "Arp Mode", arpModeNames, 0));
            layout.add (std::make_unique<juce::AudioParameterChoice> (arpRate, "Arp Rate", delayDivisionNames, 2));
            layout.add (std::make_unique<juce::AudioParameterChoice> (arpDirection, "Arp Direction", arpDirectionNames, 0));
            layout.add (std::make_unique<juce::AudioParameterInt> (arpOctaves, "Arp Octaves", 1, 4, 1));
            layout.add (std::make_unique<Float> (arpGate, "Arp Gate", juce::NormalisableRange<float> (5.0f, 200.0f), 60.0f, percent));
            layout.add (std::make_unique<Float> (arpSwing, "Arp Swing", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, percent));
            layout.add (std::make_unique<juce::AudioParameterBool> (arpLatch, "Arp Latch", false));
            layout.add (std::make_unique<juce::AudioParameterInt> (arpLength, "Arp Length", 1, 32, 16));
            layout.add (std::make_unique<juce::AudioParameterChoice> (seqTrigger, "Seq Trigger", juce::StringArray { "Key", "Host" }, 0));
            for (int l = 0; l < numModLanes; ++l)
            {
                const juce::String name = "Lane " + juce::String (l + 1);
                layout.add (std::make_unique<juce::AudioParameterChoice> (laneTarget (l), name + " Target", laneTargetNames, 0));
                layout.add (std::make_unique<Float> (laneDepth (l), name + " Depth", juce::NormalisableRange<float> (-100.0f, 100.0f), 50.0f, percent));
                layout.add (std::make_unique<juce::AudioParameterChoice> (laneRate (l), name + " Rate", delayDivisionNames, 2));
                layout.add (std::make_unique<juce::AudioParameterInt> (laneLength (l), name + " Length", 1, 32, 16));
                layout.add (std::make_unique<Float> (laneSmooth (l), name + " Smooth", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, percent));
            }
        }

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
            layout.add (std::make_unique<juce::AudioParameterChoice> (layerRole (i), role + " Role", roleNames, juce::jmin (i, (int) Role::full)));
            layout.add (std::make_unique<juce::AudioParameterBool> (layerAnchor (i), role + " Anchor", i == 0));   // the Sub holds the floor
            {
                auto percent = juce::AudioParameterFloatAttributes().withLabel ("%")
                                   .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; });
                juce::NormalisableRange<float> cutoffRange (20.0f, 20000.0f);
                cutoffRange.setSkewForCentre (1000.0f);
                layout.add (std::make_unique<juce::AudioParameterChoice> (layerFilterType (i), role + " Filter", filterTypeNames, 0));
                layout.add (std::make_unique<Float> (layerCutoff (i), role + " Cutoff", cutoffRange, 20000.0f,
                    juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
                        return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v * 0.001f, 2) + " kHz"; })));
                layout.add (std::make_unique<Float> (layerResonance (i), role + " Resonance", juce::NormalisableRange<float> (0.0f, 100.0f), 10.0f, percent));
                layout.add (std::make_unique<Float> (layerFilterEnv (i), role + " Filter Env", juce::NormalisableRange<float> (-48.0f, 48.0f, 0.1f), 0.0f,
                    juce::AudioParameterFloatAttributes().withLabel ("st")));
                layout.add (std::make_unique<Float> (layerKeyTrack (i), role + " Key Track", juce::NormalisableRange<float> (0.0f, 100.0f), 50.0f, percent));
                layout.add (std::make_unique<Float> (layerFilterAttack (i), role + " Filter Attack", detail::timeRange (0.0f, 10000.0f, 200.0f), 2.0f, detail::msAttributes()));
                layout.add (std::make_unique<Float> (layerFilterDecay (i), role + " Filter Decay", detail::timeRange (1.0f, 20000.0f, 500.0f), 600.0f, detail::msAttributes()));
                layout.add (std::make_unique<Float> (layerFilterSustain (i), role + " Filter Sustain", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percent));
                layout.add (std::make_unique<Float> (layerFilterRelease (i), role + " Filter Release", detail::timeRange (1.0f, 30000.0f, 500.0f), 300.0f, detail::msAttributes()));
                layout.add (std::make_unique<Float> (layerDrive (i), role + " Drive", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, percent));
                juce::NormalisableRange<float> rate (0.05f, 5.0f, 0.01f);
                rate.setSkewForCentre (0.5f);
                layout.add (std::make_unique<Float> (layerChorus (i), role + " Chorus", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, percent));
                layout.add (std::make_unique<Float> (layerChorusRate (i), role + " Chorus Rate", rate, 0.4f,
                    juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " Hz"; })));
                layout.add (std::make_unique<Float> (layerChorusDepth (i), role + " Chorus Depth", juce::NormalisableRange<float> (0.0f, 100.0f), 50.0f, percent));
            }
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
