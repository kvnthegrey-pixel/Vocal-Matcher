#pragma once

#include "ChainParams.h"

/** Vocal Chain: auto-tune, compressor with parallel punch, de-esser, colour,
    thickener and a sweet plate reverb in one insert. Reports a fixed latency
    of 2048 samples (the pitch corrector's look-ahead); the host compensates. */
class VocalChainProcessor final : public juce::AudioProcessor
{
public:
    VocalChainProcessor();

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
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void applyPreset (int index);

    juce::AudioProcessorValueTreeState apvts;
    vc::ChainEngine engine;

private:
    juce::AudioBuffer<float> work;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChainProcessor)
};
