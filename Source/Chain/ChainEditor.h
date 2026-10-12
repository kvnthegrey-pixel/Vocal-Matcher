#pragma once

#include "ChainProcessor.h"
#include <memory>

/** Dark, single-page editor: one panel per stage, knobs generated from the parameter table. */
class VocalChainEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VocalChainEditor (VocalChainProcessor&);
    ~VocalChainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        const vc::FloatDef* def = nullptr;
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    class Look final : public juce::LookAndFeel_V4
    {
    public:
        Look();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
        juce::Colour accent { 0xff59d8c5 };
    };

    void timerCallback() override;
    static juce::Colour accentFor (int section);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float fill01, juce::Colour, const juce::String&) const;

    VocalChainProcessor& proc;
    Look look;
    std::vector<std::unique_ptr<Knob>> knobs;

    juce::ComboBox presetBox, keyBox, scaleBox;
    juce::ToggleButton tuneOn { "Auto-tune" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tuneAtt;

    juce::Rectangle<int> panel[7];
    juce::Rectangle<float> grMeter, deessMeter, inMeter, outMeter;
    juce::Rectangle<int> noteReadout;
    float grDb = 0, deessDb = 0, inPk = 0, outPk = 0, detMidi = 0, tgtMidi = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChainEditor)
};
