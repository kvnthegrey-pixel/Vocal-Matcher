#include "AssistProcessor.h"
#include "AssistEditor.h"
#include "../MidiReference.h"

AssistProcessor::AssistProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PerformanceAssistState", createLayout())
{
    methodParam     = apvts.getRawParameterValue ("method");
    mcleodGateParam = apvts.getRawParameterValue ("gateMcLeod");
    yinGateParam    = apvts.getRawParameterValue ("gateYin");
    latencyParam    = apvts.getRawParameterValue ("latencyMs");
    keyParam        = apvts.getRawParameterValue ("key");
    scaleParam      = apvts.getRawParameterValue ("scale");
    octaveParam     = apvts.getRawParameterValue ("octave");
    offsetParam     = apvts.getRawParameterValue ("refOffset");
    perfectParam    = apvts.getRawParameterValue ("perfectCents");
    goodParam       = apvts.getRawParameterValue ("goodCents");
    a4Param         = apvts.getRawParameterValue ("a4");
    modeParam       = apvts.getRawParameterValue ("mode");
    focusParam      = apvts.getRawParameterValue ("focus");
    gainParam       = apvts.getRawParameterValue ("waveGain");
    windowParam     = apvts.getRawParameterValue ("windowSec");
    graceParam      = apvts.getRawParameterValue ("graceMs");
    smoothParam     = apvts.getRawParameterValue ("smoothMs");

    reference.addChangeListener (this);
    matcherObj.setSettings (currentMatchSettings());
    keeper.setSettings (scoreSettings());
    startTimerHz (30);
}

juce::AudioProcessorValueTreeState::ParameterLayout AssistProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "method", 1 }, "Pitch method",
                                                       StringArray { "McLeod", "YIN" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gateMcLeod", 1 }, "Voicing gate (McLeod)",
                                                      NormalisableRange<float> (0.5f, 0.99f, 0.01f), va::PitchTracker::defaultMcLeodGate));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gateYin", 1 }, "Voicing gate (YIN)",
                                                      NormalisableRange<float> (0.05f, 0.40f, 0.01f), va::PitchTracker::defaultYinGate));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "latencyMs", 1 }, "Voice timing compensation (ms)",
                                                      NormalisableRange<float> (-50.0f, 50.0f, 0.5f), 0.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key",
                                                       StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale",
                                                       StringArray { "Off", "Major", "Minor" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "octave", 1 }, "Reference octave shift", -2, 2, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "refOffset", 1 }, "Reference start in project (s)",
                                                      NormalisableRange<float> (-60.0f, 600.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "perfectCents", 1 }, "Perfect within (cents)",
                                                      NormalisableRange<float> (5.0f, 40.0f, 1.0f), 25.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "goodCents", 1 }, "Good within (cents)",
                                                      NormalisableRange<float> (30.0f, 100.0f, 1.0f), 50.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "a4", 1 }, "A4 reference (Hz)",
                                                      NormalisableRange<float> (430.0f, 450.0f, 0.1f), 440.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                       StringArray { "Play", "Ghost" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "focus", 1 }, "Focus phrase (0 = all)", 0, 99, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "waveGain", 1 }, "Voice wave size",
                                                      NormalisableRange<float> (0.5f, 4.0f, 0.05f), 1.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "windowSec", 1 }, "Seconds on screen",
                                                      NormalisableRange<float> (3.0f, 14.0f, 0.5f), 6.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "graceMs", 1 }, "Note start forgiveness (ms)",
                                                      NormalisableRange<float> (0.0f, 250.0f, 5.0f), 100.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "smoothMs", 1 }, "Vibrato smoothing (ms)",
                                                      NormalisableRange<float> (40.0f, 400.0f, 10.0f), 180.0f));
    return layout;
}

void AssistProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    setLatencySamples (0);              // the audio is never delayed
    currentSampleRate = sampleRate;
    envFifo.reset();
    envCount = 0;
    envPrevPlaying = false;
    envNewPass = false;
    engine.start (sampleRate, samplesPerBlock);
}

bool AssistProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void AssistProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Nothing here writes to the buffer: the audio is a bit-exact pass-through.
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
    captureEnvelope (buffer, hostSample, playing);
}

