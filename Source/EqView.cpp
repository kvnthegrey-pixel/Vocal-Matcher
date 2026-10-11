#include "EqView.h"

namespace
{
constexpr double fLo = 50.0, fHi = 18000.0, dbRange = 18.0;

float xOfHz (const juce::Rectangle<int>& r, double hz)
{
    return (float) r.getX() + (float) (r.getWidth() * std::log (hz / fLo) / std::log (fHi / fLo));
}
float yOfDb (const juce::Rectangle<int>& r, double db)
{
    return (float) r.getCentreY() - (float) (db / dbRange * r.getHeight() * 0.5);
}

// centred on its own average (100 Hz - 8 kHz) so only the shape is compared, not the loudness
va::BandDb centred (const va::BandDb& b)
{
    double m = 0; int n = 0;
    for (int i = 0; i < va::numBands; ++i)
        if (va::bandCentre (i) >= 100.0 && va::bandCentre (i) <= 8000.0) { m += b[(size_t) i]; ++n; }
    m /= std::max (n, 1);
    va::BandDb o {};
    for (int i = 0; i < va::numBands; ++i) o[(size_t) i] = b[(size_t) i] - (float) m;
    return o;
}
}

EqView::EqView (VocalAssistorProcessor& p)
    : proc (p), onAttach (p.apvts, "eqOn", onButton), amountAttach (p.apvts, "eqAmount", amount)
{
    for (auto* b : { &loadButton, &learnButton, &clearButton }) addAndMakeVisible (*b);
    addAndMakeVisible (onButton);
    amount.setSliderStyle (juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    amount.setTextValueSuffix (" %");
    addAndMakeVisible (amount);
    amountLabel.setText ("Amount", juce::dontSendNotification);
    amountLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc6));
    addAndMakeVisible (amountLabel);

    loadButton.onClick = [this] { chooseTone(); };
    learnButton.setClickingTogglesState (true);
    learnButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc62828));
    learnButton.onClick = [this] { proc.setLearning (learnButton.getToggleState()); };
    clearButton.onClick = [this] { proc.clearMyVoice(); learnButton.setToggleState (false, juce::dontSendNotification); repaint(); };
}

void EqView::visibilityChanged()
{
    if (isVisible()) startTimerHz (15); else stopTimer();
}

void EqView::resized()
{
    auto r = getLocalBounds();
    auto strip = r.removeFromBottom (78);
    plotArea = r.reduced (0, 4);
    auto row1 = strip.removeFromTop (34).reduced (0, 3);
    loadButton.setBounds (row1.removeFromLeft (190));
    row1.removeFromLeft (8);
    learnButton.setBounds (row1.removeFromLeft (150));
    row1.removeFromLeft (8);
    clearButton.setBounds (row1.removeFromLeft (150));
    row1.removeFromLeft (20);
    onButton.setBounds (row1.removeFromLeft (130));
    amountLabel.setBounds (row1.removeFromLeft (60));
    amount.setBounds (row1);
    noteArea = strip;
}

void EqView::chooseTone()
{
    if (proc.isToneBusy()) return;
    chooser = std::make_unique<juce::FileChooser> ("Choose the vocal whose tone you want (a Suno vocal stem)",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.flac;*.aif;*.aiff;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.existsAsFile() && ! proc.loadToneReference (f) && onMessage)
                                  onMessage ("Busy", "The tone is already being measured.");
                          });
}

void EqView::timerCallback()
{
    auto err = proc.takeToneError();
    if (err.isNotEmpty() && onMessage) onMessage ("Could not measure the tone", err);
    if (learnButton.getToggleState() != proc.isLearning())
        learnButton.setToggleState (proc.isLearning(), juce::dontSendNotification);
    if (lastRev != proc.eqRevision() || lastLearning != proc.isLearning() || proc.isToneBusy() || proc.isLearning())
    {
        lastRev = proc.eqRevision();
        lastLearning = proc.isLearning();
        repaint();
    }
}

