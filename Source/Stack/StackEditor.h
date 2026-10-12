#pragma once

#include "StackProcessor.h"
#include <memory>

class VocalStackEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VocalStackEditor (VocalStackProcessor&);
    ~VocalStackEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        const vs::FloatDef* def = nullptr;
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    class Look final : public juce::LookAndFeel_V4
    {
    public:
        Look();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    };

    void timerCallback() override;
    static juce::Colour accentFor (int section);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float fill01, juce::Colour, const juce::String&) const;

    VocalStackProcessor& proc;
    Look look;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ComboBox presetBox;
    juce::Rectangle<int> panel[5];
    juce::Rectangle<float> leadMeter, stackMeter, inMeter, outMeter;
    float leadGr = 0, stackGr = 0, inPk = 0, outPk = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalStackEditor)
};
