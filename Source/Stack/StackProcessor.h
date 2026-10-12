#pragma once

#include "StackParams.h"

/** Vocal Stack: stacked, hard, upfront vocal texture. Zero latency, mono or stereo in. */
class VocalStackProcessor final : public juce::AudioProcessor
{
public:
    VocalStackProcessor();

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
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    void applyPreset (int index);

    juce::AudioProcessorValueTreeState apvts;
    vs::StackEngine engine;

private:
    juce::AudioBuffer<float> work;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalStackProcessor)
};
