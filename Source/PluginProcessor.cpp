#include "PluginProcessor.h"
#include "PluginEditor.h"

VocalAssistorProcessor::VocalAssistorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void VocalAssistorProcessor::prepareToPlay (double, int)
{
    // The audio is never delayed, so no latency is reported to the host.
    setLatencySamples (0);
}

bool VocalAssistorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Normal mono or stereo insert: input and output must match.
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void VocalAssistorProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    // Intentionally empty. The host hands us the same buffer for input and
    // output, so leaving it untouched is a bit-exact pass-through. Pitch
    // analysis is added in Phase 2 and only ever reads from this buffer.
}

juce::AudioProcessorEditor* VocalAssistorProcessor::createEditor()
{
    return new VocalAssistorEditor (*this);
}

void VocalAssistorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("VocalAssistorState");
    state.setProperty ("version", stateVersion, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalAssistorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        juce::ignoreUnused (state); // Settings are restored here from Phase 3 onward.
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalAssistorProcessor();
}
