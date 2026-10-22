#include "PluginProcessor.h"
#include "PluginEditor.h"

PotatoSynthProcessor::PotatoSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PotatoSynth", ps::params::createLayout())
{
}

void PotatoSynthProcessor::prepareToPlay (double, int)
{
}

bool PotatoSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PotatoSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // No sound sources yet (Phase 0): the stack and voices arrive in Phase 1.
    buffer.clear();
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
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PotatoSynthProcessor();
}
