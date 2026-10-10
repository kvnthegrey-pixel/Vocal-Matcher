#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MidiReference.h"

VocalAssistorProcessor::VocalAssistorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VocalAssistorState", createLayout())
{
    methodParam     = apvts.getRawParameterValue ("method");
    mcleodGateParam = apvts.getRawParameterValue ("gateMcLeod");
    yinGateParam    = apvts.getRawParameterValue ("gateYin");
}

juce::AudioProcessorValueTreeState::ParameterLayout VocalAssistorProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "method", 1 }, "Pitch method",
                                                       StringArray { "McLeod", "YIN" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gateMcLeod", 1 }, "Voicing gate (McLeod)",
                                                      NormalisableRange<float> (0.5f, 0.99f, 0.01f), va::PitchTracker::defaultMcLeodGate));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gateYin", 1 }, "Voicing gate (YIN)",
                                                      NormalisableRange<float> (0.05f, 0.40f, 0.01f), va::PitchTracker::defaultYinGate));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "latencyMs", 1 }, "Live trace latency compensation",
                                                      NormalisableRange<float> (-50.0f, 50.0f, 0.5f), 0.0f));
    return layout;
}

void VocalAssistorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // The audio is never delayed, so no latency is reported to the host.
    setLatencySamples (0);
    engine.start (sampleRate, samplesPerBlock);
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

void VocalAssistorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // The host hands us the same buffer for input and output, and nothing
    // here writes to it, so the audio is a bit-exact pass-through. The pitch
    // engine only reads the buffer (and copies it into a FIFO).
    engine.setMethod (methodParam->load() < 0.5f ? va::PitchTracker::Method::McLeod
                                                 : va::PitchTracker::Method::Yin);
    engine.setMcLeodGate (mcleodGateParam->load());
    engine.setYinGate (yinGateParam->load());
    engine.pushAudio (buffer);
}

juce::String VocalAssistorProcessor::loadReferenceMidi (const juce::File& file, int trackIndex)
{
    juce::FileInputStream in (file);
    juce::MidiFile midi;
    if (! in.openedOk() || ! midi.readFrom (in))
        return "That file could not be read as a MIDI file.";

    va::AnalysisInfo info;
    auto blocks = va::MidiReference::importTrack (midi, trackIndex, info.rules);
    if (blocks.empty())
        return "That track has no notes long enough to use (the minimum is 80 ms).";

    info.kind = va::SourceKind::Midi;
    info.sourceName = file.getFileName();
    info.midiTrack = trackIndex;
    info.quality = va::Quality::Green;      // MIDI is exact
    reference.setReference (std::move (blocks), info);
    return {};
}

bool VocalAssistorProcessor::loadReferenceAudio (const juce::File& file)
{
    analysisError.clear();
    const auto info = reference.infoCopy();
    if (! job.start (file, va::SegmenterRules(), va::PyinSettings()))
        return false;
    startTimerHz (10);
    juce::ignoreUnused (info);
    return true;
}

void VocalAssistorProcessor::timerCallback()
{
    va::AudioAnalysisResult result;
    if (job.takeResult (result))
    {
        stopTimer();
        if (result.ok)
            reference.setReference (std::move (result.blocks), result.info);
        else if (! result.cancelled)
            analysisError = result.error.isNotEmpty() ? result.error : juce::String ("The analysis failed.");
        reference.sendChangeMessage();
    }
    else if (! job.isRunning())
        stopTimer();
}

juce::AudioProcessorEditor* VocalAssistorProcessor::createEditor()
{
    return new VocalAssistorEditor (*this);
}

void VocalAssistorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("version", stateVersion, nullptr);
    state.removeChild (state.getChildWithName ("Reference"), nullptr);
    state.addChild (reference.toValueTree(), -1, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalAssistorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            auto ref = tree.getChildWithName ("Reference");
            if (ref.isValid())
            {
                tree.removeChild (ref, nullptr);
                reference.fromValueTree (ref);       // exact blocks back, no re-analysis
            }
            apvts.replaceState (tree);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalAssistorProcessor();
}
