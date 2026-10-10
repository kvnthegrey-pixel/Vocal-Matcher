#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PitchEngine.h"
#include "ReferenceModel.h"
#include "ReferenceJob.h"
#include "PerformanceSource.h"

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
    ~VocalAssistorProcessor() override { stopTimer(); reference.removeChangeListener (this); job.cancel(); }

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

    static constexpr int stateVersion = 3;

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
