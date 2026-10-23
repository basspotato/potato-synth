#pragma once
#include "Theme.h"
#include "../Params.h"
#include "../MixHealth.h"
#include "../Inserts.h"

namespace ps
{
// The MIX page: the patch's master chain (OTT, glue, width, Sub Duck) and Mix Health (true peak, loudness,
// correlation, mono lows, masking, listen modes). Meters refresh at 30 Hz.
class MixPage : public juce::Component,
                private juce::Timer
{
public:
    // layerColour: a layer's role colour, for the dots under a masking light
    MixPage (juce::AudioProcessorValueTreeState& state, MixHealth& health, std::function<juce::Colour (int)> layerColour)
        : apvts (state), mixHealth (health), colourOf (std::move (layerColour))
    {
        struct Def { const char* name; juce::ParameterID id; juce::Colour colour; };
        const Def defs[] = { { "OTT", params::ottAmount, theme::mid }, { "OTT TIME", params::ottTime, theme::mid },
                             { "UPWARD", params::ottUpward, theme::mid }, { "GLUE", params::glueAmount, theme::body },
                             { "RELEASE", params::glueRelease, theme::body }, { "WIDTH", params::stackWidth, theme::air },
                             { "SUB DUCK", params::subDuck, theme::sub } };
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& k = knobs[i];
            k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
            k.slider.setColour (juce::Slider::rotarySliderFillColourId, defs[i].colour);
            addAndMakeVisible (k.slider);
            k.label.setText (defs[i].name, juce::dontSendNotification);
            k.label.setJustificationType (juce::Justification::centred);
            k.label.setFont (ps::fonts::bold (11.0f));
            k.label.setColour (juce::Label::textColourId, defs[i].colour.interpolatedWith (theme::text, 0.35f));
            addAndMakeVisible (k.label);
            k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, defs[i].id.getParamID(), k.slider);
            auto* p = apvts.getParameter (defs[i].id.getParamID());
            k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        }

