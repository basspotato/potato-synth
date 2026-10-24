#pragma once
#include "Theme.h"
#include "../OutputScope.h"

namespace ps
{
// A stereo output level meter (-60 to 0 dB) with a peak hold, so you can see sound is playing. 30 Hz.
class LevelMeter : public juce::Component,
                   private juce::Timer
{
public:
    explicit LevelMeter (OutputScope& source) : scope (source) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float gap = 3.0f, bw = (r.getWidth() - gap) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto bar = juce::Rectangle<float> (r.getX() + (float) ch * (bw + gap), r.getY(), bw, r.getHeight());
            g.setColour (theme::bgBottom.withAlpha (0.8f));
            g.fillRoundedRectangle (bar, 3.0f);
            const float h = bar.getHeight() * toFraction (levelDb[(size_t) ch]);
            if (h > 0.5f)
            {
                const auto fill = bar.withTop (bar.getBottom() - h);
                g.setGradientFill (juce::ColourGradient (theme::mid, 0.0f, bar.getBottom(), theme::sub, 0.0f, bar.getY(), false));
                g.fillRoundedRectangle (fill, 3.0f);
            }
            g.setColour (theme::text.withAlpha (0.9f));
            g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * toFraction (holdDb[(size_t) ch]) - 1.0f, bar.getWidth(), 2.0f);
        }
    }

private:
    static float toFraction (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); }

    void timerCallback() override
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float db = juce::Decibels::gainToDecibels (scope.takePeak ((int) ch), -100.0f);
            levelDb[ch] = std::max (db, levelDb[ch] - 36.0f / 30.0f);   // falls 36 dB/s
            if (db >= holdDb[ch])
            {
                holdDb[ch] = db;
                holdFrames[ch] = 45;
            }
            else if (holdFrames[ch] > 0)
                --holdFrames[ch];
            else
                holdDb[ch] = std::max (db, holdDb[ch] - 20.0f / 30.0f);
        }
        repaint();
    }

    OutputScope& scope;
    std::array<float, 2> levelDb { -100.0f, -100.0f }, holdDb { -100.0f, -100.0f };
    std::array<int, 2> holdFrames {};
};
}
