#pragma once

#include "PluginProcessor.h"

/** The Tone EQ tab: the reference tone, your voice and what the EQ is doing,
    plus the controls to measure them. Message thread only. */
class EqView final : public juce::Component, private juce::Timer
{
public:
    explicit EqView (VocalAssistorProcessor&);
    ~EqView() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    /** Called with a message for the user (e.g. an error). */
    std::function<void (const juce::String&, const juce::String&)> onMessage;

private:
    void timerCallback() override;
    void chooseTone();

    VocalAssistorProcessor& proc;
    juce::TextButton loadButton { "Load tone reference..." }, learnButton { "Learn my voice" }, clearButton { "Clear my voice" };
    juce::ToggleButton onButton { "Tone EQ on" };
    juce::Slider amount;
    juce::Label amountLabel;
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttach;
    juce::AudioProcessorValueTreeState::SliderAttachment amountAttach;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> plotArea, noteArea;
    int lastRev = -1;
    bool lastLearning = false;
};
