#pragma once

#include "AssistProcessor.h"
#include "GameView.h"
#include "TakesView.h"
#include "../BlockEditor.h"

/** The Performance Assist window: Perform (the game), Takes (history, heatmap, comp plan),
    Edit reference (fix the Suno note blocks) and Settings. */
class AssistEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private juce::ChangeListener
{
public:
    explicit AssistEditor (AssistProcessor&);
    ~AssistEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    enum class Tab { Perform, Takes, Edit, Settings };
    void showTab (Tab);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void chooseReference();
    void startLoading (const juce::File&);
    void importMidi (const juce::File&);
    void showMessage (const juce::String& title, const juce::String& text);
    void updateButtons();
    void syncFocusBox();
    void updateCycleLabel();

    AssistProcessor& proc;
    Tab tab = Tab::Perform;

    pa::GameView gameView;
    pa::TakesView takesView;
    va::BlockEditor blockEditor;

    juce::TextButton tabPerform { "Perform" }, tabTakes { "Takes" }, tabEdit { "Edit reference" }, tabSettings { "Settings" };
    juce::TextButton loadButton { "Load reference vocal..." }, bigLoadButton { "Load reference vocal..." },
        cancelButton { "Cancel" }, copyCycleButton { "Copy cycle times" };
    juce::ComboBox modeBox, focusBox;
    juce::Label modeLabel, focusLabel, cycleLabel;
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };

    // Edit tab
    juce::TextButton undoButton { "Undo" }, lockButton { "Lock reference" }, splitButton { "Split at cursor" },
        mergeButton { "Merge" }, deleteButton { "Delete" }, downButton { "Pitch -1" }, upButton { "Pitch +1" },
        fitButton { "Fit all" };
    juce::ToggleButton followButton { "Follow playhead" };

    // Settings tab
    juce::ComboBox keyBox, scaleBox, methodBox;
    juce::Slider octave, offset, latency, perfectCents, goodCents, graceMs, smoothMs, waveGain, windowSec, a4, gateMcLeod, gateYin;
    juce::Label keyLabel, scaleLabel, methodLabel, octaveLabel, offsetLabel, latencyLabel, perfectLabel, goodLabel,
        graceLabel, smoothLabel, gainLabel, windowLabel, a4Label, gateMcLeodLabel, gateYinLabel, settingsNote;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttach, keyAttach, scaleAttach, methodAttach;
    juce::AudioProcessorValueTreeState::SliderAttachment octAttach, offAttach, latAttach, perfAttach, goodAttach,
        graceAttach, smoothAttach, gainAttach, windowAttach, a4Attach, mcAttach, yinAttach;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> contentArea, statusArea;
    int lastPhraseCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssistEditor)
};
