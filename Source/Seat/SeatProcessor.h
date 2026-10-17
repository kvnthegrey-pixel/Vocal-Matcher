#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SeatEngine.h"

/** Runs SeatEngine's file analyses on their own thread, so a long beat never freezes the window. */
class SeatJob final : private juce::Thread
{
public:
    enum class Kind { Beat, Vocal };
    struct Result
    {
        bool ok = false, cancelled = false;
        juce::String error;
        Kind kind = Kind::Beat;
        ms::BeatProfile beat;
        ms::VocalProfile vocal;
    };

    SeatJob() : juce::Thread ("Mix Seat analysis") {}
    ~SeatJob() override { cancel(); }

    bool start (Kind k, const juce::File& f)
    {
        if (isThreadRunning()) return false;
        kind = k; file = f;
        progress = 0.0f; cancelFlag = false; finished = false; delivered = true;
        startThread (juce::Thread::Priority::low);
        return true;
    }
    void cancel() { cancelFlag = true; stopThread (5000); }
    bool isRunning() const { return const_cast<SeatJob*> (this)->isThreadRunning(); }
    float getProgress() const { return progress.load(); }
    juce::String getFileName() const { return file.getFileName(); }
    Kind getKind() const { return kind; }

    /** True once, after the thread has finished. */
    bool takeResult (Result& out)
    {
        if (! finished.load() || delivered) return false;
        out = std::move (result);
        delivered = true;
        return true;
    }

private:
    void run() override
    {
        Result r;
        r.kind = kind;
        auto prog = [this] (float p) { progress = p; return ! threadShouldExit() && ! cancelFlag.load(); };
        r.ok = kind == Kind::Beat ? ms::analyseBeatFile (file, r.beat, r.error, prog)
                                  : ms::analyseVocalFile (file, r.vocal, r.error, prog);
        r.cancelled = ! r.ok && (cancelFlag.load() || threadShouldExit());
        result = std::move (r);
        delivered = false;
        finished = true;
    }

    juce::File file;
    Kind kind = Kind::Beat;
    Result result;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> cancelFlag { false }, finished { false };
    bool delivered = true;
};

/**
    Mix Seat: put it on the vocal track, upload the beat, and it nudges the vocal's EQ so the
    voice sits in the gaps the beat leaves.

    The beat never has to run through the plugin: it is analysed from the file you upload. The
    vocal is measured from what plays through the plugin (or from a vocal file you load).
    The only thing the plugin does to the audio is a gentle 12-band EQ (zero latency).
*/
class SeatProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    SeatProcessor();
    ~SeatProcessor() override { stopTimer(); job.cancel(); }

    const juce::String getName() const override { return JucePlugin_Name; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- message thread ------------------------------------------------------
    bool loadBeat (const juce::File& f)        { return job.start (SeatJob::Kind::Beat, f); }
    bool loadVocalFile (const juce::File& f)   { return job.start (SeatJob::Kind::Vocal, f); }
    void cancelAnalysis()                      { job.cancel(); }
    bool isAnalysing() const                   { return job.isRunning(); }
    float analysisProgress() const             { return job.getProgress(); }
    juce::String analysisName() const          { return job.getFileName(); }
    bool analysingBeat() const                 { return job.getKind() == SeatJob::Kind::Beat; }
    juce::String takeError()                   { auto e = lastError; lastError.clear(); return e; }
    void clearBeat();
    void resetVocal();

    const ms::BeatProfile&  beatProfile() const  { return beat; }
    const ms::VocalProfile& vocalProfile() const { return vocal; }
    const ms::SeatPlan&     plan() const         { return seatPlan; }
    const ms::Gains&        appliedGains() const { return applied; }
    ms::SeatSettings        settings() const;
    bool   followOn() const                      { return followParam->load() >= 0.5f; }
    bool   bypassed() const                      { return bypassParam->load() >= 0.5f; }
    double songSeconds() const                   { return hostSec.load() - (double) offsetParam->load(); }
    double sampleRate() const                    { return currentSampleRate.load(); }
    int    revision() const                      { return rev; }
    /** One timer tick's work now (tests). */
    void pumpSeat()                              { tick(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override { tick(); }
    void tick();
    void drainVocal();
    void adoptResult (const SeatJob::Result&);
    void refreshStateText();
    void pushVocal (const juce::AudioBuffer<float>&) noexcept;

    // audio thread
    va::EqFilterBank bank;
    float trimPrev = 1.0f;
    std::atomic<double> hostSec { 0.0 };
    std::atomic<double> currentSampleRate { 48000.0 };

    // audio thread -> message thread: mono copy of the incoming vocal
    static constexpr int vocalFifoSize = 1 << 17;
    juce::AbstractFifo vocalFifo { vocalFifoSize };
    std::vector<float> vocalFifoStore = std::vector<float> ((size_t) vocalFifoSize);
    std::vector<float> drainBuf = std::vector<float> (16384);

    // message thread
    ms::BeatProfile beat;
    ms::VocalProfile vocal;
    ms::VocalLearner learner;
    ms::SeatPlan seatPlan;
    ms::Gains applied {};
    ms::SeatSettings lastSettings;
    bool lastFollow = false, planDirty = true;
    double planRate = 0.0;
    int rev = 0;
    double learnerRate = 0.0;
    std::atomic<bool> rateChanged { false };
    juce::String lastError;
    SeatJob job;

    // project state, written by the message thread and read by whichever thread saves
    juce::CriticalSection stateLock;
    std::string beatText, vocalText;
    bool havePending = false;
    ms::BeatProfile pendingBeat;
    ms::VocalProfile pendingVocal;
    bool pendingHasBeat = false, pendingHasVocal = false;

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* maxDbParam = nullptr;
    std::atomic<float>* followParam = nullptr;
    std::atomic<float>* offsetParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* trimParam = nullptr;
    std::atomic<float>* freezeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeatProcessor)
};
