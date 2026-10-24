#pragma once
#include "Theme.h"
#include "../OutputScope.h"

namespace ps
{
// A plain oscilloscope of the output: it triggers on a rising zero crossing so a held note stands still, and
// scales slowly to the signal so quiet sounds still show. Repaints at 30 Hz.
class WaveformView : public juce::Component,
                     private juce::Timer
{
public:
    explicit WaveformView (OutputScope& source) : scope (source) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::bgBottom.withAlpha (0.7f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (theme::panelEdge.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        const auto area = r.reduced (10.0f, 8.0f);
        g.setColour (theme::panelEdge.withAlpha (0.5f));
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

        if (area.getWidth() < 4.0f)
            return;
        const int w = juce::jmax (2, (int) area.getWidth());
        juce::Path path;
        for (int x = 0; x < w; ++x)
        {
            // Each pixel shows the extremes of its slice, so a dense high note doesn't alias into noise
            const int a = x * window / w, b = juce::jmax (a + 1, (x + 1) * window / w);
            float lo = shown[(size_t) a], hi = lo;
            for (int i = a; i < b; ++i)
            {
                lo = std::min (lo, shown[(size_t) i]);
                hi = std::max (hi, shown[(size_t) i]);
            }
            const float px = area.getX() + (float) x;
            const float yLo = area.getCentreY() - hi * scale * area.getHeight() * 0.5f;
            const float yHi = area.getCentreY() - lo * scale * area.getHeight() * 0.5f;
            if (x == 0)
                path.startNewSubPath (px, yLo);
            path.lineTo (px, yLo);
            path.lineTo (px, std::max (yHi, yLo + 0.5f));
        }
        const auto colour = theme::mid;
        g.setColour (colour.withAlpha (0.22f));
        g.strokePath (path, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
    }

private:
    static constexpr int window = 2048;

    void timerCallback() override
    {
        std::array<float, OutputScope::size> latest;
        scope.snapshot (latest.data(), OutputScope::size);
        // The first rising zero crossing in the older half, so a full window follows it
        int start = 0;
        for (int i = 1; i < OutputScope::size - window; ++i)
            if (latest[(size_t) (i - 1)] < 0.0f && latest[(size_t) i] >= 0.0f)
            {
                start = i;
                break;
            }
        float peak = 0.0f;
        for (int i = 0; i < window; ++i)
        {
            shown[(size_t) i] = latest[(size_t) (start + i)];
            peak = std::max (peak, std::abs (shown[(size_t) i]));
        }
        const float want = 0.9f / juce::jmax (peak, 0.05f);
        scale += (want - scale) * (want < scale ? 0.5f : 0.08f);
        repaint();
    }

    OutputScope& scope;
    std::array<float, window> shown {};
    float scale = 1.0f;
};
}