void EqView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101216));
    auto plot = plotArea.reduced (46, 18);

    // grid
    g.setFont (juce::FontOptions (11.0f));
    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yOfDb (plot, db);
        g.setColour (db == 0 ? juce::Colour (0xff3a404c) : juce::Colour (0xff22262e));
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
        g.setColour (juce::Colour (0xff6c727f));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db) + " dB", plot.getX() - 46, (int) y - 7, 42, 14, juce::Justification::centredRight);
    }
    for (double f : { 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xOfHz (plot, f);
        g.setColour (juce::Colour (0xff22262e));
        g.drawVerticalLine ((int) x, (float) plot.getY(), (float) plot.getBottom());
        g.setColour (juce::Colour (0xff6c727f));
        g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 || f == 1000.0 || f == 2000.0 || f == 5000.0 ? 0 : 1) + " k" : juce::String ((int) f),
                    (int) x - 20, plot.getBottom() + 2, 40, 14, juce::Justification::centred);
    }

    auto drawBands = [&] (const va::BandDb& b, juce::Colour c, float thick)
    {
        juce::Path p;
        for (int i = 0; i < va::numBands; ++i)
        {
            const float x = xOfHz (plot, va::bandCentre (i));
            const float y = juce::jlimit ((float) plot.getY(), (float) plot.getBottom(), yOfDb (plot, b[(size_t) i]));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved));
    };

    const bool haveRef = proc.hasToneReference(), haveMine = proc.hasMyVoice();
    if (haveRef)  drawBands (centred (proc.refBands()), juce::Colour (0xffffb300), 2.5f);
    if (haveMine) drawBands (centred (proc.myBands()), juce::Colour (0xff00e5ff), 2.5f);
    if (haveRef && haveMine)
    {
        va::BandDb c = proc.eqCurve();
        const bool on = proc.apvts.getRawParameterValue ("eqOn")->load() >= 0.5f;
        const float k = proc.apvts.getRawParameterValue ("eqAmount")->load() * 0.01f;
        for (auto& v : c) v *= k;
        drawBands (c, on ? juce::Colours::white : juce::Colours::white.withAlpha (0.35f), on ? 3.5f : 1.5f);
    }

    // legend
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    int lx = plot.getX() + 8;
    auto legend = [&] (juce::Colour c, const juce::String& t, int w)
    {
        g.setColour (c); g.fillRect (lx, plot.getY() + 6, 16, 4);
        g.setColour (juce::Colour (0xffd5d8df)); g.drawText (t, lx + 22, plot.getY() - 1, w, 18, juce::Justification::centredLeft);
        lx += w + 40;
    };
    legend (juce::Colour (0xffffb300), "Reference tone", 110);
    legend (juce::Colour (0xff00e5ff), "Your voice", 80);
    legend (juce::Colours::white, "EQ correction", 110);

    // status
    auto n = noteArea;
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (juce::Colour (0xffb8bcc6));
    juce::String refText = proc.isToneBusy() ? "measuring the reference tone..."
                           : haveRef ? "reference tone: " + proc.getToneName() : "no reference tone yet";
    juce::String voiceText = proc.isLearning() ? "learning your voice: " + juce::String (proc.myVoiceSeconds(), 1) + " s (sing or play the vocal)"
                             : haveMine ? "your voice learned (" + juce::String (proc.myVoiceSeconds(), 0) + " s)" : "your voice not learned yet";
    g.drawText (refText + "     |     " + voiceText, n.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff8a8f99));
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText ("1) Load the Suno vocal (or load it as the audio reference on the Pitch tab).  2) Press Learn my voice and play your recorded vocal "
                      "through this insert for 30 s or more, then press it again to stop.  3) Turn Tone EQ on.  Matches tonal balance only: "
                      "Suno's compression, reverb and character are not copied. Limited to +/-12 dB.",
                      n, juce::Justification::topLeft, 3);
}
