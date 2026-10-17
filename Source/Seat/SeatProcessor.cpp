#include "SeatProcessor.h"
#include "SeatEditor.h"

SeatProcessor::SeatProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MixSeatState", createLayout())
{
    amountParam = apvts.getRawParameterValue ("amount");
    maxDbParam  = apvts.getRawParameterValue ("maxDb");
    followParam = apvts.getRawParameterValue ("follow");
    offsetParam = apvts.getRawParameterValue ("beatOffset");
    bypassParam = apvts.getRawParameterValue ("bypass");
    trimParam   = apvts.getRawParameterValue ("trim");
    freezeParam = apvts.getRawParameterValue ("freeze");
    lastSettings = settings();
    lastFollow = followOn();
    startTimerHz (30);
}

juce::AudioProcessorValueTreeState::ParameterLayout SeatProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Amount",
                                                      NormalisableRange<float> (0.0f, 100.0f, 1.0f), 70.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "maxDb", 1 }, "Max change (dB)",
                                                      NormalisableRange<float> (1.0f, 8.0f, 0.1f), 4.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "follow", 1 }, "Follow the beat through the song", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "beatOffset", 1 }, "Beat starts at (s)",
                                                      NormalisableRange<float> (-60.0f, 600.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass the EQ", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "trim", 1 }, "Output trim (dB)",
                                                      NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Stop learning the vocal", false));
    return layout;
}

ms::SeatSettings SeatProcessor::settings() const
{
    ms::SeatSettings s;
    s.amount = amountParam->load() * 0.01f;
    s.maxDb = maxDbParam->load();
    return s;
}

void SeatProcessor::prepareToPlay (double sampleRate, int)
{
    setLatencySamples (0);                       // the audio is never delayed
    currentSampleRate = sampleRate;
    bank.prepare (sampleRate, 2);
    vocalFifo.reset();
    trimPrev = 1.0f;
    rateChanged = true;                          // the message thread rebuilds what depends on the rate
}

bool SeatProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SeatProcessor::pushVocal (const juce::AudioBuffer<float>& buffer) noexcept
{
    const int n = buffer.getNumSamples(), chans = buffer.getNumChannels();
    if (n <= 0 || chans <= 0) return;
    int s1, n1, s2, n2;
    vocalFifo.prepareToWrite (n, s1, n1, s2, n2);
    if (n1 + n2 < n)
        return;                                  // the message thread is busy: skip this block, never wait
    const float scale = 1.0f / (float) chans;
    auto mix = [&] (int dst, int count, int srcStart)
    {
        for (int i = 0; i < count; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < chans; ++c) s += buffer.getReadPointer (c)[srcStart + i];
            vocalFifoStore[(size_t) (dst + i)] = s * scale;
        }
    };
    mix (s1, n1, 0);
    if (n2 > 0) mix (s2, n2, n1);
    vocalFifo.finishedWrite (n1 + n2);
}

void SeatProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getTimeInSeconds())            hostSec = *t;
            else if (auto s = pos->getTimeInSamples())       hostSec = (double) *s / currentSampleRate.load();
        }

    if (freezeParam->load() < 0.5f)
        pushVocal (buffer);                                  // measure the input, before the EQ, so the result never chases itself

    bank.process (buffer);                                   // untouched when every filter is at 0 dB

    const float target = juce::Decibels::decibelsToGain (trimParam->load());
    if (std::abs (target - 1.0f) > 1.0e-4f || std::abs (trimPrev - 1.0f) > 1.0e-4f)
    {
        const int n = buffer.getNumSamples();
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            buffer.applyGainRamp (c, 0, n, trimPrev, target);
        trimPrev = target;
    }
}

void SeatProcessor::drainVocal()
{
    for (;;)
    {
        const int ready = vocalFifo.getNumReady();
        if (ready <= 0) return;
        const int n = std::min (ready, (int) drainBuf.size());
        int s1, n1, s2, n2;
        vocalFifo.prepareToRead (n, s1, n1, s2, n2);
        std::copy (vocalFifoStore.begin() + s1, vocalFifoStore.begin() + s1 + n1, drainBuf.begin());
        if (n2 > 0) std::copy (vocalFifoStore.begin() + s2, vocalFifoStore.begin() + s2 + n2, drainBuf.begin() + n1);
        vocalFifo.finishedRead (n1 + n2);

        if (learner.add (drainBuf.data(), n1 + n2))
        {
            const auto& p = learner.profile();
            float biggest = 99.0f;
            if (vocal.valid && p.valid)
            {
                biggest = 0.0f;
                for (size_t b = 0; b < (size_t) ms::numBands; ++b)
                    biggest = std::max (biggest, std::abs (p.average[b] - vocal.average[b]));
            }
            if (biggest > 0.3f || vocal.valid != p.valid)
            {
                vocal = p;
                planDirty = true;
                ++rev;
                refreshStateText();
            }
            else
            {
                vocal.seconds = p.seconds;
            }
        }
    }
}

