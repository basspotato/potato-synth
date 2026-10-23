#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// The GUI's typefaces, from Potato-Kicker: Barlow Condensed for labels, Share Tech Mono for readouts
namespace ps::fonts
{
    struct Faces
    {
        juce::Typeface::Ptr semi = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize);
        juce::Typeface::Ptr bold = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedBold_ttf, BinaryData::BarlowCondensedBold_ttfSize);
        juce::Typeface::Ptr mono = juce::Typeface::createSystemTypefaceFor (BinaryData::ShareTechMonoRegular_ttf, BinaryData::ShareTechMonoRegular_ttfSize);
    };
    inline const Faces& faces() { static Faces f; return f; }

    // Barlow Condensed reads smaller than the system font at the same height: sizes are given as they were
    // for that font and scaled here
    constexpr float opticalSize = 1.25f;
    inline juce::FontOptions semi (float h, float kern = 0.0f) { return juce::FontOptions().withTypeface (faces().semi).withHeight (h * opticalSize).withKerningFactor (kern); }
    inline juce::FontOptions bold (float h, float kern = 0.0f) { return juce::FontOptions().withTypeface (faces().bold).withHeight (h * opticalSize).withKerningFactor (kern); }
    inline juce::FontOptions mono (float h) { return juce::FontOptions().withTypeface (faces().mono).withHeight (h); }
}
