#include "PluginEditor.h"
#include "EngineRegistry.h"

namespace
{
    const char* const chainNames[] = { "MASS", "SCALE", "PRESSURE", "GROOVE", "CLARITY", "SHINE", "MORPH", "BLOOM" };
    constexpr int baseWidth = 1180, baseHeight = 720;

    juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz * 0.001f, 1) + " KHZ" : juce::String (juce::roundToInt (hz)) + " HZ";
    }

    // The band a role covers, as shown on its layer card (matches Layer::updateBand)
    juce::String bandText (int role, float low, float high)
    {
        switch ((ps::params::Role) role)
        {
            case ps::params::Role::sub:  return "MONO  <" + hzText (low);
            case ps::params::Role::body: return hzText (low) + " - " + hzText (high);
            case ps::params::Role::mid:  return ">" + hzText (2.0f * low) + "  WIDE";
            case ps::params::Role::air:  return ">" + hzText (high) + "  WIDEST";
            case ps::params::Role::full: break;
        }
        return "FULL RANGE";
    }

    juce::String roleTitle (int role) { return ps::params::roleNames[juce::jlimit (0, 4, role)].toUpperCase(); }
}

PotatoSynthEditor::PotatoSynthEditor (PotatoSynthProcessor& p)
    : AudioProcessorEditor (&p), processor (p), layerPage (p.apvts)
{
    setLookAndFeel (&look);

    for (int i = 0; i < 4; ++i)
    {
        layers[(size_t) i] = std::make_unique<ps::LayerCard> (roleTitle (i), juce::String(), ps::theme::roleColour (i));
        addAndMakeVisible (*layers[(size_t) i]);
        layers[(size_t) i]->onSelect = [this, i] { selectLayer (i); showPage (true); };
        layers[(size_t) i]->onEngineClick = [this, i] { selectLayer (i); showEngineMenu (i); };

        auto& k = layerLevels[(size_t) i];
        setUpKnob (k, "LEVEL", ps::theme::roleColour (i));
        k.label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        layerLevelAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, ps::params::layerLevel (i).getParamID(), k.slider);
        k.slider.setDoubleClickReturnValue (true, 0.0);
    }

    addAndMakeVisible (xyPad);
    addChildComponent (layerPage);
    layerPage.onEngineClick = [this] { showEngineMenu (selectedLayer); };
    for (auto* tab : { &stackTab, &layerTab })
    {
        tab->setClickingTogglesState (false);
        addAndMakeVisible (*tab);
    }
    stackTab.onClick = [this] { showPage (false); };
    layerTab.onClick = [this] { showPage (true); };
    selectLayer (selectedLayer);
    showPage (false);

    for (int i = 0; i < 8; ++i)
        setUpKnob (chain[(size_t) i], chainNames[i], ps::theme::chainAccent (i));
    setUpKnob (super, "SUPER", juce::Colours::white);
    setUpKnob (master, "MASTER", ps::theme::mid);

    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, ps::params::master.getParamID(), master.slider);
    master.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    master.slider.textFromValueFunction = [] (double v) { return juce::String (std::abs (v) < 0.05 ? 0.0 : v, 1) + " dB"; };
    master.slider.updateText();

    setUpBusKnob (crossoverLow, "LOW X", ps::params::crossoverLow, crossoverLowAttachment);
    setUpBusKnob (crossoverHigh, "HIGH X", ps::params::crossoverHigh, crossoverHighAttachment);
    monoBass.setClickingTogglesState (true);
    monoBass.setColour (juce::TextButton::buttonOnColourId, ps::theme::sub);
    monoBass.setColour (juce::TextButton::buttonColourId, ps::theme::panel);
    monoBassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, ps::params::monoBass.getParamID(), monoBass);
    addAndMakeVisible (monoBass);

    build.setColour (juce::TextButton::buttonColourId, ps::theme::mid);
    drop.setColour (juce::TextButton::buttonColourId, ps::theme::sub);
    addAndMakeVisible (build);
    addAndMakeVisible (drop);

    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setSize (baseWidth, baseHeight);
    timerCallback();
    startTimerHz (5);
}

void PotatoSynthEditor::timerCallback()
{
    for (int i = 0; i < 4; ++i)
    {
        juce::String name;
        const auto id = processor.getLayerEngine (i);
        for (auto& e : ps::engines::all())
            if (id == e.id)
                name = e.name;
        layers[(size_t) i]->setEngineName (name);
        if (i == selectedLayer)
            layerPage.setEngine (name, processor.getLayerEngineInfo (i));
    }

    // Roles and crossovers drive each card's title, band and colour
    const float low = processor.apvts.getRawParameterValue (ps::params::crossoverLow.getParamID())->load();
    const float high = processor.apvts.getRawParameterValue (ps::params::crossoverHigh.getParamID())->load();
    const bool crossoversMoved = std::abs (low - shownLow) > 0.05f || std::abs (high - shownHigh) > 0.5f;
    shownLow = low;
    shownHigh = high;
    for (int i = 0; i < 4; ++i)
    {
        const int role = layerRole (i);
        if (role == shownRoles[(size_t) i] && ! crossoversMoved)
            continue;
        const bool roleChanged = role != shownRoles[(size_t) i];
        shownRoles[(size_t) i] = role;
        const auto colour = ps::theme::roleColour (role);
        layers[(size_t) i]->setRole (roleTitle (role), bandText (role, low, high), colour);
        layerLevels[(size_t) i].slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        layerLevels[(size_t) i].label.setColour (juce::Label::textColourId, colour.interpolatedWith (ps::theme::text, 0.35f));
        if (roleChanged && i == selectedLayer)
            selectLayer (i);
    }
}

