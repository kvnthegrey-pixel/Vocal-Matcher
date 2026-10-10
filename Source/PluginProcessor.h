#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PitchEngine.h"
#include "ReferenceModel.h"
#include "ReferenceJob.h"

/**
    Vocal Assistor, Phase 3: pass-through, pitch analysis and the reference blocks.

    The audio path is an exact pass-through: processBlock never touches the
    samples. Plugin latency is reported as 0 samples because the audio itself
    is never delayed (the pitch window only delays the on-screen trace).
    Pitch analysis only reads the buffer, on a separate thread.
*/
class VocalAssistorProcessor final : public juce::AudioProcessor,
                                     private juce::Timer
{
public:
    VocalAssistorProcessor();
    ~VocalAssistorProcessor() override { stopTimer(); job.cancel(); }

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

    juce::AudioProcessorValueTreeState apvts;
    va::PitchEngine engine;

private:
    void timerCallback() override;

    va::ReferenceJob job;
    juce::String analysisError;
    std::atomic<float>* methodParam = nullptr;
    std::atomic<float>* mcleodGateParam = nullptr;
    std::atomic<float>* yinGateParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalAssistorProcessor)
};