void SeatProcessor::clearBeat()
{
    beat = {};
    seatPlan = {};
    planDirty = true;
    ++rev;
    refreshStateText();
}

void SeatProcessor::resetVocal()
{
    learner.clear();
    vocal = {};
    seatPlan = {};
    planDirty = true;
    ++rev;
    refreshStateText();
}

void SeatProcessor::adoptResult (const SeatJob::Result& r)
{
    if (r.cancelled) return;
    if (! r.ok)
    {
        lastError = r.error;
        return;
    }
    if (r.kind == SeatJob::Kind::Beat)
        beat = r.beat;
    else
    {
        vocal = r.vocal;
        learner.set (vocal);
    }
    planDirty = true;
    ++rev;
    refreshStateText();
}

void SeatProcessor::refreshStateText()
{
    const juce::ScopedLock sl (stateLock);
    beatText = beat.valid ? beat.toText() : std::string();
    vocalText = vocal.valid ? vocal.toText() : std::string();
}

void SeatProcessor::tick()
{
    const double sr = currentSampleRate.load();
    if (rateChanged.exchange (false) || std::abs (learnerRate - sr) > 0.5)
    {
        learner.reset (sr);
        learnerRate = sr;
        planDirty = true;
    }

    {   // a project that was just opened
        bool take = false;
        ms::BeatProfile b; ms::VocalProfile v; bool hb = false, hv = false;
        {
            const juce::ScopedLock sl (stateLock);
            if (havePending)
            {
                take = true; b = pendingBeat; v = pendingVocal; hb = pendingHasBeat; hv = pendingHasVocal;
                havePending = false;
            }
        }
        if (take)
        {
            beat = hb ? b : ms::BeatProfile();
            vocal = hv ? v : ms::VocalProfile();
            learner.set (vocal);
            planDirty = true;
            ++rev;
            refreshStateText();
        }
    }

    if (! freezeParam->load())
        drainVocal();
    else
        vocalFifo.reset();

    SeatJob::Result r;
    if (job.takeResult (r))
        adoptResult (r);

    const auto s = settings();
    const bool follow = followOn();
    if (s.amount != lastSettings.amount || s.maxDb != lastSettings.maxDb || follow != lastFollow || sr != planRate)
    {
        lastSettings = s;
        lastFollow = follow;
        planRate = sr;
        planDirty = true;
    }

    if (planDirty)
    {
        planDirty = false;
        seatPlan = ms::buildPlan (beat, vocal, s, sr, follow);
        ++rev;
    }

    ms::Gains g {};
    if (! bypassed() && seatPlan.valid)
        g = seatPlan.gainsAt (songSeconds(), follow);
    bank.setTargets (g);
    applied = g;
}

juce::AudioProcessorEditor* SeatProcessor::createEditor() { return new SeatEditor (*this); }

void SeatProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("version", 1, nullptr);
    state.removeChild (state.getChildWithName ("Beat"), nullptr);
    state.removeChild (state.getChildWithName ("Vocal"), nullptr);
    {
        const juce::ScopedLock sl (stateLock);
        if (! beatText.empty())
        {
            juce::ValueTree t ("Beat");
            t.setProperty ("text", juce::String (beatText), nullptr);
            state.addChild (t, -1, nullptr);
        }
        if (! vocalText.empty())
        {
            juce::ValueTree t ("Vocal");
            t.setProperty ("text", juce::String (vocalText), nullptr);
            state.addChild (t, -1, nullptr);
        }
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void SeatProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);

    ms::BeatProfile b; ms::VocalProfile v;
    bool hb = false, hv = false;
    if (auto c = tree.getChildWithName ("Beat"); c.isValid())
    {
        hb = b.fromText (c.getProperty ("text").toString().toStdString());
        tree.removeChild (c, nullptr);
    }
    if (auto c = tree.getChildWithName ("Vocal"); c.isValid())
    {
        hv = v.fromText (c.getProperty ("text").toString().toStdString());
        tree.removeChild (c, nullptr);
    }
    apvts.replaceState (tree);
    {
        const juce::ScopedLock sl (stateLock);
        pendingBeat = b; pendingVocal = v;
        pendingHasBeat = hb; pendingHasVocal = hv;
        havePending = true;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SeatProcessor(); }
