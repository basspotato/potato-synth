#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "EngineRegistry.h"

namespace
{
    const juce::Identifier layersNode { "Layers" };
    juce::Identifier engineProperty (int layerIndex) { return "L" + juce::String (layerIndex + 1) + "_engine"; }

    // Init patch until the real engines exist: the test engine on the Mid layer
    const char* const defaultEngines[ps::params::numLayers] = { "sub", "", "jp", "" };
}

PotatoSynthProcessor::PotatoSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PotatoSynth", ps::params::createLayout ([this] (int layer, int slot, float v) { return slotText (layer, slot, v); }))
{
    masterDb = apvts.getRawParameterValue (ps::params::master.getParamID());
    crossoverLowHz = apvts.getRawParameterValue (ps::params::crossoverLow.getParamID());
    monoBassOn = apvts.getRawParameterValue (ps::params::monoBass.getParamID());
    safeOutputOn = apvts.getRawParameterValue (ps::params::safeOutput.getParamID());
    for (int k = 0; k < ps::params::numChainKnobs; ++k)
        chainKnobs[(size_t) k] = apvts.getRawParameterValue (ps::params::chainKnob (k).getParamID());
    superKnob = apvts.getRawParameterValue (ps::params::super.getParamID());
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        namespace p = ps::params;
        auto raw = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID()); };
        stack.layer (i).setParameters ({ raw (p::layerLevel (i)), raw (p::layerPan (i)), raw (p::layerOctave (i)),
                                         raw (p::layerSemi (i)), raw (p::layerFine (i)), raw (p::layerAttack (i)),
                                         raw (p::layerDecay (i)), raw (p::layerSustain (i)), raw (p::layerRelease (i)),
                                         raw (p::layerVoiceMode (i)), raw (p::layerGlide (i)), raw (p::layerRole (i)),
                                         raw (p::crossoverLow), raw (p::crossoverHigh), {} });
        for (int k = 0; k < p::numEngineSlots; ++k)
            stack.layer (i).setSlotParameter (k, raw (p::layerSlot (i, k)));
    }

    auto layers = apvts.state.getOrCreateChildWithName (layersNode, nullptr);
    for (int i = 0; i < ps::params::numLayers; ++i)
        layers.setProperty (engineProperty (i), defaultEngines[i], nullptr);
    applyLayerEnginesFromState();
    // A fresh instance starts each default engine from its own slot defaults (the generic host default is 50 %)
    for (int i = 0; i < ps::params::numLayers; ++i)
        if (const auto* info = engineInfos[(size_t) i].load())
            for (int k = 0; k < (int) info->slots.size(); ++k)
                apvts.getParameter (ps::params::layerSlot (i, k).getParamID())->setValueNotifyingHost (info->slots[(size_t) k].defaultValue);

    startTimerHz (10);
}

void PotatoSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    stack.prepare (sampleRate, samplesPerBlock);
    bus.prepare (sampleRate, crossoverLowHz->load(), monoBassOn->load() > 0.5f);
    clipper.prepare (sampleRate, samplesPerBlock);
    limiter.prepare (sampleRate);
    lowFocus.prepare (sampleRate);
    pressure.prepare (sampleRate);
    tilt.prepare (sampleRate);
    deHarsh.prepare (sampleRate);
    pump.prepare (sampleRate);
    knobStagesIn.prepare (sampleRate);
    knobStagesOut.prepare (sampleRate);
    knobStagesActive = false;
    loudnessTrim.prepare (sampleRate, samplesPerBlock);
    for (int k = 0; k < ps::params::numChainKnobs; ++k)
        knobPosition[(size_t) k] = chainKnobs[(size_t) k]->load();
    setLatencySamples (clipper.getLatencySamples() + limiter.getLatencySamples());   // constant for a given sample rate
    stackGain.reset (sampleRate, 0.05);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterDb->load()));
}

