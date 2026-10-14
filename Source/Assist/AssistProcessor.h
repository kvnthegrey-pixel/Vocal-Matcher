#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../PitchEngine.h"
#include "../ReferenceModel.h"
#include "../ReferenceJob.h"
#include "../PerformanceSource.h"
#include "ScoreKeeper.h"
#include "TakeStore.h"
#include "EnvelopeStore.h"

/**
    Performance Assist: a coaching plugin for singing against a reference vocal.

    You put it on the vocal track, load the Suno reference, and sing. Your voice
    is drawn as a wave that flows into the note blocks; each note is graded
    Perfect / Good / Miss, there is a score and a streak, and every pass over the
    song is kept as a take so you can see where you drift and which phrases to comp.

    The audio path is an exact pass-through: processBlock never writes to the
    buffer and the plugin reports zero latency. Everything else happens on other
    threads or on the message thread.
*/
class AssistProcessor final : public juce::AudioProcessor,
                              public va::PerformanceSource,
                              private juce::Timer,
                              private juce::ChangeListener
{
public:
    AssistProcessor();
    ~AssistProcessor() override { stopTimer(); reference.removeChangeListener (this); job.cancel(); }

    const juce::String getName() const override { return JucePlugin_Name; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { engine.stop(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

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

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static constexpr int stateVersion = 1;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- reference vocal (message thread) --------------------------------
    juce::String loadReferenceMidi (const juce::File&, int trackIndex);
    bool loadReferenceAudio (const juce::File&);
    void cancelAnalysis()                    { job.cancel(); }
    bool isAnalysing() const                 { return job.isRunning(); }
    float getAnalysisProgress() const        { return job.getProgress(); }
    juce::String getAnalysisFileName() const { return job.getFileName(); }
    juce::String takeAnalysisError()         { auto e = analysisError; analysisError.clear(); return e; }
    juce::String peekAnalysisError() const   { return analysisError; }

    va::ReferenceModel reference;

    // ---- va::PerformanceSource ----------------------------------------------
    double playheadSeconds() const override;
    bool   isPlaying() const override                 { return hostPlaying.load(); }
    const va::Matcher& matcher() const override       { return matcherObj; }
    const va::TraceStore& trace() const override      { return traceStore; }
    va::ViewSettings viewSettings() const override;

    // ---- what the performance view reads (message thread) ---------------------------
    const va::ScoreKeeper&    score() const     { return keeper; }
    const va::EnvelopeStore&  envelope() const  { return envStore; }
    const va::TakeStore&      takeStore() const { return takes; }
    int  takesRevision() const                  { return takesRev; }
    void clearTakes()                           { const juce::ScopedLock sl (takesLock); takes.clear(); ++takesRev; }
    bool getLatestPoint (va::PitchPoint& p) const { p = latestPoint; return haveLatestPoint; }
    va::ScoreSettings scoreSettings() const;
    bool   ghostMode() const                    { return modeParam->load() >= 0.5f; }
    double waveGain() const                     { return (double) gainParam->load(); }
    double windowSeconds() const                { return (double) windowParam->load(); }
    /** The blocks as the score sees them, for phrase lengths and the heatmap. */
    std::vector<double> phraseSeconds() const;
    /** Runs one timer tick's work now (tests). */
    void pumpPerformance()                      { updatePerformance(); }

    juce::AudioProcessorValueTreeState apvts;
    va::PitchEngine engine;

private:
    void timerCallback() override       { updatePerformance(); pollAnalysis(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updatePerformance();
    void pollAnalysis();
    void commitTake();
    void drainEnvelope (double sampleRate, double latencySec);
    void captureEnvelope (const juce::AudioBuffer<float>&, int64_t hostSample, bool playing) noexcept;
    va::MatchSettings currentMatchSettings() const;

    va::Matcher matcherObj;
    va::TraceStore traceStore;
    va::EnvelopeStore envStore;
    va::ScoreKeeper keeper;
    va::TakeStore takes;
    mutable juce::CriticalSection takesLock;      // the host may save the project from another thread
    int takesRev = 0;

    // audio thread -> message thread: voice envelope
    static constexpr int envSliceSamples = 128;
    static constexpr int envFifoSize = 8192;
    juce::AbstractFifo envFifo { envFifoSize };
    std::vector<va::EnvSlice> envFifoStore = std::vector<va::EnvSlice> ((size_t) envFifoSize);
    float envMin = 0.0f, envMax = 0.0f;
    int envCount = 0;
    int64_t envStart = 0, envExpected = 0;
    bool envPrevPlaying = false, envNewPass = false;

    va::PitchPoint latestPoint;
    bool haveLatestPoint = false;
    int lastPassId = -1;
    double lastPointSec = 0.0;
    bool wasPlaying = false;
    int notPlayingTicks = 0;
    double stopSec = 0.0;

    std::atomic<int64_t> hostSampleAtBlock { 0 };
    std::atomic<double> hostWallMs { 0.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<double> currentSampleRate { 48000.0 };

    std::atomic<float>* methodParam = nullptr;
    std::atomic<float>* mcleodGateParam = nullptr;
    std::atomic<float>* yinGateParam = nullptr;
    std::atomic<float>* latencyParam = nullptr;
    std::atomic<float>* keyParam = nullptr;
    std::atomic<float>* scaleParam = nullptr;
    std::atomic<float>* octaveParam = nullptr;
    std::atomic<float>* offsetParam = nullptr;
    std::atomic<float>* perfectParam = nullptr;
    std::atomic<float>* goodParam = nullptr;
    std::atomic<float>* a4Param = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* focusParam = nullptr;
    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* windowParam = nullptr;
    std::atomic<float>* graceParam = nullptr;
    std::atomic<float>* smoothParam = nullptr;

    va::ReferenceJob job;
    juce::String analysisError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssistProcessor)
};
