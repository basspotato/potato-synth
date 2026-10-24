#pragma once
#include "Params.h"
#include "Arp.h"
#include <array>
#include <vector>

// The factory bank. A preset is the init state plus its own engines and parameter values: loading one sets
// every parameter to its default, puts the preset's engines in (each starting from its own slot defaults), then
// applies the preset's values in order. Presets are never host programs (PluginProcessor.h).
namespace ps::presets
{
    // The enum order is only an index (the browser's display order is categoryOrder), but new categories are
    // appended before count
    enum class Category { init, lead, bass, pad, pluck, keys, arp, fx, subBass, drone, chords, count };
    inline const char* const categoryNames[] = { "Init", "Supersaw / Lead", "Bass", "Pad / Atmos", "Pluck / Stab",
                                                 "Keys", "Arp / Sequence", "FX / Riser / Impact", "Sub / Low Bass", "Drones", "Chords" };
    // Top to bottom in the browser
    inline constexpr Category categoryOrder[] = { Category::init, Category::lead, Category::bass, Category::subBass, Category::arp,
                                                  Category::keys, Category::chords, Category::pluck, Category::drone, Category::pad, Category::fx };

    struct Preset
    {
        juce::String name;
        Category category = Category::init;
        bool solo = false;   // a deliberate solo sound: Potato Ready doesn't ask it for three role layers
        std::array<juce::String, params::numLayers> engines;
        std::vector<std::pair<juce::String, float>> values;   // parameter ID, value in the parameter's own units
        std::vector<NoteGenerator::Step> steps;              // the arp / sequencer pattern; empty keeps the default

        Preset& engine (int layer, const char* id, params::Role role)
        {
            engines[(size_t) layer] = id;
            return set (params::layerRole (layer), (float) role);
        }
        Preset& set (const juce::ParameterID& id, float value)
        {
            values.push_back ({ id.getParamID(), value });
            return *this;
        }
        // An engine slot, normalised 0..1
        Preset& slot (int layer, int slot, float value) { return set (params::layerSlot (layer, slot), value); }
    };

    // The whole bank, Init first. Built once, on first use.
    const std::vector<Preset>& factory();

    // The preset `by` places after bank index `from` (-1 or any) in browser order: categoryOrder, then bank order. Wraps.
    int stepInBrowserOrder (int from, int by);

    // A preset's calibrated level trim in dB (0 if it has none): master up to +6, the rest on its layer levels
    float trimDb (const juce::String& name);
}
