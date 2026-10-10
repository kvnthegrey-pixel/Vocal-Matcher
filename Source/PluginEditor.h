#pragma once

#include "PluginProcessor.h"

/** Phase 2 window: live pitch readout and the pitch settings. The piano roll
    arrives in Phase 5. */
class VocalAssistorEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit VocalAssistorEditor (VocalAssistorProcessor&);
    ~VocalAssistorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    VocalAssistorProcessor& proc;

    juce::ComboBox methodBox;
    juce::Slider gateMcLeod, gateYin, latency;
    juce::Label methodLabel, gateMcLeodLabel, gateYinLabel, latencyLabel;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment methodAttach;
    juce::AudioProcessorValueTreeState::SliderAttachment mcAttach, yinAttach, latAttach;

    va::PitchPoint latest;
    bool haveLatest = false;
    juce::Rectangle<int> readoutArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalAssistorEditor)
};
