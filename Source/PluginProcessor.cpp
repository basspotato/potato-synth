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
    for (int i = 0; i < ps::params::numLayers; ++i)
        stack.layer (i).setLevelParameter (apvts.getRawParameterValue (ps::params::layerLevel (i).getParamID()));

    auto layers = apvts.state.getOrCreateChildWithName (layersNode, nullptr);
    for (int i = 0; i < ps::params::numLayers; ++i)
        layers.setProperty (engineProperty (i), defaultEngines[i], nullptr);
    applyLayerEnginesFromState();

    startTimerHz (10);
}

void PotatoSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    stack.prepare (sampleRate, samplesPerBlock);
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

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterDb->load()));
    masterGain.applyGain (buffer, buffer.getNumSamples());
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
    // Older state versions get migrated here before being applied (none exist yet).
    apvts.replaceState (state);
    applyLayerEnginesFromState();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PotatoSynthProcessor();
}
