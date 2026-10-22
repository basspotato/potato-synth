#pragma once
#include "Theme.h"
#include "../Params.h"
#include "../PluginProcessor.h"

namespace ps
{
// A row of step bars. Drag across the bars to draw values; an optional strip of toggles sits above them
// (on/rest for the sequencer). Steps past the length are dimmed. Values are 0..1; a bipolar row draws from
// the centre line, and `snap` rounds to that many levels.
class StepBars : public juce::Component
{
public:
    std::function<float (int)> getValue;
    std::function<void (int, float)> setValue;
    std::function<bool (int)> getToggle;          // optional strips above the bars
    std::function<void (int)> flipToggle;
    std::function<bool (int)> getTie;
    std::function<void (int)> flipTie;
    std::function<juce::String (int)> valueText;  // shown in the bar while it is being drawn
    int length = 16, snap = 0;
    bool bipolar = false;
    juce::Colour colour { theme::mid };

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::bgBottom.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 8.0f);

        const float w = barArea().getWidth() / (float) maxSteps;
        for (int s = 0; s < maxSteps; ++s)
        {
            const bool inside = s < length;
            const float x = barArea().getX() + (float) s * w;
            const auto beatShade = (s / 4) % 2 == 0 ? 0.05f : 0.0f;
            g.setColour (juce::Colours::white.withAlpha (beatShade));
            g.fillRect (x, r.getY() + 4.0f, w, r.getHeight() - 8.0f);

            const bool on = getToggle == nullptr || getToggle (s);
            const auto c = colour.withAlpha (inside ? (on ? 1.0f : 0.25f) : 0.12f);
            if (getToggle != nullptr)
            {
                auto t = juce::Rectangle<float> (x + 2.0f, toggleArea().getY(), w - 4.0f, toggleArea().getHeight()).reduced (0.0f, 1.0f);
                g.setColour (on ? c : theme::panelEdge.withAlpha (inside ? 0.6f : 0.25f));
                g.fillRoundedRectangle (t, 3.0f);
            }
            if (getTie != nullptr && getTie (s))
            {
                auto t = juce::Rectangle<float> (x + 2.0f, tieArea().getY(), w, tieArea().getHeight()).reduced (0.0f, 3.0f);
                g.setColour (colour.withAlpha (inside ? 0.7f : 0.2f));
                g.fillRoundedRectangle (t, 3.0f);
            }

            const auto bars = barArea();
            const float v = juce::jlimit (0.0f, 1.0f, getValue != nullptr ? getValue (s) : 0.0f);
            const float base = bipolar ? bars.getCentreY() : bars.getBottom();
            const float top = bars.getBottom() - v * bars.getHeight();
            auto bar = juce::Rectangle<float>::leftTopRightBottom (x + 2.0f, juce::jmin (base, top), x + w - 2.0f, juce::jmax (base, top));
            if (bar.getHeight() < 2.0f)
                bar = bar.withSizeKeepingCentre (bar.getWidth(), 2.0f);
            g.setGradientFill (juce::ColourGradient (c, 0.0f, bars.getY(), c.withMultipliedAlpha (0.35f), 0.0f, bars.getBottom(), false));
            g.fillRoundedRectangle (bar, 2.5f);
        }
        if (bipolar)
        {
            g.setColour (theme::textDim.withAlpha (0.4f));
            g.drawHorizontalLine (juce::roundToInt (barArea().getCentreY()), barArea().getX(), barArea().getRight());
        }
        if (dragStep >= 0 && valueText != nullptr)
        {
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (valueText (dragStep), barArea().reduced (6.0f, 2.0f), juce::Justification::topRight);
        }
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int s = stepAt (e.position.x);
        if (getToggle != nullptr && e.position.y < toggleArea().getBottom())
        {
            flipToggle (s);
            repaint();
            return;
        }
        if (getTie != nullptr && e.position.y < tieArea().getBottom())
        {
            flipTie (s);
            repaint();
            return;
        }
        drawing = true;
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drawing)
            return;
        const auto bars = barArea();
        float v = juce::jlimit (0.0f, 1.0f, (bars.getBottom() - e.position.y) / bars.getHeight());
        if (snap > 1)
            v = std::round (v * (float) (snap - 1)) / (float) (snap - 1);
        dragStep = stepAt (e.position.x);
        setValue (dragStep, v);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        drawing = false;
        dragStep = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.position.y >= barArea().getY())
            setValue (stepAt (e.position.x), bipolar ? 0.5f : 1.0f);   // back to centre / full
        repaint();
    }

