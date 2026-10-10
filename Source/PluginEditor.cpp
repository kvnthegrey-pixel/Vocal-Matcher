#include "PluginEditor.h"

VocalAssistorEditor::VocalAssistorEditor (VocalAssistorProcessor& p)
    : AudioProcessorEditor (&p)
{
    setSize (720, 360);
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
    g.drawText (juce::String ("Version ") + VA_VERSION_LABEL,
                area.removeFromTop (28), juce::Justification::centredLeft);

    area.removeFromTop (16);

    g.setColour (juce::Colour (0xffb8bcc6));
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText ("Phase 1 build. The plugin loads and passes your audio through unchanged.\n"
                      "The reference prompt, note blocks and piano roll arrive in later phases.",
                      area, juce::Justification::topLeft, 4);
}