int PotatoSynthEditor::layerRole (int layerIndex) const
{
    return juce::jlimit (0, 4, juce::roundToInt (processor.apvts.getRawParameterValue (
        ps::params::layerRole (layerIndex).getParamID())->load()));
}

void PotatoSynthEditor::setUpBusKnob (Knob& k, const juce::String& name, const juce::ParameterID& id,
                                      std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
{
    setUpKnob (k, name, ps::theme::full);
    k.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    k.label.setJustificationType (juce::Justification::centredLeft);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id.getParamID(), k.slider);
    auto* p = processor.apvts.getParameter (id.getParamID());
    k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    k.slider.onValueChange = [&k, name]
    {
        k.label.setText (name + "\n" + k.slider.getTextFromValue (k.slider.getValue()), juce::dontSendNotification);
        k.slider.repaint();
    };
    k.slider.onValueChange();
}

void PotatoSynthEditor::selectLayer (int layerIndex)
{
    selectedLayer = layerIndex;
    for (int i = 0; i < 4; ++i)
        layers[(size_t) i]->setSelected (i == layerIndex);
    const int role = layerRole (layerIndex);
    layerPage.showLayer (layerIndex, roleTitle (role), ps::theme::roleColour (role));
    timerCallback();   // brings the page's engine controls up for this layer
    layerTab.setButtonText ("LAYER " + juce::String (layerIndex + 1) + ": " + roleTitle (role));
    layerTab.setColour (juce::TextButton::buttonColourId, ps::theme::roleColour (role));
}

void PotatoSynthEditor::showPage (bool layerPageVisible)
{
    layerPage.setVisible (layerPageVisible);
    xyPad.setVisible (! layerPageVisible);
    stackTab.setColour (juce::TextButton::buttonColourId, layerPageVisible ? ps::theme::panel : ps::theme::air);
    layerTab.setAlpha (layerPageVisible ? 1.0f : 0.6f);
    stackTab.setAlpha (layerPageVisible ? 0.6f : 1.0f);
}

void PotatoSynthEditor::showEngineMenu (int layerIndex)
{
    juce::PopupMenu menu;
    const auto current = processor.getLayerEngine (layerIndex);
    menu.addItem (1, "Empty", true, current.isEmpty());
    int itemId = 2;
    for (auto& e : ps::engines::all())
        menu.addItem (itemId++, e.name, true, current == e.id);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (layers[(size_t) layerIndex].get()),
                        [safe = juce::Component::SafePointer<PotatoSynthEditor> (this), layerIndex] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            const auto& all = ps::engines::all();
                            const juce::String id = result == 1 ? juce::String() : juce::String (all[(size_t) (result - 2)].id);
                            safe->processor.setLayerEngine (layerIndex, id);
                            safe->timerCallback();
                        });
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
    for (size_t i = 0; i < layers.size(); ++i)
    {
        auto card = left.removeFromTop (cardH);
        layers[i]->setBounds (card);
        auto knobArea = card.removeFromRight (scaled (78)).reduced (scaled (6));
        layerLevels[i].label.setBounds (knobArea.removeFromBottom (scaled (14)));
        layerLevels[i].slider.setBounds (knobArea);
        left.removeFromTop (scaled (12));
    }

    area.removeFromLeft (scaled (20));
    auto right = area.removeFromRight (scaled (220));
    area.removeFromRight (scaled (20));
    auto tabs = area.withY (scaled (24)).withHeight (scaled (34));
    stackTab.setBounds (tabs.removeFromLeft (scaled (120)));
    tabs.removeFromLeft (scaled (8));
    layerTab.setBounds (tabs.removeFromLeft (scaled (180)));

    // Stack bus controls, top right
    auto bus = juce::Rectangle<int> (area.getRight() - scaled (420), scaled (20), scaled (420) + scaled (240), scaled (42));
    bus = bus.withRight (getWidth() - scaled (24));
    for (auto* k : { &crossoverHigh, &crossoverLow })
    {
        auto cell = bus.removeFromRight (scaled (130));
        k->slider.setBounds (cell.removeFromLeft (scaled (42)));
        k->label.setBounds (cell.withTrimmedLeft (scaled (4)));
        bus.removeFromRight (scaled (8));
    }
    monoBass.setBounds (bus.removeFromRight (scaled (120)).reduced (0, scaled (4)));
    xyPad.setBounds (area);
    layerPage.setBounds (area);

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