private:
    static constexpr int maxSteps = 32;

    juce::Rectangle<float> inner() const { return getLocalBounds().toFloat().reduced (6.0f, 6.0f); }
    juce::Rectangle<float> toggleArea() const { return inner().withHeight (getToggle != nullptr ? 14.0f : 0.0f); }
    juce::Rectangle<float> tieArea() const
    {
        return inner().withTrimmedTop (toggleArea().getHeight() + (getToggle != nullptr ? 3.0f : 0.0f)).withHeight (getTie != nullptr ? 10.0f : 0.0f);
    }
    juce::Rectangle<float> barArea() const { return inner().withTrimmedTop (tieArea().getBottom() - inner().getY() + 4.0f); }

    int stepAt (float x) const
    {
        const auto a = barArea();
        return juce::jlimit (0, maxSteps - 1, (int) ((x - a.getX()) / a.getWidth() * (float) maxSteps));
    }

    bool drawing = false;
    int dragStep = -1;
};

// ARP / SEQ page: the arpeggiator and step sequencer (mode, rate, direction, octaves, gate, swing, latch,
// length, trigger), a 32-step editor for the sequencer pattern, and the four mod lanes that move the chain
// knobs, SUPER or the XY pad in time.
class ArpPage : public juce::Component,
                private juce::Timer
{
public:
    explicit ArpPage (PotatoSynthProcessor& p) : processor (p), apvts (p.apvts)
    {
        for (int m = 0; m < 3; ++m)
        {
            auto& b = modeButtons[(size_t) m];
            b.setButtonText (params::arpModeNames[m].toUpperCase());
            b.onClick = [this, m] { setChoice (params::arpMode, m); };
            addAndMakeVisible (b);
        }
        latch.setButtonText ("LATCH");
        latch.setClickingTogglesState (true);
        addAndMakeVisible (latch);
        latchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, params::arpLatch.getParamID(), latch);

        setUpCombo (rate, "RATE", params::arpRate, rateAttachment);
        setUpCombo (direction, "DIRECTION", params::arpDirection, directionAttachment);
        setUpCombo (trigger, "SEQ START", params::seqTrigger, triggerAttachment);

        setUpKnob (octaves, "OCTAVES", params::arpOctaves, theme::mid);
        setUpKnob (gate, "GATE", params::arpGate, theme::mid);
        setUpKnob (swing, "SWING", params::arpSwing, theme::mid);
        setUpKnob (length, "STEPS", params::arpLength, theme::mid);

        for (int e = 0; e < 4; ++e)
        {
            auto& b = editButtons[(size_t) e];
            b.setButtonText (editNames[e]);
            b.onClick = [this, e] { editField = e; configureSteps(); };
            addAndMakeVisible (b);
        }
        steps.getToggle = [this] (int s) { return processor.getPatternStep (s).on; };
        steps.flipToggle = [this] (int s) { auto st = processor.getPatternStep (s); st.on = ! st.on; processor.setPatternStep (s, st); };
        steps.getTie = [this] (int s) { return processor.getPatternStep (s).tie; };
        steps.flipTie = [this] (int s) { auto st = processor.getPatternStep (s); st.tie = ! st.tie; processor.setPatternStep (s, st); };
        steps.colour = theme::mid;
        addAndMakeVisible (steps);
        configureSteps();

        for (int l = 0; l < ModLanes::numLanes; ++l)
        {
            auto& b = laneButtons[(size_t) l];
            b.setButtonText ("LANE " + juce::String (l + 1));
            b.onClick = [this, l] { showLane (l); };
            addAndMakeVisible (b);
        }
        laneTarget.setTooltip ("What this lane moves");
        for (auto* c : { &laneTarget, &laneRate })
        {
            styleCombo (*c);
            addAndMakeVisible (*c);
        }
        laneTarget.addItemList (params::laneTargetNames, 1);
        laneRate.addItemList (params::delayDivisionNames, 1);
        for (auto* l : { &laneTargetLabel, &laneRateLabel })
        {
            styleLabel (*l, theme::air);
            addAndMakeVisible (*l);
        }
        laneTargetLabel.setText ("TARGET", juce::dontSendNotification);
        laneRateLabel.setText ("RATE", juce::dontSendNotification);
        for (auto* k : { &laneDepth, &laneLength, &laneSmooth })
        {
            k->slider.setSliderStyle (juce::Slider::LinearBar);
            k->slider.setColour (juce::Slider::trackColourId, theme::air.withAlpha (0.45f));
            k->slider.setColour (juce::Slider::backgroundColourId, theme::bgBottom);
            styleLabel (k->label, theme::air);
            addAndMakeVisible (k->slider);
            addAndMakeVisible (k->label);
        }
        laneDepth.label.setText ("DEPTH", juce::dontSendNotification);
        laneLength.label.setText ("STEPS", juce::dontSendNotification);
        laneSmooth.label.setText ("SMOOTH", juce::dontSendNotification);
        lanes.getValue = [this] (int s) { return processor.getLaneValue (lane, s); };
        lanes.setValue = [this] (int s, float v) { processor.setLaneValue (lane, s, v); };
        lanes.valueText = [this] (int s) { return juce::String (juce::roundToInt (processor.getLaneValue (lane, s) * 100.0f)) + "%"; };
        lanes.colour = theme::air;
        addAndMakeVisible (lanes);
        showLane (0);
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (theme::mid.withAlpha (0.5f));
        g.drawRoundedRectangle (r.reduced (0.75f), 14.0f, 1.5f);

        g.setColour (theme::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("ARP / SEQ", titleArea, juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("PATTERN", patternTitle, juce::Justification::centredLeft);
        g.drawText ("MOD LANES", laneTitle, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::CharPointer_UTF8 ("top row: on/rest  \xc2\xb7  thin row: tie  \xc2\xb7  drag bars"), patternTitle, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 10);
        auto header = r.removeFromTop (28);
        titleArea = header.removeFromLeft (130);
        latch.setBounds (header.removeFromRight (70).reduced (2, 2));
        header.removeFromRight (8);
        for (int m = 2; m >= 0; --m)
            modeButtons[(size_t) m].setBounds (header.removeFromRight (64).reduced (2, 2));
        r.removeFromTop (6);

        // Arp controls: three menus and four knobs on one row
        auto controls = r.removeFromTop (juce::jlimit (64, 100, r.getHeight() / 5));
        const int cw = controls.getWidth() / 7;
        for (auto* c : { &rate, &direction, &trigger })
        {
            auto cell = controls.removeFromLeft (cw).reduced (3, 0);
            c->label.setBounds (cell.removeFromTop (14));
            c->box.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 24));
        }
        for (auto* k : { &octaves, &gate, &swing, &length })
        {
            auto cell = controls.removeFromLeft (cw).reduced (3, 0);
            k->label.setBounds (cell.removeFromTop (14));
            k->slider.setBounds (cell);
        }
        r.removeFromTop (6);

        const int half = (r.getHeight() - 6) / 2;
        auto pattern = r.removeFromTop (half);
        r.removeFromTop (6);
        auto patternHead = pattern.removeFromTop (22);
        patternTitle = patternHead;
        auto editRow = patternHead.withTrimmedLeft (70).withWidth (juce::jmin (290, patternHead.getWidth() / 2));
        const int ew = editRow.getWidth() / 4;
        for (auto& b : editButtons)
            b.setBounds (editRow.removeFromLeft (ew).reduced (2, 1));
        pattern.removeFromTop (3);
        steps.setBounds (pattern);

        auto laneHead = r.removeFromTop (22);
        laneTitle = laneHead;
        laneHead.removeFromLeft (80);
        for (auto& b : laneButtons)
            b.setBounds (laneHead.removeFromLeft (64).reduced (2, 1));
        r.removeFromTop (3);
        auto laneControls = r.removeFromRight (juce::jmin (230, r.getWidth() / 3));
        r.removeFromRight (6);
        lanes.setBounds (r);
        const int rowH = laneControls.getHeight() / 5;
        auto row = [&] { return laneControls.removeFromTop (rowH).reduced (0, 1); };
        auto comboRow = [] (juce::Label& l, juce::ComboBox& c, juce::Rectangle<int> cell)
        {
            l.setBounds (cell.removeFromLeft (58));
            c.setBounds (cell);
        };
        comboRow (laneTargetLabel, laneTarget, row());
        comboRow (laneRateLabel, laneRate, row());
        for (auto* k : { &laneDepth, &laneLength, &laneSmooth })
        {
            auto cell = row();
            k->label.setBounds (cell.removeFromLeft (58));
            k->slider.setBounds (cell);
        }
    }

