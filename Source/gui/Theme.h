#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Early-design look: deep, colourful, replaceable later. All colours live here.
namespace ps::theme
{
    inline const juce::Colour bgTop       { 0xff140a2e };   // deep indigo
    inline const juce::Colour bgBottom    { 0xff07041a };
    inline const juce::Colour panel       { 0xff1d1240 };
    inline const juce::Colour panelEdge   { 0xff3a2a78 };
    inline const juce::Colour text        { 0xffece6ff };
    inline const juce::Colour textDim     { 0xff9a8fc8 };
    inline const juce::Colour knobBody    { 0xff241a4c };

    // One colour per layer role
    inline const juce::Colour sub         { 0xffff2d8a };   // magenta
    inline const juce::Colour body        { 0xffff8a1f };   // orange
    inline const juce::Colour mid         { 0xff21e0ff };   // cyan
    inline const juce::Colour air         { 0xffc79bff };   // violet
    inline const juce::Colour full        { 0xffd9d4f5 };   // silver: no band limit

    // Indexed by params::Role (Sub, Body, Mid, Air, Full)
    inline juce::Colour roleColour (int role)
    {
        const juce::Colour c[] = { sub, body, mid, air, full };
        return c[(size_t) juce::jlimit (0, 4, role)];
    }

    // Chain knob accents, cycling through a warm-to-cool spectrum
    inline juce::Colour chainAccent (int index)
    {
        static const juce::Colour c[] = { juce::Colour (0xffff2d8a), juce::Colour (0xffff5f4a),
                                          juce::Colour (0xffff8a1f), juce::Colour (0xffffd23a),
                                          juce::Colour (0xff46f2a0), juce::Colour (0xff21e0ff),
                                          juce::Colour (0xff6f8bff), juce::Colour (0xffc79bff) };
        return c[(size_t) juce::jlimit (0, 7, index)];
    }
}
