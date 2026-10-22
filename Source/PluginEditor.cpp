#include "PluginEditor.h"

namespace
{
    const char* const chainNames[] = { "MASS", "SCALE", "PRESSURE", "GROOVE", "CLARITY", "SHINE", "MORPH", "BLOOM" };
    constexpr int baseWidth = 1180, baseHeight = 720;
}

PotatoSynthEditor::PotatoSynthEditor (PotatoSynthProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look);

    layers[0] = std::make_unique<ps::LayerCard> ("SUB",  "MONO  <120 HZ",       ps::theme::sub);
    layers[1] = std::make_unique<ps::LayerCard> ("BODY", "120 HZ - 3 KHZ",      ps::theme::body);
    layers[2] = std::make_unique<ps::LayerCard> ("MID",  "300 HZ - 3 KHZ",      ps::theme::mid);
    layers[3] = std::make_unique<ps::LayerCard> ("AIR",  "WIDE  >2.5 KHZ",      ps::theme::air);
    for (auto& l : layers)
        addAndMakeVisible (*l);

    addAndMakeVisible (xyPad);

    for (int i = 0; i < 8; ++i)
        setUpKnob (chain[(size_t) i], chainNames[i], ps::theme::chainAccent (i));
    setUpKnob (super, "SUPER", juce::Colours::white);
    setUpKnob (master, "MASTER", ps::theme::mid);

    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, ps::params::master.getParamID(), master.slider);
    master.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    master.slider.textFromValueFunction = [] (double v) { return juce::String (std::abs (v) < 0.05 ? 0.0 : v, 1) + " dB"; };
    master.slider.updateText();

    build.setColour (juce::TextButton::buttonColourId, ps::theme::mid);
    drop.setColour (juce::TextButton::buttonColourId, ps::theme::sub);
    addAndMakeVisible (build);
    addAndMakeVisible (drop);

    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setSize (baseWidth, baseHeight);
}

PotatoSynthEditor::~PotatoSynthEditor()
{
    setLookAndFeel (nullptr);
}

void PotatoSynthEditor::setUpKnob (Knob& k, const juce::String& name, juce::Colour accent)
{
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setRange (0.0, 1.0);
    k.slider.setValue (0.0, juce::dontSendNotification);
    k.slider.setDoubleClickReturnValue (true, 0.0);
    k.slider.onValueChange = [&k] { k.slider.repaint(); };
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, accent.interpolatedWith (ps::theme::text, 0.35f));
    addAndMakeVisible (k.label);
}

void PotatoSynthEditor::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (ps::theme::bgTop, 0.0f, 0.0f,
                                             ps::theme::bgBottom, 0.0f, r.getBottom(), false));
    g.fillAll();

    // Soft colour washes, one per role, for depth
    const juce::Colour washes[] = { ps::theme::sub, ps::theme::body, ps::theme::mid, ps::theme::air };
    const juce::Point<float> at[] = { { 0.1f, 0.2f }, { 0.9f, 0.15f }, { 0.2f, 0.95f }, { 0.85f, 0.9f } };
    for (int i = 0; i < 4; ++i)
    {
        const auto c = juce::Point<float> (r.getWidth() * at[i].x, r.getHeight() * at[i].y);
        juce::ColourGradient wash (washes[i].withAlpha (0.10f), c.x, c.y,
                                   washes[i].withAlpha (0.0f), c.x + r.getWidth() * 0.45f, c.y, true);
        g.setGradientFill (wash);
        g.fillRect (r);
    }

    const float s = r.getWidth() / (float) baseWidth;
    g.setColour (ps::theme::text);
    g.setFont (juce::FontOptions (30.0f * s, juce::Font::bold));
    g.drawText ("POTATO-SYNTH", juce::Rectangle<float> (24.0f * s, 14.0f * s, 400.0f * s, 36.0f * s),
                juce::Justification::centredLeft);
    g.setColour (ps::theme::textDim);
    g.setFont (juce::FontOptions (13.0f * s));
    g.drawText (juce::CharPointer_UTF8 ("SUPER STACK  \xc2\xb7  PREVIEW BUILD"), juce::Rectangle<float> (26.0f * s, 48.0f * s, 400.0f * s, 18.0f * s),
                juce::Justification::centredLeft);
    g.drawText (juce::CharPointer_UTF8 ("HARMONY ENGINE  \xc2\xb7  ARRIVES IN PHASE 3"),
                juce::Rectangle<float> (0.0f, r.getBottom() - 26.0f * s, r.getWidth() - 24.0f * s, 18.0f * s),
                juce::Justification::centredRight);
}

void PotatoSynthEditor::resized()
{
    const float s = (float) getWidth() / (float) baseWidth;
    auto scaled = [s] (int v) { return juce::roundToInt ((float) v * s); };

    auto area = getLocalBounds().reduced (scaled (24));
    area.removeFromTop (scaled (56));
    area.removeFromBottom (scaled (16));

    auto chainRow = area.removeFromBottom (scaled (150));
    area.removeFromBottom (scaled (16));

    auto left = area.removeFromLeft (scaled (280));
    const int cardH = (left.getHeight() - scaled (12) * 3) / 4;
    for (auto& l : layers)
    {
        l->setBounds (left.removeFromTop (cardH));
        left.removeFromTop (scaled (12));
    }

    area.removeFromLeft (scaled (20));
    auto right = area.removeFromRight (scaled (220));
    area.removeFromRight (scaled (20));
    xyPad.setBounds (area);

    auto superArea = right.removeFromTop (scaled (210));
    super.label.setBounds (superArea.removeFromBottom (scaled (20)));
    super.slider.setBounds (superArea);
    right.removeFromTop (scaled (10));
    auto buttons = right.removeFromTop (scaled (44));
    build.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (scaled (4), 0));
    drop.setBounds (buttons.reduced (scaled (4), 0));
    right.removeFromTop (scaled (10));
    master.label.setBounds (right.removeFromBottom (scaled (20)));
    master.slider.setBounds (right);

    const int w = chainRow.getWidth() / 8;
    for (auto& k : chain)
    {
        auto cell = chainRow.removeFromLeft (w).reduced (scaled (6), 0);
        k.label.setBounds (cell.removeFromBottom (scaled (20)));
        k.slider.setBounds (cell);
    }
}