private:
    struct Combo
    {
        juce::ComboBox box;
        juce::Label label;
    };
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    static void styleCombo (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::backgroundColourId, theme::bgBottom);
        c.setColour (juce::ComboBox::outlineColourId, theme::panelEdge);
        c.setColour (juce::ComboBox::textColourId, theme::text);
        c.setColour (juce::ComboBox::arrowColourId, theme::textDim);
    }

    static void styleLabel (juce::Label& l, juce::Colour accent)
    {
        l.setJustificationType (juce::Justification::centredLeft);
        l.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, accent.interpolatedWith (theme::text, 0.35f));
    }

    void setUpCombo (Combo& c, const juce::String& name, const juce::ParameterID& id,
                     std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& attachment)
    {
        styleCombo (c.box);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id.getParamID())))
            c.box.addItemList (p->choices, 1);
        addAndMakeVisible (c.box);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id.getParamID(), c.box);
        c.label.setText (name, juce::dontSendNotification);
        styleLabel (c.label, theme::mid);
        c.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (c.label);
    }

    void setUpKnob (Knob& k, const juce::String& name, const juce::ParameterID& id, juce::Colour accent)
    {
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 15);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        addAndMakeVisible (k.slider);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id.getParamID(), k.slider);
        auto* p = apvts.getParameter (id.getParamID());
        k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        k.label.setText (name, juce::dontSendNotification);
        styleLabel (k.label, accent);
        k.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (k.label);
    }

    // Points the pattern editor's bars at velocity, note offset, probability or ratchets
    void configureSteps()
    {
        auto edit = [this] (int s, auto&& change)
        {
            auto st = processor.getPatternStep (s);
            change (st);
            processor.setPatternStep (s, st);
        };
        steps.bipolar = editField == 1;
        steps.snap = editField == 1 ? 49 : editField == 3 ? 4 : 0;
        switch (editField)
        {
            case 0:
                steps.getValue = [this] (int s) { return processor.getPatternStep (s).velocity; };
                steps.setValue = [edit] (int s, float v) { edit (s, [v] (auto& st) { st.velocity = v; }); };
                steps.valueText = [this] (int s) { return "vel " + juce::String (juce::roundToInt (processor.getPatternStep (s).velocity * 127.0f)); };
                break;
            case 1:
                steps.getValue = [this] (int s) { return ((float) processor.getPatternStep (s).offset + 24.0f) / 48.0f; };
                steps.setValue = [edit] (int s, float v) { edit (s, [v] (auto& st) { st.offset = juce::roundToInt (v * 48.0f) - 24; }); };
                steps.valueText = [this] (int s) { const int o = processor.getPatternStep (s).offset; return (o > 0 ? "+" : "") + juce::String (o) + " st"; };
                break;
            case 2:
                steps.getValue = [this] (int s) { return processor.getPatternStep (s).probability; };
                steps.setValue = [edit] (int s, float v) { edit (s, [v] (auto& st) { st.probability = v; }); };
                steps.valueText = [this] (int s) { return juce::String (juce::roundToInt (processor.getPatternStep (s).probability * 100.0f)) + "%"; };
                break;
            default:
                steps.getValue = [this] (int s) { return (float) (processor.getPatternStep (s).ratchet - 1) / 3.0f; };
                steps.setValue = [edit] (int s, float v) { edit (s, [v] (auto& st) { st.ratchet = 1 + juce::roundToInt (v * 3.0f); }); };
                steps.valueText = [this] (int s) { return juce::String (processor.getPatternStep (s).ratchet) + "x"; };
                break;
        }
        for (int e = 0; e < 4; ++e)
            highlight (editButtons[(size_t) e], e == editField, theme::mid);
        steps.repaint();
    }

    void showLane (int l)
    {
        lane = l;
        laneTargetAttachment.reset();   // detach first, so re-pointing the menus never writes into the old lane
        laneRateAttachment.reset();
        laneTargetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, params::laneTarget (l).getParamID(), laneTarget);
        laneRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, params::laneRate (l).getParamID(), laneRate);
        const juce::ParameterID ids[] = { params::laneDepth (l), params::laneLength (l), params::laneSmooth (l) };
        Knob* knobs[] = { &laneDepth, &laneLength, &laneSmooth };
        for (int i = 0; i < 3; ++i)
        {
            knobs[i]->attachment.reset();
            knobs[i]->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ids[i].getParamID(), knobs[i]->slider);
            auto* p = apvts.getParameter (ids[i].getParamID());
            knobs[i]->slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        }
        for (int i = 0; i < ModLanes::numLanes; ++i)
            highlight (laneButtons[(size_t) i], i == l, theme::air);
        timerCallback();
        lanes.repaint();
    }

    void timerCallback() override
    {
        auto value = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID())->load(); };
        const int mode = juce::roundToInt (value (params::arpMode));
        for (int m = 0; m < 3; ++m)
            highlight (modeButtons[(size_t) m], m == mode, m == 0 ? theme::panelEdge : theme::mid);
        highlight (latch, latch.getToggleState(), theme::mid);

        const int seqLength = juce::roundToInt (value (params::arpLength));
        const int laneLen = juce::roundToInt (value (params::laneLength (lane)));
        const bool laneOn = juce::roundToInt (value (params::laneTarget (lane))) > 0;
        if (steps.length != seqLength)
        {
            steps.length = seqLength;
            steps.repaint();
        }
        const auto laneColour = laneOn ? theme::air : theme::air.withSaturation (0.15f);
        if (lanes.length != laneLen || lanes.colour != laneColour)
        {
            lanes.length = laneLen;
            lanes.colour = laneColour;
            lanes.repaint();
        }
        for (int i = 0; i < ModLanes::numLanes; ++i)
            laneButtons[(size_t) i].setButtonText ((juce::roundToInt (value (params::laneTarget (i))) > 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ")) : juce::String())
                                                   + "LANE " + juce::String (i + 1));
    }

    static void highlight (juce::Button& b, bool on, juce::Colour colour)
    {
        b.setColour (juce::TextButton::buttonColourId, on ? colour : theme::panel);
        b.setColour (juce::TextButton::buttonOnColourId, on ? colour : theme::panel);
        b.setAlpha (on ? 1.0f : 0.7f);
    }

    void setChoice (const juce::ParameterID& id, int index)
    {
        auto* p = apvts.getParameter (id.getParamID());
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
        timerCallback();
    }

    static constexpr const char* editNames[4] = { "VELOCITY", "NOTE", "CHANCE", "RATCHET" };

    PotatoSynthProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    juce::Rectangle<int> titleArea, patternTitle, laneTitle;

    std::array<juce::TextButton, 3> modeButtons;
    juce::TextButton latch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> latchAttachment;
    Combo rate, direction, trigger;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateAttachment, directionAttachment, triggerAttachment;
    Knob octaves, gate, swing, length;

    std::array<juce::TextButton, 4> editButtons;
    int editField = 0;
    StepBars steps;

    std::array<juce::TextButton, ModLanes::numLanes> laneButtons;
    int lane = 0;
    juce::ComboBox laneTarget, laneRate;
    juce::Label laneTargetLabel, laneRateLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> laneTargetAttachment, laneRateAttachment;
    Knob laneDepth, laneLength, laneSmooth;
    StepBars lanes;
};
}
