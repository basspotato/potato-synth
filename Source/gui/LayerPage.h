#pragma once
#include "Theme.h"
#include "../Params.h"
#include "../Engine.h"
#include "../Inserts.h"

namespace ps
{
// Draws the selected layer's amp envelope from its parameters. Segment widths follow the square root
// of each time, so short and long envelopes both stay readable.
class EnvelopeView : public juce::Component
{
public:
    void setColour (juce::Colour c) { colour = c; repaint(); }

    void setValues (float attackMs, float decayMs, float sustain, float releaseMs)
    {
        const std::array<float, 4> v { attackMs, decayMs, sustain, releaseMs };
        if (v != values)
        {
            values = v;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::bgBottom.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        r = r.reduced (14.0f, 12.0f);
        const float a = std::sqrt (values[0]), d = std::sqrt (values[1]), rel = std::sqrt (values[3]);
        const float hold = (a + d + rel) * 0.25f + 1.0f;
        const float scale = r.getWidth() / (a + d + hold + rel);
        const float s = juce::jlimit (0.0f, 1.0f, values[2]);
        auto yFor = [&r] (float level) { return r.getBottom() - level * r.getHeight(); };

        const float x0 = r.getX(), x1 = x0 + a * scale, x2 = x1 + d * scale, x3 = x2 + hold * scale, x4 = x3 + rel * scale;
        juce::Path p;
        p.startNewSubPath (x0, yFor (0.0f));
        p.quadraticTo (x0 + (x1 - x0) * 0.35f, yFor (0.85f), x1, yFor (1.0f));             // convex attack
        p.quadraticTo (x1 + (x2 - x1) * 0.15f, yFor (s), x2, yFor (s));                    // exponential decay
        p.lineTo (x3, yFor (s));
        p.quadraticTo (x3 + (x4 - x3) * 0.12f, yFor (0.0f), x4, yFor (0.0f));              // exponential release

        juce::Path fill (p);
        fill.lineTo (x0, yFor (0.0f));
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f), 0.0f, r.getY(),
                                                 colour.withAlpha (0.02f), 0.0f, r.getBottom(), false));
        g.fillPath (fill);
        for (int i = 2; i >= 1; --i)
        {
            g.setColour (colour.withAlpha (0.12f * (float) (3 - i)));
            g.strokePath (p, juce::PathStrokeType (2.0f + 3.0f * (float) i));
        }
        g.setColour (colour.interpolatedWith (juce::Colours::white, 0.2f));
        g.strokePath (p, juce::PathStrokeType (2.0f));

        g.setColour (theme::textDim);
        g.setFont (ps::fonts::bold (11.0f));
        g.drawText ("AMP ENVELOPE", getLocalBounds().reduced (12, 8), juce::Justification::topRight);
    }

private:
    std::array<float, 4> values { 2.0f, 400.0f, 1.0f, 300.0f };
    juce::Colour colour { theme::mid };
};

