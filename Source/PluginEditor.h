#pragma once

#include "PluginProcessor.h"
#include "BlockEditor.h"

/** Phase 4/5 window: load a reference vocal, edit its note blocks and lock them,
    plus the scrolling piano roll with the live trace, the big ON/OFF readout
    and the settings. */
class VocalAssistorEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer,
                                  private juce::ChangeListener
{
public:
    explicit VocalAssistorEditor (VocalAssistorProcessor&);
    ~VocalAssistorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void chooseReference();
    void startLoading (const juce::File&);
    void importMidi (const juce::File&);
    void exportMidi();
    void updateButtons();
    void showMessage (const juce::String& title, const juce::String& text);

    VocalAssistorProcessor& proc;

    va::BlockEditor blockEditor;
    juce::ToggleButton followButton { "Follow playhead" };
    juce::TextButton loadButton { "Load reference vocal..." }, bigLoadButton { "Load reference vocal..." },
        exportButton { "Export MIDI..." }, undoButton { "Undo" }, lockButton { "Lock reference" },
        splitButton { "Split at cursor" }, mergeButton { "Merge" }, deleteButton { "Delete" },
        downButton { "Pitch -1" }, upButton { "Pitch +1" }, fitButton { "Fit all" },
        cancelButton { "Cancel" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };

    juce::ComboBox methodBox, keyBox, scaleBox;
    juce::Slider gateMcLeod, gateYin, latency, octave, offset, greenCents, amberCents, a4;
    juce::Label methodLabel, gateMcLeodLabel, gateYinLabel, latencyLabel, keyLabel, scaleLabel,
        octaveLabel, offsetLabel, greenLabel, amberLabel, a4Label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> methodAttach, keyAttach, scaleAttach;
    juce::AudioProcessorValueTreeState::SliderAttachment mcAttach, yinAttach, latAttach, octAttach, offAttach,
        greenAttach, amberAttach, a4Attach;

    std::unique_ptr<juce::FileChooser> chooser;

    va::PitchPoint latest;
    bool haveLatest = false;
    juce::Rectangle<int> bigArea, readoutArea, statusArea, qualityArea, promptArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalAssistorEditor)
};