        const char* const listenNames[] = { "NORMAL", "PHONE", "MONO CLUB" };
        for (int i = 0; i < (int) listen.size(); ++i)
        {
            auto& b = listen[(size_t) i];
            b.setButtonText (listenNames[i]);
            b.onClick = [this, i] { mixHealth.setListen ((MixHealth::Listen) i); timerCallback(); };
            addAndMakeVisible (b);
        }
        reset.setButtonText ("RESET");
        reset.onClick = [this] { mixHealth.requestReset(); };
        addAndMakeVisible (reset);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.75f), 14.0f, 1.5f);

        g.setColour (theme::text);
        g.setFont (ps::fonts::bold (20.0f));
        g.drawText ("MASTER CHAIN", titleArea, juce::Justification::centredLeft);
        g.drawText ("MIX HEALTH", healthTitle, juce::Justification::centredLeft);

        // Readouts
        const auto& v = mixHealth.readings();
        const float tpHold = v.truePeakHoldDb.load();
        auto cellText = [&g] (juce::Rectangle<int> cell, const juce::String& title, const juce::String& value, juce::Colour c)
        {
            g.setColour (theme::bgBottom.withAlpha (0.6f));
            g.fillRoundedRectangle (cell.toFloat(), 10.0f);
            g.setColour (theme::textDim);
            g.setFont (ps::fonts::bold (11.0f));
            g.drawText (title, cell.reduced (10, 6), juce::Justification::topLeft);
            g.setColour (c);
            g.setFont (ps::fonts::bold (24.0f));
            g.drawText (value, cell.reduced (10, 6), juce::Justification::bottomLeft);
        };
        auto db = [] (float v, const char* unit) { return v <= -99.0f ? juce::String ("--") : juce::String (v, 1) + " " + unit; };
        const auto good = juce::Colour (0xff46f2a0), warn = juce::Colour (0xffffd23a), bad = theme::sub;
        cellText (cells[0], "TRUE PEAK", db (shownPeak, "dBTP"), shownPeak > -1.0f ? bad : theme::text);
        cellText (cells[1], "PEAK HOLD", db (tpHold, "dBTP"), tpHold > -1.0f ? bad : good);
        cellText (cells[2], "SHORT-TERM", db (v.shortTermLufs.load(), "LUFS"), theme::text);
        cellText (cells[3], "INTEGRATED", db (v.integratedLufs.load(), "LUFS"), theme::text);
        cellText (cells[4], "MOMENTARY", db (v.momentaryLufs.load(), "LUFS"), theme::textDim);
        const float side = v.sideLowsDb.load();
        cellText (cells[5], "LOWS", side < -30.0f ? juce::String ("MONO") : "SIDE " + juce::String (side, 0) + " dB",
                  side < -30.0f ? good : side < -15.0f ? warn : bad);

        // Correlation: -1 (out of phase) .. +1 (mono)
        auto bar = correlationArea.toFloat();
        g.setColour (theme::textDim);
        g.setFont (ps::fonts::bold (11.0f));
        g.drawText ("CORRELATION", bar.removeFromTop (14.0f), juce::Justification::centredLeft);
        g.setColour (theme::bgBottom.withAlpha (0.7f));
        g.fillRoundedRectangle (bar, 6.0f);
        const float corr = v.correlation.load();
        const float x = bar.getX() + (corr * 0.5f + 0.5f) * bar.getWidth();
        g.setColour (corr < 0.0f ? bad : corr < 0.3f ? warn : good);
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (x, bar.getCentreX()), bar.getY() + 3.0f,
                                                        std::abs (x - bar.getCentreX()) + 2.0f, bar.getHeight() - 6.0f), 3.0f);
        g.setColour (theme::text.withAlpha (0.5f));
        g.drawVerticalLine ((int) bar.getCentreX(), bar.getY(), bar.getBottom());

        // Masking: one light per band, darker for the low bands and lighter going up. A light glows while two or
        // more layers contend in its band, with a dot in each one's role colour, and holds for a second after
        // (timerCallback), so it reads at a glance instead of flickering.
        auto area = maskArea;
        g.setColour (theme::textDim);
        g.setFont (ps::fonts::bold (11.0f));
        g.drawText ("MASKING", area.removeFromTop (16), juce::Justification::centredLeft);
        static const char* const bands[] = { "200 Hz", "400 Hz", "800 Hz", "1.6 kHz", "3.2 kHz" };
        const int bw = area.getWidth() / MixHealth::numMaskBands;
        const float d = (float) juce::jlimit (12, 30, juce::jmin (bw / 3, area.getHeight() - 40));
        for (int b = 0; b < MixHealth::numMaskBands; ++b)
        {
            auto cell = area.withX (area.getX() + b * bw).withWidth (bw).toFloat();
            const auto colour = maskColour (b);
            const float glow = maskGlow[(size_t) b];
            const auto centre = juce::Point<float> (cell.getCentreX(), cell.getY() + 6.0f + d * 0.5f);
            for (int i = 3; i >= 1; --i)
            {
                const float rr = d * 0.5f + (float) i * 5.0f;
                g.setColour (colour.withAlpha (0.12f * glow * (float) (4 - i)));
                g.fillEllipse (centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f);
            }
            g.setColour (colour.withMultipliedBrightness (0.25f).interpolatedWith (colour, glow));
            g.fillEllipse (centre.x - d * 0.5f, centre.y - d * 0.5f, d, d);
            g.setColour (colour.withAlpha (0.5f + 0.5f * glow));
            g.drawEllipse (centre.x - d * 0.5f, centre.y - d * 0.5f, d, d, 1.5f);

            g.setColour (glow > 0.0f ? theme::text : theme::textDim);
            g.setFont (ps::fonts::bold (11.0f));
            const float labelY = centre.y + d * 0.5f + 6.0f;
            g.drawText (bands[b], juce::Rectangle<float> (cell.getX(), labelY, cell.getWidth(), 14.0f), juce::Justification::centred);

            // The contending layers
            const uint32_t bits = maskShown[(size_t) b];
            int count = 0;
            for (int l = 0; l < params::numLayers; ++l)
                count += (int) ((bits >> l) & 1u);
            float dotX = cell.getCentreX() - (float) count * 5.0f + 1.0f;
            for (int l = 0; l < params::numLayers; ++l)
                if ((bits >> l) & 1u)
                {
                    g.setColour (colourOf (l).withAlpha (glow));
                    g.fillEllipse (dotX, labelY + 18.0f, 8.0f, 8.0f);
                    dotX += 10.0f;
                }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 12);
        titleArea = r.removeFromTop (28);
        auto row = r.removeFromTop (juce::jmin (120, r.getHeight() / 3));
        const int w = row.getWidth() / (int) knobs.size();
        for (auto& k : knobs)
        {
            auto cell = row.removeFromLeft (w).reduced (4, 0);
            k.label.setBounds (cell.removeFromTop (14));
            k.slider.setBounds (cell);
        }
        r.removeFromTop (12);
        healthTitle = r.removeFromTop (28);
        auto buttons = healthTitle.withTrimmedLeft (healthTitle.getWidth() / 3);
        reset.setBounds (buttons.removeFromRight (80).reduced (2, 0));
        buttons.removeFromRight (12);
        const int bw = buttons.getWidth() / (int) listen.size();
        for (auto& b : listen)
            b.setBounds (buttons.removeFromLeft (bw).reduced (2, 0));
        r.removeFromTop (8);
        auto grid = r.removeFromTop (juce::jmin (130, r.getHeight() / 2));
        const int cw = grid.getWidth() / 3, ch = grid.getHeight() / 2;
        for (int i = 0; i < 6; ++i)
            cells[(size_t) i] = juce::Rectangle<int> (grid.getX() + (i % 3) * cw, grid.getY() + (i / 3) * ch, cw, ch).reduced (3);
        r.removeFromTop (8);
        correlationArea = r.removeFromTop (34);
        r.removeFromTop (8);
        maskArea = r;
    }

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override
    {
        // The peak readout falls 20 dB/s from the highest it has seen
        shownPeak = std::max (shownPeak - 20.0f / 30.0f, mixHealth.takeTruePeakDb());
        const int mode = (int) mixHealth.getListen();
        for (int i = 0; i < (int) listen.size(); ++i)
        {
            listen[(size_t) i].setColour (juce::TextButton::buttonColourId, i == mode ? (i == 0 ? theme::mid : theme::sub) : theme::panel);
            listen[(size_t) i].setAlpha (i == mode ? 1.0f : 0.7f);
        }
        // Masking lights: on while a band is contended, held 1 s after, then fading over 0.5 s. A layer's dot stays
        // 1 s after it last contended, so the dots settle instead of hopping with every block
        const auto& v = mixHealth.readings();
        for (int b = 0; b < MixHealth::numMaskBands; ++b)
        {
            const uint32_t bits = v.maskedLayers[(size_t) b].load();
            uint32_t shown = 0;
            for (int l = 0; l < params::numLayers; ++l)
            {
                auto& hold = layerHold[(size_t) b][(size_t) l];
                if ((bits >> l) & 1u)
                    hold = 30;
                else if (hold > 0)
                    --hold;
                if (hold > 0)
                    shown |= 1u << l;
            }
            maskShown[(size_t) b] = shown;
            if (bits != 0)
                maskGlow[(size_t) b] = 1.0f;
            else if (shown == 0)
                maskGlow[(size_t) b] = std::max (0.0f, maskGlow[(size_t) b] - 1.0f / 15.0f);
        }
        repaint (healthArea());
    }

    static juce::Colour maskColour (int band)
    {
        static const juce::Colour c[] = { juce::Colour (0xff9a1050), juce::Colour (0xffd8324a), juce::Colour (0xffff9a2e),
                                          juce::Colour (0xff6fe8ff), juce::Colour (0xfff0eaff) };
        return c[(size_t) juce::jlimit (0, 4, band)];
    }

    juce::Rectangle<int> healthArea() const { return getLocalBounds().withTop (healthTitle.getBottom()); }

    juce::AudioProcessorValueTreeState& apvts;
    MixHealth& mixHealth;
    std::function<juce::Colour (int)> colourOf;
    std::array<float, MixHealth::numMaskBands> maskGlow {};
    std::array<std::array<int, params::numLayers>, MixHealth::numMaskBands> layerHold {};
    std::array<uint32_t, MixHealth::numMaskBands> maskShown {};
    std::array<Knob, 7> knobs;
    std::array<juce::TextButton, 3> listen;
    juce::TextButton reset;
    juce::Rectangle<int> titleArea, healthTitle, correlationArea, maskArea;
    std::array<juce::Rectangle<int>, 6> cells;
    float shownPeak = -100.0f;
};
}
