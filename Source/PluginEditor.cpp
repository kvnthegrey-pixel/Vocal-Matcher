#include "PluginEditor.h"

namespace
{
juce::String noteName (double hz, double a4, int& centsOut)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 (hz / a4);
    const int nearest = (int) std::lround (midi);
    centsOut = (int) std::lround ((midi - nearest) * 100.0);
    return juce::String (names[((nearest % 12) + 12) % 12]) + juce::String (nearest / 12 - 1);
}

void setupSlider (juce::Slider& s, juce::Label& l, const juce::String& text, juce::Component& parent)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    parent.addAndMakeVisible (s);
    l.setText (text, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc6));
    parent.addAndMakeVisible (l);
}
}

VocalAssistorEditor::VocalAssistorEditor (VocalAssistorProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      methodAttach (p.apvts, "method", methodBox),
      mcAttach (p.apvts, "gateMcLeod", gateMcLeod),
      yinAttach (p.apvts, "gateYin", gateYin),
      latAttach (p.apvts, "latencyMs", latency)
{
    methodBox.addItemList ({ "McLeod", "YIN" }, 1);
    addAndMakeVisible (methodBox);
    methodLabel.setText ("Pitch method", juce::dontSendNotification);
    methodLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc6));
    addAndMakeVisible (methodLabel);

    setupSlider (gateMcLeod, gateMcLeodLabel, "Voicing gate, McLeod (higher is stricter)", *this);
    setupSlider (gateYin, gateYinLabel, "Voicing gate, YIN (lower is stricter)", *this);
    setupSlider (latency, latencyLabel, "Live trace latency compensation (ms)", *this);

    setSize (720, 420);
    startTimerHz (30);
}

void VocalAssistorEditor::resized()
{
    auto r = getLocalBounds().reduced (32);
    r.removeFromTop (44 + 28 + 8);
    readoutArea = r.removeFromTop (120);
    r.removeFromTop (8);

    auto row = [&r] (juce::Component& label, juce::Component& control)
    {
        auto line = r.removeFromTop (30);
        label.setBounds (line.removeFromLeft (300));
        control.setBounds (line);
    };
    row (methodLabel, methodBox);
    row (gateMcLeodLabel, gateMcLeod);
    row (gateYinLabel, gateYin);
    row (latencyLabel, latency);
}

void VocalAssistorEditor::timerCallback()
{
    va::PitchPoint pts[256];
    int n;
    bool changed = false;
    while ((n = proc.engine.popPoints (pts, 256)) > 0)
    {
        latest = pts[n - 1];
        haveLatest = true;
        changed = true;
    }
    if (changed)
        repaint (readoutArea);
}

void VocalAssistorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));

    auto area = getLocalBounds().reduced (32);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("Vocal Assistor", area.removeFromTop (44), juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff7fb2ff));
    g.setFont (juce::FontOptions (16.0f));
    g.drawText (juce::String ("Version ") + VA_VERSION_LABEL + "   (Phase 2: pitch engine test window)",
                area.removeFromTop (28), juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff1f232b));
    g.fillRoundedRectangle (readoutArea.toFloat(), 8.0f);

    auto box = readoutArea.reduced (16);
    const double sr = proc.engine.getSampleRate();

    if (haveLatest && latest.voiced)
    {
        int cents = 0;
        const auto name = noteName (latest.hz, 440.0, cents);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
        g.drawText (name + "   " + (cents >= 0 ? "+" : "") + juce::String (cents) + " cents",
                    box.removeFromTop (56), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xffb8bcc6));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (juce::String (latest.hz, 2) + " Hz   confidence " + juce::String (latest.confidence, 2),
                    box.removeFromTop (22), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (juce::Colour (0xff8a8f99));
        g.setFont (juce::FontOptions (28.0f));
        g.drawText (haveLatest ? "no pitch" : "waiting for audio", box.removeFromTop (56), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Sing or play audio through this insert.", box.removeFromTop (22), juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (0xff8a8f99));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String ("Analysis delay ") + juce::String (va::PitchEngine::algorithmicDelayMs (sr), 1)
                    + " ms (window 2048, hop 256).  Dropped samples: " + juce::String (proc.engine.getDroppedSamples()),
                box, juce::Justification::bottomLeft);
}
