#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PitchEngine.h"
#include "ReferenceModel.h"
#include "ReferenceJob.h"
#include "PerformanceSource.h"
#include "ToneSequence.h"
#include "EqMatch.h"
#include <mutex>
#include <thread>

/**
    Vocal Assistor, Phase 3: pass-through, pitch analysis and the reference blocks.

    The audio path is an exact pass-through: processBlock never touches the
    samples. Plugin latency is reported as 0 samples because the audio itself
    is never delayed (the pitch window only delays the on-screen trace).
    Pitch analysis only reads the buffer, on a separate thread.
*/
class VocalAssistorProcessor final : public juce::AudioProcessor,
                                     public va::PerformanceSource,
                                     private juce::Timer,
                                     private juce::ChangeListener
{
public:
    VocalAssistorProcessor();
    ~VocalAssistorProcessor() override { stopTimer(); reference.removeChangeListener (this); job.cancel(); joinToneThread(); }

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

    static constexpr int stateVersion = 4;

    // ---- reference vocal (message thread) --------------------------------
    /** Imports one track of a MIDI file as the reference. Returns an error message or empty. */
    juce::String loadReferenceMidi (const juce::File&, int trackIndex);
    /** Starts the background pYIN analysis of an audio file. Returns false if one is already running. */
    bool loadReferenceAudio (const juce::File&);
    void cancelAnalysis()                { job.cancel(); }
    bool isAnalysing() const             { return job.isRunning(); }
    float getAnalysisProgress() const    { return job.getProgress(); }
    juce::String getAnalysisFileName() const { return job.getFileName(); }
    juce::String takeAnalysisError()     { auto e = analysisError; analysisError.clear(); return e; }
    juce::String peekAnalysisError() const { return analysisError; }

    va::ReferenceModel reference;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- va::PerformanceSource ----------------------------------------------
    double playheadSeconds() const override;
    bool   isPlaying() const override                 { return hostPlaying.load(); }
    const va::Matcher& matcher() const override       { return matcherObj; }
    const va::TraceStore& trace() const override      { return traceStore; }
    va::ViewSettings viewSettings() const override;
    /** Most recent analysis frame, for the small live readout (message thread). */
    bool getLatestPoint (va::PitchPoint& p) const { p = latestPoint; return haveLatestPoint; }
    /** Asks the audio thread to play reference-then-you through this track's output.
        Only while a comparison is sounding is anything added to the audio; otherwise the pass-through is exact. */
    void requestComparison (double refHz, double sungHz, double lengthSec)
    {
        reqRef = refHz; reqSung = sungHz; reqLen = lengthSec;
        reqCounter.fetch_add (1);
    }
    // ---- tone matching EQ (message thread) -------------------------------
    /** Measures the tone of an audio file in the background and uses it as the target sound. */
    bool loadToneReference (const juce::File&);
    bool isToneBusy() const                 { return toneBusy.load(); }
    bool hasToneReference() const           { return haveRef; }
    bool hasMyVoice() const                 { return haveMine; }
    juce::String getToneName() const        { return toneName; }
    juce::String takeToneError()            { auto e = toneErrorMsg; toneErrorMsg.clear(); return e; }
    void setLearning (bool on)              { learning = on; }
    bool isLearning() const                 { return learning.load(); }
    void clearMyVoice();
    double myVoiceSeconds() const           { return mineSeconds; }
    const va::BandDb& refBands() const      { return refDb; }
    const va::BandDb& myBands() const       { return mineDb; }
    const va::BandDb& eqCurve() const       { return corrCurve; }       // what the EQ is being asked to do, at 100%
    int eqRevision() const                  { return eqRev; }
    /** Runs one timer tick's work now (tests). */
    void pumpPerformance()                            { updatePerformance(); }

    juce::AudioProcessorValueTreeState apvts;
    va::PitchEngine engine;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updatePerformance();
    va::MatchSettings currentMatchSettings() const;

    va::Matcher matcherObj;
    va::TraceStore traceStore;
    void joinToneThread();
    void updateEq();
    void applyEqTargets();

    va::EqFilterBank eqBank;
    std::atomic<bool> learning { false };
    juce::AbstractFifo learnFifo { 1 << 16 };
    std::vector<float> learnStore = std::vector<float> (1 << 16, 0.0f), learnScratch;
    std::unique_ptr<va::BandSpectrum> mineSpec;
    double mineSpecRate = 0.0, mineSeconds = 0.0;
    va::BandDb refDb {}, mineDb {}, corrCurve {};
    std::array<float, (size_t) va::numEqBands> solvedGains {};
    bool haveRef = false, haveMine = false;
    juce::String toneName, toneErrorMsg;
    int eqRev = 0, tickCount = 0;
    float lastEqAmount = -1.0f, lastEqOn = -1.0f;
    std::atomic<float>* eqOnParam = nullptr;
    std::atomic<float>* eqAmountParam = nullptr;
    std::thread toneThread;
    std::atomic<bool> toneBusy { false }, toneCancel { false }, toneDone { false };
    std::mutex toneMutex;
    va::BandDb toneResult {};
    bool toneOk = false;
    juce::String toneErr, toneFile;

    std::atomic<double> reqRef { 0.0 }, reqSung { 0.0 }, reqLen { 0.5 };
    std::atomic<int> reqCounter { 0 };
    int seenCounter = 0;
    va::ToneSequence tone;
    std::vector<float> toneBuf;
    va::PitchPoint latestPoint;
    bool haveLatestPoint = false;
    int lastPassId = -1;
    bool lastPointWasPlaying = false;

    std::atomic<int64_t> hostSampleAtBlock { 0 };
    std::atomic<double> hostWallMs { 0.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<double> currentSampleRate { 48000.0 };

    std::atomic<float>* keyParam = nullptr;
    std::atomic<float>* scaleParam = nullptr;
    std::atomic<float>* octaveParam = nullptr;
    std::atomic<float>* offsetParam = nullptr;
    std::atomic<float>* greenParam = nullptr;
    std::atomic<float>* amberParam = nullptr;
    std::atomic<float>* a4Param = nullptr;
    std::atomic<float>* latencyParam = nullptr;

    va::ReferenceJob job;
    juce::String analysisError;
    std::atomic<float>* methodParam = nullptr;
    std::atomic<float>* mcleodGateParam = nullptr;
    std::atomic<float>* yinGateParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalAssistorProcessor)
};
