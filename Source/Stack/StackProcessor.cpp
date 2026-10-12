#include "StackProcessor.h"
#include "StackEditor.h"

VocalStackProcessor::VocalStackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VocalStackState", vs::createLayout())
{
}

void VocalStackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    work.setSize (2, std::max (1, samplesPerBlock));
}

bool VocalStackProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in == juce::AudioChannelSet::stereo()) return out == juce::AudioChannelSet::stereo();
    if (in == juce::AudioChannelSet::mono())   return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return false;
}

void VocalStackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (work.getNumSamples() < n) work.setSize (2, n, false, false, true);
    work.setSize (2, n, true, false, true);                      // keep the memory, change the length

    // the chain always works in stereo; a mono input feeds both sides
    work.copyFrom (0, 0, buffer, 0, 0, n);
    work.copyFrom (1, 0, buffer, inCh > 1 ? 1 : 0, 0, n);
    engine.process (work, vs::readParams (apvts));

    if (outCh >= 2 && buffer.getNumChannels() >= 2)
    {
        buffer.copyFrom (0, 0, work, 0, 0, n);
        buffer.copyFrom (1, 0, work, 1, 0, n);
    }
    else
    {
        // mono out: both sides summed, because the reverb and thickener are stereo
        for (int i = 0; i < n; ++i)
            buffer.getWritePointer (0)[i] = 0.5f * (work.getSample (0, i) + work.getSample (1, i));
    }
}

void VocalStackProcessor::applyPreset (int index)
{
    const auto& all = vs::presets();
    if (index < 0 || index >= (int) all.size()) return;
    const auto& p = all[(size_t) index];

    // start from the defaults so a preset only has to list what differs
    for (auto& f : vs::floatDefs())
        if (auto* q = apvts.getParameter (f.id))
            q->setValueNotifyingHost (q->convertTo0to1 (f.def));
    for (auto& kv : p.values)
        if (auto* q = apvts.getParameter (kv.first))
            q->setValueNotifyingHost (q->convertTo0to1 (kv.second));
}

juce::AudioProcessorEditor* VocalStackProcessor::createEditor() { return new VocalStackEditor (*this); }

void VocalStackProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void VocalStackProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VocalStackProcessor(); }
