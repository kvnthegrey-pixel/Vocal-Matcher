#include "StackEditor.h"

namespace
{
const juce::Colour kBg { 0xff120f14 }, kPanel { 0xff1c1820 }, kText { 0xffe6e0ea }, kDim { 0xff9a90a4 };
const char* const kTitles[5] = { "INPUT", "STACK", "TEXTURE", "UPFRONT", "OUTPUT" };
}

VocalStackEditor::Look::Look()
{
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2430));
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a3342));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff231e29));
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::Label::textColourId, kText);
}

void VocalStackEditor::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float a = start + pos * (end - start);
    g.setColour (juce::Colour (0xff2a2430));
    g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);
    juce::Path track, val;
    track.addCentredArc (c.x, c.y, r * 0.92f, r * 0.92f, 0, start, end, true);
    val.addCentredArc (c.x, c.y, r * 0.92f, r * 0.92f, 0, start, a, true);
    g.setColour (juce::Colour (0xff383040));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path p;
    p.addRoundedRectangle (-1.5f, -r * 0.68f, 3.0f, r * 0.38f, 1.0f);
    g.setColour (kText);
    g.fillPath (p, juce::AffineTransform::rotation (a).translated (c.x, c.y));
}

juce::Colour VocalStackEditor::accentFor (int section)
{
    switch (section)
    {
        case vs::SecStack:   return juce::Colour (0xffb27bff);
        case vs::SecTexture: return juce::Colour (0xffff5c5c);
        case vs::SecUpfront: return juce::Colour (0xffffb347);
        default:             return juce::Colour (0xff7aa2ff);
    }
}

VocalStackEditor::VocalStackEditor (VocalStackProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    setSize (1100, 470);
    for (auto& f : vs::floatDefs())
    {
        auto k = std::make_unique<Knob>();
        k->def = &f;
        k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
        k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, accentFor (f.section));
        k->slider.setColour (juce::Slider::textBoxTextColourId, kText);
        k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k->slider.setDoubleClickReturnValue (true, f.def);
        k->slider.setTextValueSuffix (juce::String (f.unit));
        k->slider.setNumDecimalPlacesToDisplay (f.step >= 1.0f ? 0 : 1);
        k->label.setText (f.label, juce::dontSendNotification);
        k->label.setJustificationType (juce::Justification::centred);
        k->label.setColour (juce::Label::textColourId, kDim);
        k->label.setFont (juce::FontOptions (12.0f));
        k->attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, f.id, k->slider);
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
        knobs.push_back (std::move (k));
    }
    int i = 0;
    for (auto& pr : vs::presets()) presetBox.addItem (pr.name, ++i);
    presetBox.setTextWhenNothingSelected ("Presets...");
    presetBox.onChange = [this] { proc.applyPreset (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);
    resized();
    startTimerHz (30);
}

VocalStackEditor::~VocalStackEditor() { stopTimer(); setLookAndFeel (nullptr); }

void VocalStackEditor::timerCallback()
{
    leadGr = std::abs (proc.engine.leadReductionDb()); stackGr = std::abs (proc.engine.stackReductionDb());
    inPk = proc.engine.inputPeak(); outPk = proc.engine.outputPeak();
    repaint();
}

void VocalStackEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float fill, juce::Colour c, const juce::String& txt) const
{
    g.setColour (juce::Colour (0xff2a2430));
    g.fillRoundedRectangle (r, 3.0f);
    fill = juce::jlimit (0.0f, 1.0f, fill);
    g.setColour (c);
    g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() * (1.0f - fill)), 3.0f);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (txt, r.translated (0, r.getHeight() + 2).withHeight (14).expanded (24, 0), juce::Justification::centred);
}

void VocalStackEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("VOCAL STACK", 20, 12, 300, 30, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("stacked  |  hard  |  upfront      no latency  |  build " VA_VERSION_LABEL, 190, 18, 600, 20, juce::Justification::centredLeft);
    for (int s = 0; s < 5; ++s)
    {
        auto r = panel[s].toFloat();
        g.setColour (kPanel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (accentFor (s));
        g.fillRoundedRectangle (r.withHeight (3.0f), 1.5f);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (kTitles[s], panel[s].getX() + 12, panel[s].getY() + 8, 160, 16, juce::Justification::centredLeft);
    }
    auto pk = [] (float v) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (v, -60.0f) + 60.0f) / 60.0f); };
    drawMeter (g, inMeter, pk (inPk), accentFor (vs::SecInput), "in");
    drawMeter (g, outMeter, pk (outPk), accentFor (vs::SecOutput), "out");
    drawMeter (g, stackMeter, stackGr / 18.0f, accentFor (vs::SecStack), "GR " + juce::String (stackGr, 1));
    drawMeter (g, leadMeter, leadGr / 18.0f, accentFor (vs::SecUpfront), "GR " + juce::String (leadGr, 1));
}

void VocalStackEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    area.removeFromTop (44);
    presetBox.setBounds (getWidth() - 14 - 240, 14, 240, 26);
    const int w = area.getWidth();
    panel[vs::SecInput]   = area.removeFromLeft (w * 11 / 100).reduced (3);
    panel[vs::SecStack]   = area.removeFromLeft (w * 36 / 100).reduced (3);
    panel[vs::SecTexture] = area.removeFromLeft (w * 20 / 100).reduced (3);
    panel[vs::SecUpfront] = area.removeFromLeft (w * 22 / 100).reduced (3);
    panel[vs::SecOutput]  = area.reduced (3);

    std::vector<Knob*> by[5];
    for (auto& k : knobs) by[k->def->section].push_back (k.get());
    auto place = [] (juce::Rectangle<int> a, std::vector<Knob*>& ks, int perRow)
    {
        if (ks.empty()) return;
        const int rows = ((int) ks.size() + perRow - 1) / perRow;
        const int cw = a.getWidth() / perRow, rh = a.getHeight() / rows;
        for (size_t i = 0; i < ks.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (a.getX() + ((int) i % perRow) * cw, a.getY() + ((int) i / perRow) * rh, cw, rh);
            ks[i]->label.setBounds (cell.removeFromTop (14));
            ks[i]->slider.setBounds (cell);
        }
    };
    auto inner = [] (juce::Rectangle<int> p) { return p.withTrimmedTop (28).reduced (6, 2); };
    auto meterCol = [] (juce::Rectangle<int>& a) { auto m = a.removeFromRight (26); return m.reduced (4, 6).withTrimmedBottom (18); };

    { auto a = inner (panel[vs::SecInput]);   inMeter = meterCol (a).toFloat();    place (a, by[vs::SecInput], 1); }
    { auto a = inner (panel[vs::SecStack]);   stackMeter = meterCol (a).toFloat(); place (a, by[vs::SecStack], 3); }
    place (inner (panel[vs::SecTexture]), by[vs::SecTexture], 1 + 1);
    { auto a = inner (panel[vs::SecUpfront]); leadMeter = meterCol (a).toFloat();  place (a, by[vs::SecUpfront], 2); }
    { auto a = inner (panel[vs::SecOutput]);  outMeter = meterCol (a).toFloat();   place (a, by[vs::SecOutput], 1); }
}
