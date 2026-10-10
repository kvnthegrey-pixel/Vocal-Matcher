#pragma once

#include "PluginProcessor.h"

/** Phase 1 placeholder window. The piano roll arrives in Phase 5. */
class VocalAssistorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit VocalAssistorEditor (VocalAssistorProcessor&);
    ~VocalAssistorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalAssistorEditor)
};
