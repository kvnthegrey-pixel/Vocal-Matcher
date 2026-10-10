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
    keyParam        = apvts.getRawParameterValue ("key");
    scaleParam      = apvts.getRawParameterValue ("scale");
    octaveParam     = apvts.getRawParameterValue ("octave");
    offsetParam     = apvts.getRawParameterValue ("refOffset");
    greenParam      = apvts.getRawParameterValue ("greenCents");
    amberParam      = apvts.getRawParameterValue ("amberCents");
    a4Param         = apvts.getRawParameterValue ("a4");
    latencyParam    = apvts.getRawParameterValue ("latencyMs");

    reference.addChangeListener (this);
    matcherObj.setSettings (currentMatchSettings());
    startTimerHz (30);
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
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "auditionMode", 1 }, "Comparison sound",
                                                       StringArray { "This track (Cubase output)", "Windows output" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key",
                                                       StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale",
                                                       StringArray { "Off", "Major", "Minor" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "octave", 1 }, "Reference octave shift", -2, 2, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "refOffset", 1 }, "Reference start in project (s)",
                                                      NormalisableRange<float> (-60.0f, 600.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "greenCents", 1 }, "Green threshold (cents)",
                                                      NormalisableRange<float> (5.0f, 50.0f, 1.0f), 25.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "amberCents", 1 }, "Amber threshold (cents)",
                                                      NormalisableRange<float> (50.0f, 200.0f, 1.0f), 100.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "a4", 1 }, "A4 reference (Hz)",
                                                      NormalisableRange<float> (430.0f, 450.0f, 0.1f), 440.0f));
    return layout;
}

void VocalAssistorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // The audio is never delayed, so no latency is reported to the host.
    setLatencySamples (0);
    currentSampleRate = sampleRate;
    toneBuf.assign ((size_t) std::max (samplesPerBlock, 1), 0.0f);
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
    // The host hands us the same buffer for input and output. Nothing writes to it
    // except the comparison sound below, so the audio is a bit-exact pass-through otherwise. The pitch
    // engine only reads the buffer (and copies it into a FIFO).
    engine.setMethod (methodParam->load() < 0.5f ? va::PitchTracker::Method::McLeod
                                                 : va::PitchTracker::Method::Yin);
    engine.setMcLeodGate (mcleodGateParam->load());
    engine.setYinGate (yinGateParam->load());

    int64_t hostSample = 0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto s = pos->getTimeInSamples())
                hostSample = *s;
            else
                playing = false;
        }
    hostSampleAtBlock = hostSample;
    hostWallMs = juce::Time::getMillisecondCounterHiRes();
    hostPlaying = playing;
    engine.pushAudio (buffer, hostSample, playing);

    // The comparison sound (reference, gap, you) is added to the output only while it is sounding.
    const int counter = reqCounter.load();
    if (counter != seenCounter)
    {
        seenCounter = counter;
        tone.start (reqRef.load(), reqSung.load(), reqLen.load(), currentSampleRate.load());
    }
    if (tone.isActive())
    {
        const int total = buffer.getNumSamples();
        const int chunk = (int) toneBuf.size();
        for (int done = 0; done < total && chunk > 0; done += chunk)
        {
            const int n = std::min (chunk, total - done);
            tone.render (toneBuf.data(), n);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                float* d = buffer.getWritePointer (ch) + done;
                for (int i = 0; i < n; ++i) d[i] += toneBuf[(size_t) i];
            }
        }
    }
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
    juce::ignoreUnused (info);
    return true;
}

va::MatchSettings VocalAssistorProcessor::currentMatchSettings() const
{
    va::MatchSettings s;
    s.greenCents = greenParam->load();
    s.amberCents = std::max ((double) amberParam->load(), s.greenCents);
    s.a4 = a4Param->load();
    s.octaveShift = (int) std::lround (octaveParam->load());
    s.refOffsetSec = offsetParam->load();
    return s;
}

va::ViewSettings VocalAssistorProcessor::viewSettings() const
{
    va::ViewSettings v;
    v.refOffsetSec = offsetParam->load();
    v.octaveShift = (int) std::lround (octaveParam->load());
    v.keyRoot = (int) std::lround (keyParam->load());
    v.scale = (int) std::lround (scaleParam->load());
    v.a4 = a4Param->load();
    return v;
}

double VocalAssistorProcessor::playheadSeconds() const
{
    const double sr = currentSampleRate.load();
    double t = (double) hostSampleAtBlock.load() / sr;
    if (hostPlaying.load())
        t += juce::jlimit (0.0, 0.25, (juce::Time::getMillisecondCounterHiRes() - hostWallMs.load()) * 0.001);
    return t;
}

void VocalAssistorProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    matcherObj.setBlocks (reference.blocksCopy().blocks());
    matcherObj.setSettings (currentMatchSettings());
}

void VocalAssistorProcessor::updatePerformance()
{
    const auto settings = currentMatchSettings();
    if (! (settings == matcherObj.settings()))
    {
        if (settings.a4 != matcherObj.settings().a4)
            traceStore.clear();          // trace heights were computed at the old A4
        matcherObj.setSettings (settings);
    }

    const double sr = engine.getSampleRate();
    const double latencySec = (double) latencyParam->load() * 0.001;
    const double frameSec = (double) va::PitchTracker::hopSize / sr;

    va::PitchPoint pts[256];
    int n;
    while ((n = engine.popPoints (pts, 256)) > 0)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto& p = pts[i];
            latestPoint = p;
            haveLatestPoint = true;
            if (! p.hostValid)
                continue;

            if (p.passId != lastPassId)
            {
                lastPassId = p.passId;
                const double start = (double) (p.hostSample) / sr + latencySec;
                traceStore.eraseFrom (start);
                matcherObj.resetFrom (start);
            }

            const double t = (double) p.hostSample / sr + latencySec;
            va::TracePoint tp;
            tp.timeSec = t;
            tp.voiced = p.voiced;
            if (p.voiced)
            {
                tp.midi = (float) (69.0 + 12.0 * std::log2 ((double) p.hz / settings.a4));
                tp.confidence = methodParam->load() < 0.5f ? juce::jlimit (0.0f, 1.0f, p.confidence)
                                                           : juce::jlimit (0.0f, 1.0f, 1.0f - p.confidence);
            }
            traceStore.add (tp);
            matcherObj.onPoint (t, frameSec, p.voiced, p.voiced ? (double) p.hz : 0.0);
        }
    }

    matcherObj.onPlayhead (playheadSeconds());
}

void VocalAssistorProcessor::timerCallback()
{
    updatePerformance();

    va::AudioAnalysisResult result;
    if (job.takeResult (result))
    {
        if (result.ok)
            reference.setReference (std::move (result.blocks), result.info);
        else if (! result.cancelled)
            analysisError = result.error.isNotEmpty() ? result.error : juce::String ("The analysis failed.");
        reference.sendChangeMessage();
    }
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
            matcherObj.setBlocks (reference.blocksCopy().blocks());
            matcherObj.setSettings (currentMatchSettings());
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalAssistorProcessor();
}