void PotatoSynthProcessor::setLayerEngine (int layerIndex, const juce::String& engineId)
{
    apvts.state.getOrCreateChildWithName (layersNode, nullptr).setProperty (engineProperty (layerIndex), engineId, nullptr);
    auto engine = ps::engines::create (engineId);

    // A newly picked engine starts from its own defaults (loading state keeps the saved slot values instead)
    const auto* info = engine != nullptr ? &engine->info() : nullptr;
    for (int k = 0; k < ps::params::numEngineSlots; ++k)
    {
        auto* p = apvts.getParameter (ps::params::layerSlot (layerIndex, k).getParamID());
        const float value = info != nullptr && k < (int) info->slots.size() ? info->slots[(size_t) k].defaultValue : 0.5f;
        p->beginChangeGesture();
        p->setValueNotifyingHost (value);
        p->endChangeGesture();
    }

    engineInfos[(size_t) layerIndex] = info;
    stack.layer (layerIndex).requestEngine (std::move (engine), engineId);
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));   // slot value text follows the engine
}

juce::String PotatoSynthProcessor::slotText (int layer, int slot, float value) const
{
    const auto* info = engineInfos[(size_t) layer].load();
    if (info == nullptr || slot >= (int) info->slots.size())
        return "-";
    const auto& s = info->slots[(size_t) slot];
    return s.toText ? s.toText (value) : juce::String (juce::roundToInt (value * 100.0f)) + " %";
}

void PotatoSynthProcessor::applyLayerEnginesFromState()
{
    const auto layers = apvts.state.getChildWithName (layersNode);
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        const auto id = layers.getProperty (engineProperty (i)).toString();
        if (id != stack.layer (i).getEngineId())
        {
            auto engine = ps::engines::create (id);
            engineInfos[(size_t) i] = engine != nullptr ? &engine->info() : nullptr;
            stack.layer (i).requestEngine (std::move (engine), id);
        }
    }
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
}

bool PotatoSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PotatoSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    const auto offsets = updateChainKnobs (n);
    stack.process (buffer, midi);

    // The host's tempo and beat position (120 BPM, free-running, without a host transport)
    double bpm = 120.0, ppq = -1.0;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            if (const auto b = position->getBpm())
                bpm = *b;
            if (const auto p = position->getPpqPosition(); p && position->getIsPlaying())
                ppq = *p;
        }

    // Stack sum with sqrt-weights: the layers are scaled by 1 / sqrt(n), n the number of layers counted by
    // their level (a layer at 0 dB counts 1, a fading one less), so adding or fading in a layer keeps the
    // stack's power. A single quieter layer is never boosted.
    float layerCount = 0.0f;
    for (int i = 0; i < ps::params::numLayers; ++i)
        if (stack.layer (i).engine() != nullptr)
            layerCount += juce::square (juce::jmin (1.0f, stack.layer (i).patchGain()));
    stackGain.setTargetValue (1.0f / std::sqrt (juce::jmax (1.0f, layerCount)));
    stackGain.applyGain (buffer, n);
    bus.process (left, right, n, crossoverLowHz->load() * std::exp2 (offsets.monoBassOctaves), monoBassOn->load() > 0.5f);

    // The bus stages the knobs drive, measured in and out for the loudness trim (only while one is working)
    knobStagesIn.process (left, right, n);
    bool active = lowFocus.process (left, right, n, offsets.lowFocusDb);
    active = tilt.process (left, right, n, offsets.tiltDbPerOct) || active;
    active = deHarsh.process (left, right, n, offsets.deHarsh) || active;
    pressure.setTempoLock (offsets.tempoLock, 60.0 / juce::jlimit (20.0, 400.0, bpm));
    active = pressure.process (left, right, n, offsets.pressure) || active;
    knobStagesOut.process (left, right, n);
    knobStagesIn.update();
    knobStagesOut.update();
    if (! active && knobStagesActive)
    {
        knobStagesIn.reset();
        knobStagesOut.reset();
    }
    knobStagesActive = active;
    // The pump's power loss is known exactly, so it sits outside the measured stages
    pump.process (left, right, n, offsets.pumpDb, offsets.pumpHardness, offsets.pumpTau, bpm, ppq);

    // Harmony Engine, invariant 1: hold the loudness the patch has without the knobs
    std::array<float, ps::params::numLayers> power {}, patchGain {}, knobGain {};
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        const auto& layer = stack.layer (i);
        power[(size_t) i] = layer.measuredPower();
        patchGain[(size_t) i] = layer.patchGain();
        knobGain[(size_t) i] = layer.knobGain() * std::sqrt (layer.carvePowerRatio());
    }
    loudnessTrim.update (power.data(), patchGain.data(), knobGain.data(), ps::params::numLayers,
                         (active ? ps::harmony::stageRatio (knobStagesIn, knobStagesOut) : 1.0) * clipper.knobStageRatio()
                         / pump.powerRatio(), n);
    loudnessTrim.apply (left, right, n);

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterDb->load()));
    masterGain.applyGain (buffer, buffer.getNumSamples());

    // Safe output: a 4x oversampled soft clipper shapes overs musically, then a look-ahead limiter
    // guarantees the -1 dBTP ceiling the clipper's decimation filter can overshoot
    const bool safe = safeOutputOn->load() > 0.5f;
    clipper.process (buffer, safe, pressure.getAmount(), offsets.exciter);
    limiter.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples(), safe);
}

