#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Vocal Assistor, Phase 1 skeleton.

    The audio path is an exact pass-through: processBlock never touches the
    samples. Plugin latency is reported as 0 samples because the audio itself
    is never delayed (the pitch window only delays the on-screen trace).
*/
class VocalAssistorProcessor final : public juce::AudioProcessor
{
public:
    VocalAssistorProcessor();
    ~VocalAssistorProcessor() override = default;

    const juce::String getName() const override { return JucePlugin_Name; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

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

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalAssistorProcessor)
};
