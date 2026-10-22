#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "EngineRegistry.h"

namespace
{
    const juce::Identifier layersNode { "Layers" };
    juce::Identifier engineProperty (int layerIndex) { return "L" + juce::String (layerIndex + 1) + "_engine"; }

    // Init patch until the real engines exist: the test engine on the Mid layer
    const char* const defaultEngines[ps::params::numLayers] = { "", "", "test.sine", "" };
}

PotatoSynthProcessor::PotatoSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PotatoSynth", ps::params::createLayout())
{
    masterDb = apvts.getRawParameterValue (ps::params::master.getParamID());
    crossoverLowHz = apvts.getRawParameterValue (ps::params::crossoverLow.getParamID());
    monoBassOn = apvts.getRawParameterValue (ps::params::monoBass.getParamID());
    safeOutputOn = apvts.getRawParameterValue (ps::params::safeOutput.getParamID());
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        namespace p = ps::params;
        auto raw = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID()); };
        stack.layer (i).setParameters ({ raw (p::layerLevel (i)), raw (p::layerPan (i)), raw (p::layerOctave (i)),
                                         raw (p::layerSemi (i)), raw (p::layerFine (i)), raw (p::layerAttack (i)),
                                         raw (p::layerDecay (i)), raw (p::layerSustain (i)), raw (p::layerRelease (i)),
                                         raw (p::layerVoiceMode (i)), raw (p::layerGlide (i)), raw (p::layerRole (i)),
                                         raw (p::crossoverLow), raw (p::crossoverHigh) });
    }

    auto layers = apvts.state.getOrCreateChildWithName (layersNode, nullptr);
    for (int i = 0; i < ps::params::numLayers; ++i)
        layers.setProperty (engineProperty (i), defaultEngines[i], nullptr);
    applyLayerEnginesFromState();

    startTimerHz (10);
}

void PotatoSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    stack.prepare (sampleRate, samplesPerBlock);
    bus.prepare (sampleRate, crossoverLowHz->load(), monoBassOn->load() > 0.5f);
    clipper.prepare (sampleRate, samplesPerBlock);
    limiter.prepare (sampleRate);
    setLatencySamples (clipper.getLatencySamples() + limiter.getLatencySamples());   // constant for a given sample rate
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterDb->load()));
}

void PotatoSynthProcessor::setLayerEngine (int layerIndex, const juce::String& engineId)
{
    apvts.state.getOrCreateChildWithName (layersNode, nullptr).setProperty (engineProperty (layerIndex), engineId, nullptr);
    stack.layer (layerIndex).requestEngine (ps::engines::create (engineId), engineId);
}

void PotatoSynthProcessor::applyLayerEnginesFromState()
{
    const auto layers = apvts.state.getChildWithName (layersNode);
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        const auto id = layers.getProperty (engineProperty (i)).toString();
        if (id != stack.layer (i).getEngineId())
            stack.layer (i).requestEngine (ps::engines::create (id), id);
    }
}

bool PotatoSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PotatoSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    stack.process (buffer, midi);
    bus.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples(),
                 crossoverLowHz->load(), monoBassOn->load() > 0.5f);

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterDb->load()));
    masterGain.applyGain (buffer, buffer.getNumSamples());

    // Safe output: a 4x oversampled soft clipper shapes overs musically, then a look-ahead limiter
    // guarantees the -1 dBTP ceiling the clipper's decimation filter can overshoot
    const bool safe = safeOutputOn->load() > 0.5f;
    clipper.process (buffer, safe);
    limiter.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples(), safe);
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