// Chain knob positions are smoothed (30 ms) at block rate, SUPER is folded in, and the offsets go to the layers
ps::chain::Offsets PotatoSynthProcessor::updateChainKnobs (int numSamples)
{
    const float coef = 1.0f - std::exp (-(float) numSamples / (0.03f * (float) getSampleRate()));
    const float super = superKnob->load();
    std::array<float, ps::chain::numKnobs> u {};
    for (int k = 0; k < ps::chain::numKnobs; ++k)
    {
        auto& p = knobPosition[(size_t) k];
        p += (chainKnobs[(size_t) k]->load() - p) * coef;
        u[(size_t) k] = juce::jlimit (0.0f, 1.0f, p + ps::chain::superContribution ((ps::chain::Knob) k, super));
    }
    std::array<ps::params::Role, ps::params::numLayers> roles {};
    for (int i = 0; i < ps::params::numLayers; ++i)
        roles[(size_t) i] = stack.layer (i).role();
    const auto offsets = ps::chain::compute (u, roles);

    // Band ownership, from the energies the layers measured last block
    std::array<std::array<float, ps::harmony::CarveBank::numBands>, ps::params::numLayers> energy {};
    std::array<bool, ps::params::numLayers> sounding {};
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        energy[(size_t) i] = stack.layer (i).carveEnergy();
        sounding[(size_t) i] = stack.layer (i).engine() != nullptr;
    }
    const bool carving = offsets.carveDepthDb > 1.0e-3f;
    const auto carve = ps::harmony::solveOwnership (energy, roles, sounding, offsets.carveTemperature, offsets.carveDepthDb);
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        stack.layer (i).setChainOffsets (offsets.levelDb[(size_t) i], offsets.slot[(size_t) i]);
        stack.layer (i).setOnsetDuck (offsets.onsetDuckDb[(size_t) i], offsets.onsetDuckMs);
        stack.layer (i).setCarveGains (carve[(size_t) i], carving);
    }
    return offsets;
}

// Parameters missing from older state (added in a later version) load at their defaults, not at whatever
// value this instance happened to hold.
void PotatoSynthProcessor::migrateState (juce::ValueTree& state) const
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! state.getChildWithProperty ("id", rp->getParameterID()).isValid())
                state.appendChild (juce::ValueTree ("PARAM", { { "id", rp->getParameterID() },
                                                               { "value", rp->convertFrom0to1 (rp->getDefaultValue()) } }),
                                   nullptr);
    state.setProperty ("stateVersion", ps::params::stateVersion, nullptr);
}

juce::AudioProcessorEditor* PotatoSynthProcessor::createEditor()
{
    return new PotatoSynthEditor (*this);
}

void PotatoSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", ps::params::stateVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PotatoSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    migrateState (state);
    apvts.replaceState (state);
    applyLayerEnginesFromState();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PotatoSynthProcessor();
}
