#pragma once

#include "SeatProcessor.h"
#include <memory>

class SeatEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SeatEditor (SeatProcessor&);
    ~SeatEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    /** The one-line status the header shows (tests read it). */
    juce::String statusText() const { return status; }

private:
    class Graph;
    class Look final : public juce::LookAndFeel_V4
    {
    public:
        Look();
    };

    void timerCallback() override;
    void chooseBeat();
    void chooseVocal();
    juce::String buildStatus() const;

    SeatProcessor& proc;
    Look look;
    std::unique_ptr<Graph> graph;
    juce::TextButton beatButton { "Upload beat..." }, vocalButton { "Learn vocal from file..." },
                     resetVocalButton { "Forget vocal" }, clearBeatButton { "Remove beat" };
    juce::ToggleButton followButton { "Follow the beat through the song" }, bypassButton { "Bypass" },
                       freezeButton { "Stop learning the vocal" };
    juce::Slider amount, maxDb, offset, trim;
    juce::Label amountL, maxDbL, offsetL, trimL;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountA, maxDbA, offsetA, trimA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> followA, bypassA, freezeA;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String status, errorText;
    juce::Rectangle<int> reasonsArea, statusArea;
    int lastRev = -1;
    double lastSong = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeatEditor)
};