// Detail page for one layer: engine picker, the engine's own controls (its slots) or the layer's filter, drive and
// chorus, its two insert slots, voice mode, amp envelope, pan, tune, glide and key / velocity zones.
class LayerPage : public juce::Component,
                  private juce::Timer
{
public:
    std::function<void()> onEngineClick;

    explicit LayerPage (juce::AudioProcessorValueTreeState& state) : apvts (state)
    {
        addAndMakeVisible (envelope);
        engineButton.onClick = [this] { if (onEngineClick != nullptr) onEngineClick(); };
        addAndMakeVisible (engineButton);

        for (int r = 0; r < (int) roleButtons.size(); ++r)
        {
            auto& b = roleButtons[(size_t) r];
            b.setButtonText (params::roleNames[r].toUpperCase());
            b.onClick = [this, r] { setChoice (params::layerRole (layer), r); };
            addAndMakeVisible (b);
        }

        for (int m = 0; m < (int) modeButtons.size(); ++m)
        {
            auto& b = modeButtons[(size_t) m];
            b.setButtonText (params::voiceModeNames[m].toUpperCase());
            b.setClickingTogglesState (false);
            b.onClick = [this, m] { setChoice (params::layerVoiceMode (layer), m); };
            addAndMakeVisible (b);
        }

        const char* const names[] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "PAN", "OCTAVE", "SEMI", "FINE", "GLIDE",
                                      "KEY LOW", "KEY HIGH", "KEY FADE", "VEL LOW", "VEL HIGH", "VEL FADE" };
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& k = knobs[i];
            addAndMakeVisible (k.slider);
            k.label.setText (names[i], juce::dontSendNotification);
            k.label.setJustificationType (juce::Justification::centred);
            k.label.setFont (ps::fonts::bold (12.0f));
            addAndMakeVisible (k.label);
        }
        for (auto& k : slotKnobs)
        {
            k.label.setJustificationType (juce::Justification::centred);
            k.label.setFont (ps::fonts::bold (12.0f));
            addChildComponent (k.slider);
            addChildComponent (k.label);
        }
        // Filter / FX section: the layer's own filter, its envelope, drive and chorus
        const char* const fxNames[] = { "CUTOFF", "RESONANCE", "ENV AMOUNT", "KEY TRACK", "DRIVE", "CHORUS", "RATE", "DEPTH",
                                        "F ATTACK", "F DECAY", "F SUSTAIN", "F RELEASE" };
        for (size_t i = 0; i < fxKnobs.size(); ++i)
        {
            auto& k = fxKnobs[i];
            k.label.setText (fxNames[i], juce::dontSendNotification);
            k.label.setJustificationType (juce::Justification::centred);
            k.label.setFont (ps::fonts::bold (12.0f));
            addChildComponent (k.slider);
            addChildComponent (k.label);
        }
        for (int t = 0; t < (int) filterTypeButtons.size(); ++t)
        {
            auto& b = filterTypeButtons[(size_t) t];
            b.setButtonText (params::filterTypeNames[t].toUpperCase());
            b.onClick = [this, t] { setChoice (params::layerFilterType (layer), t); };
            addChildComponent (b);
        }
        // Insert slots: a type menu, Amount and the type's three controls per slot, and the order
        for (int k = 0; k < params::numInsertSlots; ++k)
        {
            auto& slot = insertSlots[(size_t) k];
            slot.type.onClick = [this, k] { showInsertMenu (k); };
            addChildComponent (slot.type);
            for (auto* knob : { &slot.amount, &slot.controls[0], &slot.controls[1], &slot.controls[2] })
            {
                knob->label.setJustificationType (juce::Justification::centred);
                knob->label.setFont (ps::fonts::bold (12.0f));
                addChildComponent (knob->slider);
                addChildComponent (knob->label);
            }
            slot.amount.label.setText ("AMOUNT", juce::dontSendNotification);
        }
        insertSwap.setButtonText ("SWAP ORDER");
        insertSwap.setClickingTogglesState (true);
        addChildComponent (insertSwap);
        const char* const sectionNames[] = { "ENGINE", "FILTER / FX", "INSERTS", "AMP / VOICE" };
        for (int i = 0; i < (int) sectionButtons.size(); ++i)
        {
            auto& b = sectionButtons[(size_t) i];
            b.setButtonText (sectionNames[i]);
            b.onClick = [this, i] { showSection (i); };
            addAndMakeVisible (b);
        }
        noSlots.setJustificationType (juce::Justification::centred);
        noSlots.setColour (juce::Label::textColourId, theme::textDim);
        addChildComponent (noSlots);
        showSection (0);
        startTimerHz (15);
    }

    void showLayer (int index, const juce::String& role, juce::Colour colour)
    {
        layer = index;
        roleName = role;
        accent = colour;
        envelope.setColour (colour);
        engineButton.setColour (juce::TextButton::buttonColourId, colour);

        const juce::ParameterID ids[] = { params::layerAttack (index), params::layerDecay (index), params::layerSustain (index),
                                          params::layerRelease (index), params::layerPan (index), params::layerOctave (index),
                                          params::layerSemi (index), params::layerFine (index), params::layerGlide (index),
                                          params::layerKeyLow (index), params::layerKeyHigh (index), params::layerKeyFade (index),
                                          params::layerVelLow (index), params::layerVelHigh (index), params::layerVelFade (index) };
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& k = knobs[i];
            k.attachment.reset();
            k.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
            k.label.setColour (juce::Label::textColourId, colour.interpolatedWith (theme::text, 0.35f));
            k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ids[i].getParamID(), k.slider);
            auto* p = apvts.getParameter (ids[i].getParamID());
            k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        }
        const juce::ParameterID fxIds[] = { params::layerCutoff (index), params::layerResonance (index), params::layerFilterEnv (index),
                                            params::layerKeyTrack (index), params::layerDrive (index), params::layerChorus (index),
                                            params::layerChorusRate (index), params::layerChorusDepth (index), params::layerFilterAttack (index),
                                            params::layerFilterDecay (index), params::layerFilterSustain (index), params::layerFilterRelease (index) };
        for (size_t i = 0; i < fxKnobs.size(); ++i)
        {
            auto& k = fxKnobs[i];
            k.attachment.reset();
            k.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
            k.label.setColour (juce::Label::textColourId, colour.interpolatedWith (theme::text, 0.35f));
            k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, fxIds[i].getParamID(), k.slider);
            auto* p = apvts.getParameter (fxIds[i].getParamID());
            k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        }
        for (int k = 0; k < params::numInsertSlots; ++k)
        {
            auto& slot = insertSlots[(size_t) k];
            slot.amount.attachment.reset();
            slot.amount.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts, params::layerInsertAmount (index, k).getParamID(), slot.amount.slider);
            slot.amount.slider.setDoubleClickReturnValue (true, 50.0);
            for (int c = 0; c < 3; ++c)
            {
                auto& knob = slot.controls[(size_t) c];
                knob.attachment.reset();
                knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    apvts, params::layerInsertControl (index, k, c).getParamID(), knob.slider);
            }
            for (auto* knob : { &slot.amount, &slot.controls[0], &slot.controls[1], &slot.controls[2] })
            {
                knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
                knob->label.setColour (juce::Label::textColourId, colour.interpolatedWith (theme::text, 0.35f));
            }
            slot.shownType = -1;
        }
        insertSwapAttachment.reset();
        insertSwapAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, params::layerInsertSwap (index).getParamID(), insertSwap);
        insertSwap.setColour (juce::TextButton::buttonOnColourId, colour);
        slotsStale = true;   // reattached for this layer by the next setEngine()
        timerCallback();
        repaint();
    }

    // Chain-knob map: draws where each role-tagged slot knob really sits once the chain knobs' offsets are added
    void setSlotGhosts (const std::array<float, numSlotRoles>& offsets)
    {
        for (size_t k = 0; k < slotKnobs.size(); ++k)
        {
            auto& s = slotKnobs[k].slider;
            double ghost = -1.0;
            if (shownInfo != nullptr && k < shownInfo->slots.size() && shownInfo->slots[k].role != SlotRole::none
                && std::abs (offsets[(size_t) shownInfo->slots[k].role]) > 1.0e-4f)
                ghost = juce::jlimit (0.0, 1.0, s.valueToProportionOfLength (s.getValue()) + (double) offsets[(size_t) shownInfo->slots[k].role]);
            if ((double) s.getProperties().getWithDefault ("ghost", -1.0) != ghost)
            {
                s.getProperties().set ("ghost", ghost);
                s.repaint();
            }
        }
    }

    void setEngine (const juce::String& name, const EngineInfo* info)
    {
        engineButton.setButtonText (juce::String ("ENGINE:  ") + (name.isEmpty() ? juce::String ("EMPTY") : name.toUpperCase())
                                    + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
        if (info != shownInfo || slotsStale)
            attachSlots (info);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (accent.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.75f), 14.0f, 1.5f);

        auto engineRow = slotArea.toFloat();
        g.setColour (theme::bgBottom.withAlpha (0.6f));
        g.fillRoundedRectangle (engineRow, 10.0f);
        g.setColour (accent.withAlpha (0.35f));
        g.drawRoundedRectangle (engineRow.reduced (0.5f), 10.0f, 1.0f);
        // Each knob sits in its own cell: name above, value below, so a value never runs into the next row's name
        for (size_t t = 0; t < tiles[(size_t) section].size(); ++t)
        {
            // Inserts: four tiles per slot, drawn only while that slot has an effect loaded
            if (section == 2 && ! insertSlots[t / 4].amount.slider.isVisible())
                continue;
            const auto c = tiles[(size_t) section][t].toFloat();
            g.setColour (theme::panel.withAlpha (0.55f));
            g.fillRoundedRectangle (c, 8.0f);
            g.setColour (theme::panelEdge.withAlpha (0.6f));
            g.drawRoundedRectangle (c.reduced (0.5f), 8.0f, 1.0f);
        }

        g.setColour (theme::text);
        g.setFont (ps::fonts::bold (20.0f));
        g.drawText ("LAYER " + juce::String (layer + 1) + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + roleName,
                    getLocalBounds().reduced (18, 12).removeFromTop (28), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 12);
        auto header = r.removeFromTop (28);
        engineButton.setBounds (header.removeFromRight (juce::jmin (260, header.getWidth() / 2)));
        r.removeFromTop (8);
        auto roleRow = r.removeFromTop (26);
        auto sections = roleRow.removeFromRight (roleRow.getWidth() * 3 / 5);
        roleRow.removeFromRight (12);
        const int sw = sections.getWidth() / (int) sectionButtons.size();
        for (auto& b : sectionButtons)
            b.setBounds (sections.removeFromLeft (sw).reduced (2, 0));
        const int rw = roleRow.getWidth() / (int) roleButtons.size();
        for (auto& b : roleButtons)
            b.setBounds (roleRow.removeFromLeft (rw).reduced (2, 0));
        r.removeFromTop (8);

        // Each section gets the whole page below the role row
        slotArea = r;
        layoutSlots();
        auto rows = r.reduced (8, 10);
        const int knobRowH = juce::jmin (130, (rows.getHeight() - 16) / 3);
        auto envRow = rows.removeFromTop (knobRowH);
        rows.removeFromTop (8);
        auto bottomRow = rows.removeFromTop (knobRowH);
        rows.removeFromTop (8);
        auto zoneRow = rows.removeFromTop (knobRowH);
        // The envelope drawing sits beside its four knobs
        envelope.setBounds (envRow.removeFromRight (envRow.getWidth() * 2 / 5).reduced (4, 0));

        auto& voiceTiles = tiles[3];
        voiceTiles.clear();
        auto place = [&voiceTiles] (Knob& k, juce::Rectangle<int> cell)
        {
            voiceTiles.push_back (cell);
            placeKnob (k, cell);
        };
        const int w = envRow.getWidth() / 4;
        for (size_t i = 0; i < 4; ++i)
            place (knobs[i], envRow.removeFromLeft (w).reduced (3, 0));
        // Bottom row: voice mode buttons stacked in the first cell, then pan, tune and glide
        const int bw = bottomRow.getWidth() / 6;
        auto modeCell = bottomRow.removeFromLeft (bw).reduced (3, 0);
        voiceTiles.push_back (modeCell);
        auto modes = modeCell.reduced (6, 4);
        const int mh = juce::jmin (30, modes.getHeight() / 3);
        modes = modes.withSizeKeepingCentre (modes.getWidth(), mh * 3);
        for (auto& b : modeButtons)
            b.setBounds (modes.removeFromTop (mh).reduced (0, 2));
        for (size_t i = 4; i < 9; ++i)
            place (knobs[i], bottomRow.removeFromLeft (bw).reduced (3, 0));
        // Zones: which keys and velocities the layer plays
        const int zw = zoneRow.getWidth() / 6;
        for (size_t i = 9; i < knobs.size(); ++i)
            place (knobs[i], zoneRow.removeFromLeft (zw).reduced (3, 0));
    }

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // A knob's cell: the name on top, the dial, and the value box below, all inside the cell
    static void placeKnob (Knob& k, juce::Rectangle<int> cell)
    {
        auto inner = cell.reduced (3, 5);
        k.label.setBounds (inner.removeFromTop (19));
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmax (40, inner.getWidth()), 22);
        k.slider.setBounds (inner);
    }


    // Shows one knob per slot the engine uses, named by the engine; its value text comes from the parameter.
    void attachSlots (const EngineInfo* info)
    {
        shownInfo = info;
        slotsStale = false;
        const int used = info != nullptr ? juce::jmin ((int) info->slots.size(), (int) slotKnobs.size()) : 0;
        for (int k = 0; k < (int) slotKnobs.size(); ++k)
        {
            auto& knob = slotKnobs[(size_t) k];
            knob.attachment.reset();
            const bool visible = k < used;
            knob.slider.setVisible (visible && section == 0);
            knob.label.setVisible (visible && section == 0);
            if (! visible)
                continue;
            const auto& slot = info->slots[(size_t) k];
            knob.label.setText (slot.name.toUpperCase(), juce::dontSendNotification);
            knob.label.setColour (juce::Label::textColourId, accent.interpolatedWith (theme::text, 0.35f));
            knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
            const auto id = params::layerSlot (layer, k).getParamID();
            knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, knob.slider);
            knob.slider.setDoubleClickReturnValue (true, slot.defaultValue);
            knob.slider.updateText();
        }
        noSlots.setText (info == nullptr ? "Pick an engine to play this layer" : "This engine has no controls of its own",
                         juce::dontSendNotification);
        noSlots.setVisible (used == 0 && section == 0);
        usedSlots = used;
        layoutSlots();
    }

    // The insert type menu; picking a type also sets its controls to that type's starting values
    void showInsertMenu (int k)
    {
        juce::PopupMenu menu;
        const int current = juce::roundToInt (apvts.getRawParameterValue (params::layerInsertType (layer, k).getParamID())->load());
        for (int t = 0; t < inserts::numTypes; ++t)
            menu.addItem (t + 1, params::insertTypeNames[t], true, t == current);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&insertSlots[(size_t) k].type),
                            [safe = juce::Component::SafePointer<LayerPage> (this), k, forLayer = layer] (int result)
                            {
                                if (safe == nullptr || result == 0 || safe->layer != forLayer)
                                    return;
                                const int t = result - 1;
                                safe->setChoice (params::layerInsertType (forLayer, k), t);
                                for (int c = 0; c < 3; ++c)
                                {
                                    auto* p = safe->apvts.getParameter (params::layerInsertControl (forLayer, k, c).getParamID());
                                    p->beginChangeGesture();
                                    p->setValueNotifyingHost (p->convertTo0to1 (inserts::controls[(size_t) t][(size_t) c].defaultValue));
                                    p->endChangeGesture();
                                }
                                safe->timerCallback();
                            });
    }

    // Names, value text and visibility of an insert slot's controls follow its type
    void updateInsertSlot (int k)
    {
        auto& slot = insertSlots[(size_t) k];
        const int type = juce::jlimit (0, inserts::numTypes - 1,
                                       juce::roundToInt (apvts.getRawParameterValue (params::layerInsertType (layer, k).getParamID())->load()));
        const bool shown = section == 2;
        if (type != slot.shownType)
        {
            slot.shownType = type;
            slot.type.setButtonText ("INSERT " + juce::String (k + 1) + ":  " + params::insertTypeNames[type].toUpperCase()
                                     + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
            for (int c = 0; c < 3; ++c)
            {
                auto& knob = slot.controls[(size_t) c];
                const auto& info = inserts::controls[(size_t) type][(size_t) c];
                knob.label.setText (juce::String (info.name).toUpperCase(), juce::dontSendNotification);
                knob.slider.textFromValueFunction = [type, c] (double v) { return inserts::controlText ((inserts::Type) type, c, (float) v); };
                knob.slider.setDoubleClickReturnValue (true, info.defaultValue);
                knob.slider.updateText();
            }
        }
        slot.type.setVisible (shown);
        highlight (slot.type, type != 0, accent);
        for (auto* knob : { &slot.amount, &slot.controls[0], &slot.controls[1], &slot.controls[2] })
        {
            knob->slider.setVisible (shown && type != 0);
            knob->label.setVisible (shown && type != 0);
        }
    }

    void layoutInserts()
    {
        tiles[2].clear();
        auto area = slotArea.reduced (8, 8);
        insertSwap.setBounds (area.removeFromBottom (26).removeFromRight (160));
        area.removeFromBottom (6);
        const int h = area.getHeight() / params::numInsertSlots;
        for (auto& slot : insertSlots)
        {
            auto row = area.removeFromTop (h).reduced (0, 4);
            slot.type.setBounds (row.removeFromLeft (juce::jmin (240, row.getWidth() / 3)).withSizeKeepingCentre (juce::jmin (240, row.getWidth() / 3), 30));
            row.removeFromLeft (8);
            const int w = row.getWidth() / 4;
            for (auto* knob : { &slot.amount, &slot.controls[0], &slot.controls[1], &slot.controls[2] })
            {
                auto cell = row.removeFromLeft (w).reduced (3, 0);
                tiles[2].push_back (cell);
                placeKnob (*knob, cell);
            }
        }
    }

    void showSection (int index)
    {
        section = index;
        const bool filter = section == 1, voice = section == 3;
        envelope.setVisible (voice);
        for (auto& k : knobs)
        {
            k.slider.setVisible (voice);
            k.label.setVisible (voice);
        }
        for (auto& b : modeButtons)
            b.setVisible (voice);
        for (auto& k : fxKnobs)
        {
            k.slider.setVisible (filter);
            k.label.setVisible (filter);
        }
        for (auto& b : filterTypeButtons)
            b.setVisible (filter);
        for (int k = 0; k < (int) slotKnobs.size(); ++k)
        {
            slotKnobs[(size_t) k].slider.setVisible (section == 0 && k < usedSlots);
            slotKnobs[(size_t) k].label.setVisible (section == 0 && k < usedSlots);
        }
        noSlots.setVisible (section == 0 && usedSlots == 0);
        insertSwap.setVisible (section == 2);
        timerCallback();
    }

    // The filter section as six columns of two: the filter's four controls over its envelope, then drive over
    // chorus, and the chorus's rate over its depth
    void layoutFx()
    {
        tiles[1].clear();
        auto area = slotArea.reduced (8, 8);
        auto types = area.removeFromTop (28);
        const int tw = types.getWidth() / (int) filterTypeButtons.size();
        for (auto& b : filterTypeButtons)
            b.setBounds (types.removeFromLeft (tw).reduced (2, 0));
        area.removeFromTop (6);
        // Knob indices by position, row by row: cutoff, resonance, env amount, key track, drive, rate /
        // filter attack, decay, sustain, release, chorus, depth
        constexpr int order[12] = { 0, 1, 2, 3, 4, 6,
                                    8, 9, 10, 11, 5, 7 };
        const int h = area.getHeight() / 2, w = area.getWidth() / 6;
        for (int pos = 0; pos < 12; ++pos)
        {
            auto cell = juce::Rectangle<int> (area.getX() + (pos % 6) * w, area.getY() + (pos / 6) * h, w, h).reduced (3, 3);
            tiles[1].push_back (cell);
            placeKnob (fxKnobs[(size_t) order[pos]], cell);
        }
    }

    void layoutSlots()
    {
        layoutFx();
        layoutInserts();
        tiles[0].clear();
        auto area = slotArea.reduced (8, 8);
        noSlots.setBounds (slotArea);
        // Up to 18 controls go six to a row (bigger dials); engines with more use eight
        const int perRow = usedSlots <= 18 ? 6 : 8;
        const int rowsNeeded = juce::jmax (1, (usedSlots + perRow - 1) / perRow);
        const int w = area.getWidth() / perRow, h = juce::jmin (130, area.getHeight() / rowsNeeded);
        juce::Rectangle<int> row;
        for (int k = 0; k < usedSlots; ++k)
        {
            if (k % perRow == 0)
                row = area.removeFromTop (h);
            auto cell = row.removeFromLeft (w).reduced (3, 3);
            tiles[0].push_back (cell);
            placeKnob (slotKnobs[(size_t) k], cell);
        }
        repaint();
    }

    void timerCallback() override
    {
        auto value = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID())->load(); };
        envelope.setValues (value (params::layerAttack (layer)), value (params::layerDecay (layer)),
                            value (params::layerSustain (layer)) * 0.01f, value (params::layerRelease (layer)));

        const int mode = juce::roundToInt (value (params::layerVoiceMode (layer)));
        for (int m = 0; m < (int) modeButtons.size(); ++m)
            highlight (modeButtons[(size_t) m], m == mode, accent);
        const int type = juce::roundToInt (value (params::layerFilterType (layer)));
        for (int t = 0; t < (int) filterTypeButtons.size(); ++t)
            highlight (filterTypeButtons[(size_t) t], t == type, accent);
        for (int i = 0; i < (int) sectionButtons.size(); ++i)
            highlight (sectionButtons[(size_t) i], i == section, accent);
        const int role = juce::roundToInt (value (params::layerRole (layer)));
        for (int r = 0; r < (int) roleButtons.size(); ++r)
            highlight (roleButtons[(size_t) r], r == role, theme::roleColour (r));
        for (int k = 0; k < params::numInsertSlots; ++k)
            updateInsertSlot (k);
    }

    static void highlight (juce::TextButton& b, bool on, juce::Colour colour)
    {
        b.setColour (juce::TextButton::buttonColourId, on ? colour : theme::panel);
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

    juce::AudioProcessorValueTreeState& apvts;
    int layer = 0;
    juce::String roleName;
    juce::Colour accent { theme::mid };
    EnvelopeView envelope;
    juce::TextButton engineButton;
    std::array<Knob, 15> knobs;
    std::array<Knob, Engine::numSlots> slotKnobs;
    juce::Label noSlots;
    juce::Rectangle<int> slotArea;
    const EngineInfo* shownInfo = nullptr;   // the engine the slot knobs are attached for
    bool slotsStale = true;
    int usedSlots = 0;
    std::array<juce::TextButton, 3> modeButtons;
    std::array<juce::TextButton, 5> roleButtons;
    std::array<Knob, 12> fxKnobs;
    std::array<juce::TextButton, 5> filterTypeButtons;
    std::array<juce::TextButton, 4> sectionButtons;
    int section = 0;   // engine, filter / FX, inserts, amp / voice
    std::array<std::vector<juce::Rectangle<int>>, 4> tiles;   // each section's knob cells, for paint()
    struct InsertSlot
    {
        juce::TextButton type;
        Knob amount;
        std::array<Knob, 3> controls;
        int shownType = -1;
    };
    std::array<InsertSlot, params::numInsertSlots> insertSlots;
    juce::TextButton insertSwap;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> insertSwapAttachment;
};
}
