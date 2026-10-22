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
    // layerName: the name shown for a layer in masking warnings
    MixPage (juce::AudioProcessorValueTreeState& state, MixHealth& health, std::function<juce::String (int)> layerName)
        : apvts (state), mixHealth (health), nameOf (std::move (layerName))
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
            k.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
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
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
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
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (title, cell.reduced (10, 6), juce::Justification::topLeft);
            g.setColour (c);
            g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
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
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
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

        // Masking warnings
        auto list = maskArea;
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("MASKING", list.removeFromTop (16), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (13.0f));
        bool any = false;
        static const char* const bands[] = { "200 Hz", "400 Hz", "800 Hz", "1.6 kHz", "3.2 kHz" };
        for (int b = 0; b < MixHealth::numMaskBands; ++b)
        {
            const uint32_t bits = v.maskedLayers[(size_t) b].load();
            if (bits == 0)
                continue;
            juce::StringArray names;
            for (int l = 0; l < params::numLayers; ++l)
                if ((bits >> l) & 1u)
                    names.add (nameOf (l));
            g.setColour (warn);
            g.drawText (juce::String (bands[b]) + ":  " + names.joinIntoString (", ") + " share this band",
                        list.removeFromTop (18), juce::Justification::centredLeft);
            any = true;
        }
        if (! any)
        {
            g.setColour (good);
            g.drawText ("Every band has one owner", list.removeFromTop (18), juce::Justification::centredLeft);
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
        repaint (healthArea());
    }

    juce::Rectangle<int> healthArea() const { return getLocalBounds().withTop (healthTitle.getBottom()); }

    juce::AudioProcessorValueTreeState& apvts;
    MixHealth& mixHealth;
    std::function<juce::String (int)> nameOf;
    std::array<Knob, 7> knobs;
    std::array<juce::TextButton, 3> listen;
    juce::TextButton reset;
    juce::Rectangle<int> titleArea, healthTitle, correlationArea, maskArea;
    std::array<juce::Rectangle<int>, 6> cells;
    float shownPeak = -100.0f;
};
}
