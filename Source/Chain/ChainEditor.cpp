#include "ChainEditor.h"

namespace
{
const juce::Colour kBg { 0xff101218 }, kPanel { 0xff181b24 }, kText { 0xffdfe3ec }, kDim { 0xff8b93a6 };
const char* const kTitles[7] = { "INPUT", "AUTO-TUNE", "COMPRESSOR", "DE-ESSER", "COLOUR", "REVERB", "OUTPUT" };

juce::String noteName (float midi)
{
    if (midi < 12.0f) return "--";
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int m = (int) std::lround (midi);
    return juce::String (n[m % 12]) + juce::String (m / 12 - 1);
}
}

VocalChainEditor::Look::Look()
{
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff232837));
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2f3548));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c2030));
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::Label::textColourId, kText);
    setColour (juce::ToggleButton::textColourId, kText);
    setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff59d8c5));
}

void VocalChainEditor::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float d = std::min (b.getWidth(), b.getHeight()), r = d * 0.5f;
    const auto c = b.getCentre();
    const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float a = start + pos * (end - start);

    g.setColour (juce::Colour (0xff232837));
    g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);

    juce::Path track, val;
    track.addCentredArc (c.x, c.y, r * 0.92f, r * 0.92f, 0, start, end, true);
    val.addCentredArc (c.x, c.y, r * 0.92f, r * 0.92f, 0, start, a, true);
    g.setColour (juce::Colour (0xff2c3245));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path p;
    p.addRoundedRectangle (-1.5f, -r * 0.68f, 3.0f, r * 0.38f, 1.0f);
    g.setColour (kText);
    g.fillPath (p, juce::AffineTransform::rotation (a).translated (c.x, c.y));
}

juce::Colour VocalChainEditor::accentFor (int section)
{
    switch (section)
    {
        case vc::SecTune:   return juce::Colour (0xff59d8c5);
        case vc::SecComp:   return juce::Colour (0xfff2b84b);
        case vc::SecDeess:  return juce::Colour (0xffe8805a);
        case vc::SecColour: return juce::Colour (0xffd4c06a);
        case vc::SecReverb: return juce::Colour (0xffe56fb8);
        default:            return juce::Colour (0xff7aa2ff);
    }
}

VocalChainEditor::VocalChainEditor (VocalChainProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    setSize (1180, 640);

    for (auto& f : vc::floatDefs())
    {
        auto k = std::make_unique<Knob>();
        k->def = &f;
        k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
        k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, accentFor (f.section));
        k->slider.setColour (juce::Slider::textBoxTextColourId, kText);
        k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k->slider.setDoubleClickReturnValue (true, f.def);
        const juce::String unit (f.unit);
        k->slider.setTextValueSuffix (unit);
        k->slider.setNumDecimalPlacesToDisplay (f.step >= 1.0f ? 0 : (f.step >= 0.1f ? 1 : 2));
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
    for (auto& pr : vc::presets()) presetBox.addItem (pr.name, ++i);
    presetBox.setTextWhenNothingSelected ("Presets...");
    presetBox.onChange = [this] { proc.applyPreset (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    keyBox.addItemList (vc::keyNames(), 1);
    scaleBox.addItemList (vc::scaleNames(), 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    addAndMakeVisible (tuneOn);
    keyAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "key", keyBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "scale", scaleBox);
    tuneAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "tuneOn", tuneOn);

    resized();
    startTimerHz (30);
}

VocalChainEditor::~VocalChainEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VocalChainEditor::timerCallback()
{
    grDb = std::abs (proc.engine.gainReductionDb());
    deessDb = std::abs (proc.engine.deessCutDb());
    inPk = proc.engine.inputPeak();
    outPk = proc.engine.outputPeak();
    detMidi = proc.engine.detectedMidi();
    tgtMidi = proc.engine.targetMidi();
    repaint();
}

void VocalChainEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float fill, juce::Colour c, const juce::String& txt) const
{
    g.setColour (juce::Colour (0xff232837));
    g.fillRoundedRectangle (r, 3.0f);
    fill = juce::jlimit (0.0f, 1.0f, fill);
    g.setColour (c);
    g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() * (1.0f - fill)), 3.0f);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (txt, r.translated (0, r.getHeight() + 2).withHeight (14).expanded (24, 0), juce::Justification::centred);
}

void VocalChainEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("VOCAL CHAIN", 20, 12, 300, 30, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("tune  >  compress  >  de-ess  >  colour  >  reverb      latency 2048 samples (host-compensated)  |  build " VA_VERSION_LABEL,
                190, 18, 700, 20, juce::Justification::centredLeft);

    for (int s = 0; s < 7; ++s)
    {
        auto r = panel[s].toFloat();
        g.setColour (kPanel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (accentFor (s));
        g.fillRoundedRectangle (r.withHeight (3.0f), 1.5f);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (kTitles[s], panel[s].getX() + 12, panel[s].getY() + 8, 160, 16, juce::Justification::centredLeft);
    }

    // note read-out: what you sang and where the corrector is pulling it
    g.setColour (kText);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    const bool voiced = detMidi >= 12.0f && tuneOn.getToggleState();
    g.drawText (voiced ? noteName (detMidi) + "  >  " + noteName (tgtMidi) : "--", noteReadout.withHeight (26), juce::Justification::centred);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("sung > target", noteReadout.withTrimmedTop (28).withHeight (14), juce::Justification::centred);

    drawMeter (g, grMeter,    grDb / 18.0f,    accentFor (vc::SecComp),  "GR " + juce::String (grDb, 1));
    drawMeter (g, deessMeter, deessDb / 12.0f, accentFor (vc::SecDeess), "cut " + juce::String (deessDb, 1));
    auto pk = [] (float v) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (v, -60.0f) + 60.0f) / 60.0f); };
    drawMeter (g, inMeter,  pk (inPk),  accentFor (vc::SecInput),  "in");
    drawMeter (g, outMeter, pk (outPk), accentFor (vc::SecOutput), "out");
}

void VocalChainEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    area.removeFromTop (44);
    presetBox.setBounds (getWidth() - 14 - 220, 14, 220, 26);

    // two rows: [input | auto-tune | compressor | de-esser] and [colour | reverb | output]
    auto top = area.removeFromTop ((area.getHeight() - 10) / 2);
    area.removeFromTop (10);
    auto bottom = area;

    const int w = top.getWidth();
    panel[vc::SecInput]  = top.removeFromLeft (w * 13 / 100).reduced (3);
    panel[vc::SecTune]   = top.removeFromLeft (w * 27 / 100).reduced (3);
    panel[vc::SecComp]   = top.removeFromLeft (w * 38 / 100).reduced (3);
    panel[vc::SecDeess]  = top.reduced (3);
    panel[vc::SecColour] = bottom.removeFromLeft (w * 30 / 100).reduced (3);
    panel[vc::SecReverb] = bottom.removeFromLeft (w * 55 / 100).reduced (3);
    panel[vc::SecOutput] = bottom.reduced (3);

    // knobs, laid out in a row (reverb: two rows of five)
    std::vector<Knob*> bySection[7];
    for (auto& k : knobs) bySection[k->def->section].push_back (k.get());

    auto place = [] (juce::Rectangle<int> area, std::vector<Knob*>& ks, int perRow)
    {
        if (ks.empty()) return;
        const int rows = ((int) ks.size() + perRow - 1) / perRow;
        const int cw = area.getWidth() / perRow, rh = area.getHeight() / rows;
        for (size_t i = 0; i < ks.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (area.getX() + ((int) i % perRow) * cw, area.getY() + ((int) i / perRow) * rh, cw, rh);
            ks[i]->label.setBounds (cell.removeFromTop (14));
            ks[i]->slider.setBounds (cell);
        }
    };

    auto inner = [] (juce::Rectangle<int> p) { return p.withTrimmedTop (28).reduced (6, 2); };

    // meters sit at the right edge of their panels
    auto meterCol = [] (juce::Rectangle<int>& a, int width) { auto m = a.removeFromRight (width); return m.reduced (4, 6).withTrimmedBottom (18); };

    { auto a = inner (panel[vc::SecInput]);
      inMeter = meterCol (a, 26).toFloat();
      place (a, bySection[vc::SecInput], 1); }

    { auto a = inner (panel[vc::SecTune]);
      auto head = a.removeFromTop (28);
      tuneOn.setBounds (head.removeFromLeft (100));
      keyBox.setBounds (head.removeFromLeft (60).reduced (2, 1));
      scaleBox.setBounds (head.reduced (2, 1));
      auto readout = a.removeFromTop (48);
      noteReadout = readout;
      place (a, bySection[vc::SecTune], 2); }

    { auto a = inner (panel[vc::SecComp]);
      grMeter = meterCol (a, 26).toFloat();
      place (a, bySection[vc::SecComp], 3); }

    { auto a = inner (panel[vc::SecDeess]);
      deessMeter = meterCol (a, 26).toFloat();
      place (a, bySection[vc::SecDeess], 1); }

    place (inner (panel[vc::SecColour]), bySection[vc::SecColour], 2);
    place (inner (panel[vc::SecReverb]), bySection[vc::SecReverb], 5);

    { auto a = inner (panel[vc::SecOutput]);
      outMeter = meterCol (a, 26).toFloat();
      place (a, bySection[vc::SecOutput], 1); }
}