void AssistProcessor::captureEnvelope (const juce::AudioBuffer<float>& buffer, int64_t hostSample, bool playing) noexcept
{
    if (! playing)
    {
        envPrevPlaying = false;
        envCount = 0;
        return;
    }

    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();
    if (n == 0 || chans == 0)
        return;

    const bool continuous = envPrevPlaying && std::llabs (hostSample - envExpected) <= 4;
    if (! continuous)
    {
        envCount = 0;
        envNewPass = true;
    }
    envPrevPlaying = true;
    envExpected = hostSample + n;

    const float k = 1.0f / (float) chans;
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < chans; ++c)
            s += buffer.getReadPointer (c)[i];
        s *= k;

        if (envCount == 0)
        {
            envMin = envMax = s;
            envStart = hostSample + i;
        }
        else
        {
            envMin = std::min (envMin, s);
            envMax = std::max (envMax, s);
        }

        if (++envCount >= envSliceSamples)
        {
            envCount = 0;
            int a, b, c, d;
            envFifo.prepareToWrite (1, a, b, c, d);
            if (b > 0)
            {
                envFifoStore[(size_t) a] = { envStart, envMin, envMax, envNewPass };
                envFifo.finishedWrite (1);
                envNewPass = false;
            }
        }
    }
}

void AssistProcessor::drainEnvelope (double sampleRate, double latencySec)
{
    while (envFifo.getNumReady() > 0)
    {
        int s1, n1, s2, n2;
        envFifo.prepareToRead (std::min (envFifo.getNumReady(), 512), s1, n1, s2, n2);
        auto handle = [&] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto& e = envFifoStore[(size_t) (start + i)];
                const double t = ((double) e.hostSample + 0.5 * envSliceSamples) / sampleRate + latencySec;
                if (e.newPass)
                    envStore.eraseFrom (t);
                envStore.add ({ t, e.mn, e.mx });
            }
        };
        handle (s1, n1);
        if (n2 > 0) handle (s2, n2);
        envFifo.finishedRead (n1 + n2);
    }
}

juce::String AssistProcessor::loadReferenceMidi (const juce::File& file, int trackIndex)
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
    info.quality = va::Quality::Green;
    reference.setReference (std::move (blocks), info);
    return {};
}

bool AssistProcessor::loadReferenceAudio (const juce::File& file)
{
    analysisError.clear();
    return job.start (file, va::SegmenterRules(), va::PyinSettings());
}

va::MatchSettings AssistProcessor::currentMatchSettings() const
{
    va::MatchSettings s;
    s.greenCents = perfectParam->load();
    s.amberCents = std::max ((double) goodParam->load(), s.greenCents);
    s.a4 = a4Param->load();
    s.octaveShift = (int) std::lround (octaveParam->load());
    s.refOffsetSec = offsetParam->load();
    return s;
}

va::ScoreSettings AssistProcessor::scoreSettings() const
{
    va::ScoreSettings s;
    s.perfectCents = perfectParam->load();
    s.goodCents = std::max ((double) goodParam->load(), s.perfectCents);
    s.graceSec = (double) graceParam->load() * 0.001;
    s.smoothSec = (double) smoothParam->load() * 0.001;
    s.octaveShift = (int) std::lround (octaveParam->load());
    s.refOffsetSec = offsetParam->load();
    const int focus = (int) std::lround (focusParam->load());
    s.focusPhrase = focus <= 0 ? -1 : focus - 1;
    return s;
}

va::ViewSettings AssistProcessor::viewSettings() const
{
    va::ViewSettings v;
    v.refOffsetSec = offsetParam->load();
    v.octaveShift = (int) std::lround (octaveParam->load());
    v.keyRoot = (int) std::lround (keyParam->load());
    v.scale = (int) std::lround (scaleParam->load());
    v.a4 = a4Param->load();
    return v;
}

double AssistProcessor::playheadSeconds() const
{
    const double sr = currentSampleRate.load();
    double t = (double) hostSampleAtBlock.load() / sr;
    if (hostPlaying.load())
        t += juce::jlimit (0.0, 0.25, (juce::Time::getMillisecondCounterHiRes() - hostWallMs.load()) * 0.001);
    return t;
}

std::vector<double> AssistProcessor::phraseSeconds() const
{
    std::vector<double> out;
    for (int p = 0; p < keeper.phraseCount(); ++p)
        out.push_back (keeper.phraseDuration (p));
    return out;
}

void AssistProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The reference changed (loaded, edited, undone): the old results no longer describe it.
    matcherObj.setBlocks (reference.blocksCopy().blocks());
    matcherObj.setSettings (currentMatchSettings());
    keeper.setBlocks (reference.blocksCopy().blocks());
    keeper.setSettings (scoreSettings());
}

void AssistProcessor::commitTake()
{
    if (keeper.takeDirty())
    {
        const juce::ScopedLock sl (takesLock);
        if (takes.add (keeper.snapshot()))
            ++takesRev;
    }
    keeper.clearDirty();
}

void AssistProcessor::updatePerformance()
{
    const auto match = currentMatchSettings();
    if (! (match == matcherObj.settings()))
    {
        if (match.a4 != matcherObj.settings().a4)
            traceStore.clear();          // trace heights were computed at the old A4
        matcherObj.setSettings (match);
    }
    const auto sc = scoreSettings();
    if (! (sc == keeper.settings()))
        keeper.setSettings (sc);

    const double sr = engine.getSampleRate();
    const double latencySec = (double) latencyParam->load() * 0.001;
    const double frameSec = (double) va::PitchTracker::hopSize / sr;
    const double a4 = (double) a4Param->load();

    drainEnvelope (sr, latencySec);

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

            const double t = (double) p.hostSample / sr + latencySec;

            if (p.passId != lastPassId)
            {
                // The notes that ended just before the loop point or the stop are finished: grade them
                // before the new pass wipes the slate, then file the old pass as a take.
                if (lastPassId >= 0)
                {
                    keeper.onPlayhead (lastPointSec + sc.slackSec + 0.06);
                    commitTake();
                }
                lastPassId = p.passId;
                traceStore.eraseFrom (t);
                envStore.eraseFrom (t);
                matcherObj.resetFrom (t);
                keeper.resetFrom (t);
            }

            lastPointSec = t;
            va::TracePoint tp;
            tp.timeSec = t;
            tp.voiced = p.voiced;
            double midi = 0.0;
            if (p.voiced)
            {
                midi = va::midiFromHz ((double) p.hz, a4);
                tp.midi = (float) midi;
                tp.confidence = methodParam->load() < 0.5f ? juce::jlimit (0.0f, 1.0f, p.confidence)
                                                           : juce::jlimit (0.0f, 1.0f, 1.0f - p.confidence);
            }
            traceStore.add (tp);
            matcherObj.onPoint (t, frameSec, p.voiced, p.voiced ? (double) p.hz : 0.0);
            keeper.onPoint (t, frameSec, p.voiced, midi);
        }
    }

    const double playhead = playheadSeconds();
    matcherObj.onPlayhead (playhead);

    if (hostPlaying.load())
    {
        wasPlaying = true;
        notPlayingTicks = 0;
        stopSec = playhead;
        keeper.onPlayhead (playhead - std::max (0.0, latencySec));
    }
    else if (wasPlaying)
    {
        // Stopped: give the last frames time to arrive, grade what the singer got through, file the take.
        if (++notPlayingTicks >= 7)
        {
            keeper.onPlayhead (lastPointSec + sc.slackSec + 0.06);
            commitTake();
            wasPlaying = false;
        }
    }
}

void AssistProcessor::pollAnalysis()
{
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

juce::AudioProcessorEditor* AssistProcessor::createEditor()
{
    return new AssistEditor (*this);
}

void AssistProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("version", stateVersion, nullptr);
    state.removeChild (state.getChildWithName ("Reference"), nullptr);
    state.addChild (reference.toValueTree(), -1, nullptr);
    state.removeChild (state.getChildWithName ("Takes"), nullptr);
    {
        juce::ValueTree t ("Takes");
        {
            const juce::ScopedLock sl (takesLock);
            t.setProperty ("text", juce::String (takes.toText()), nullptr);
        }
        state.addChild (t, -1, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AssistProcessor::setStateInformation (const void* data, int sizeInBytes)
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
                reference.fromValueTree (ref);
            }
            auto saved = tree.getChildWithName ("Takes");
            if (saved.isValid())
            {
                tree.removeChild (saved, nullptr);
                va::TakeStore loaded;
                if (loaded.fromText (saved.getProperty ("text").toString().toStdString()))
                {
                    const juce::ScopedLock sl (takesLock);
                    takes = std::move (loaded);
                }
                ++takesRev;
            }
            apvts.replaceState (tree);
            matcherObj.setBlocks (reference.blocksCopy().blocks());
            matcherObj.setSettings (currentMatchSettings());
            keeper.setBlocks (reference.blocksCopy().blocks());
            keeper.setSettings (scoreSettings());
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AssistProcessor();
}
