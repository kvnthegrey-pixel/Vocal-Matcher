#include "SeatEditor.h"

namespace
{
const juce::Colour kBg { 0xff101418 }, kPanel { 0xff182027 }, kGrid { 0xff26313a }, kText { 0xffe6e9ee },
                   kDim { 0xff8d99a6 }, kBeat { 0xff5b8def }, kVocal { 0xffffb347 },
                   kCut { 0xffff6b6b }, kBoost { 0xff5fd39a };

constexpr double kLoHz = 40.0, kHiHz = 20000.0;

float xFor (double hz, juce::Rectangle<float> r)
{
    return r.getX() + r.getWidth() * (float) (std::log (hz / kLoHz) / std::log (kHiHz / kLoHz));
}
double hzFor (float x, juce::Rectangle<float> r)
{
    return kLoHz * std::pow (kHiHz / kLoHz, (double) ((x - r.getX()) / r.getWidth()));
}

/** The shape of a spectrum with its overall level removed, so the beat and the vocal can be laid on top of each other. */
std::array<float, (size_t) va::numBands> relative (const va::BandDb& d)
{
    double m = 0.0, w = 0.0;
    for (int b = 0; b < va::numBands; ++b)
    {
        const double f = va::bandCentre (b);
        if (f < 150.0 || f > 6000.0) continue;
        m += d[(size_t) b]; w += 1.0;
    }
    m = w > 0.0 ? m / w : 0.0;
    std::array<float, (size_t) va::numBands> out {};
    for (int b = 0; b < va::numBands; ++b) out[(size_t) b] = d[(size_t) b] - (float) m;
    return out;
}
}

// ---------------------------------------------------------------------------------------------
class SeatEditor::Graph final : public juce::Component
{
public:
    explicit Graph (SeatProcessor& p) : proc (p) {}

    void mouseMove (const juce::MouseEvent& e) override
    {
        hoverX = e.position.x;
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override { hoverX = -1.0f; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (kPanel);
        g.fillRoundedRectangle (area, 8.0f);

        const auto inner = area.reduced (14.0f, 10.0f);
        const float split = inner.getY() + inner.getHeight() * 0.56f;
        auto top = juce::Rectangle<float> (inner.getX() + 34.0f, inner.getY() + 14.0f, inner.getWidth() - 34.0f, split - inner.getY() - 30.0f);
        auto low = juce::Rectangle<float> (inner.getX() + 34.0f, split + 24.0f, inner.getWidth() - 34.0f, inner.getBottom() - split - 44.0f);

        const auto& beat = proc.beatProfile();
        const auto& vocal = proc.vocalProfile();
        const bool follow = proc.followOn();
        const double t = proc.songSeconds();

        drawTop (g, top, beat, vocal, follow, t);
        drawLow (g, low);

        if (hoverX >= low.getX() && hoverX <= low.getRight())
        {
            const double hz = hzFor (hoverX, low);
            const double db = va::eqResponseDb (proc.appliedGains(), hz, proc.sampleRate());
            g.setColour (kText.withAlpha (0.35f));
            g.drawVerticalLine ((int) hoverX, top.getY(), low.getBottom());
            g.setColour (kText);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (ms::hzText (hz)) + "   " + juce::String (db, 1) + " dB", juce::Rectangle<float> (low.getRight() - 170.0f, low.getY() - 20.0f, 170.0f, 16.0f),
                        juce::Justification::centredRight);
        }
    }

private:
    void drawGrid (juce::Graphics& g, juce::Rectangle<float> r, float dbLo, float dbHi, float dbStep, bool labelDb) const
    {
        g.setFont (juce::FontOptions (11.0f));
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xFor (hz, r);
            g.setColour (kGrid);
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            g.setColour (kDim);
            g.drawText (ms::hzText (hz), juce::Rectangle<float> (x - 26.0f, r.getBottom() + 2.0f, 52.0f, 13.0f), juce::Justification::centred);
        }
        for (float db = std::ceil (dbLo / dbStep) * dbStep; db <= dbHi; db += dbStep)
        {
            const float y = r.getBottom() - r.getHeight() * (db - dbLo) / (dbHi - dbLo);
            g.setColour (std::abs (db) < 0.01f ? kDim.withAlpha (0.7f) : kGrid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            if (labelDb)
            {
                g.setColour (kDim);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getX() - 32.0f, y - 7.0f, 28.0f, 14.0f), juce::Justification::centredRight);
            }
        }
    }

    static juce::Path smoothPath (const std::vector<juce::Point<float>>& pts)
    {
        juce::Path p;
        if (pts.empty()) return p;
        p.startNewSubPath (pts.front());
        for (size_t i = 1; i + 1 < pts.size(); ++i)
        {
            const auto mid = (pts[i] + pts[i + 1]) * 0.5f;
            p.quadraticTo (pts[i], mid);
        }
        p.lineTo (pts.back());
        return p;
    }

    void drawTop (juce::Graphics& g, juce::Rectangle<float> r, const ms::BeatProfile& beat, const ms::VocalProfile& vocal,
                  bool follow, double t)
    {
        g.setColour (kText);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("What the beat and your vocal sound like (shape only, loudness removed)", juce::Rectangle<float> (r.getX(), r.getY() - 16.0f, r.getWidth(), 14.0f), juce::Justification::centredLeft);

        float lo = -30.0f, hi = 15.0f;
        std::array<float, (size_t) va::numBands> rb {}, rv {};
        if (beat.valid) { rb = relative (follow ? beat.at (std::max (0.0, t)) : beat.average); }
        if (vocal.valid) rv = relative (vocal.average);
        if (beat.valid || vocal.valid)
        {
            lo = 99.0f; hi = -99.0f;
            for (int b = 0; b < va::numBands; ++b)
            {
                if (beat.valid)  { lo = std::min (lo, rb[(size_t) b]); hi = std::max (hi, rb[(size_t) b]); }
                if (vocal.valid) { lo = std::min (lo, rv[(size_t) b]); hi = std::max (hi, rv[(size_t) b]); }
            }
            lo = std::floor ((lo - 3.0f) / 10.0f) * 10.0f;
            hi = std::ceil ((hi + 3.0f) / 10.0f) * 10.0f;
            if (hi - lo < 30.0f) hi = lo + 30.0f;
        }
        drawGrid (g, r, lo, hi, 10.0f, true);

        auto yFor = [&] (float db) { return r.getBottom() - r.getHeight() * (db - lo) / (hi - lo); };
        auto pointsOf = [&] (const std::array<float, (size_t) va::numBands>& a)
        {
            std::vector<juce::Point<float>> pts;
            for (int b = 0; b < va::numBands; ++b)
                pts.push_back ({ xFor (va::bandCentre (b), r), yFor (a[(size_t) b]) });
            return pts;
        };

        if (beat.valid)
        {
            auto line = smoothPath (pointsOf (rb));
            juce::Path fill = line;
            fill.lineTo (xFor (va::bandCentre (va::numBands - 1), r), r.getBottom());
            fill.lineTo (xFor (va::bandCentre (0), r), r.getBottom());
            fill.closeSubPath();
            g.setColour (kBeat.withAlpha (0.22f));
            g.fillPath (fill);
            g.setColour (kBeat);
            g.strokePath (line, juce::PathStrokeType (2.0f));
        }
        if (vocal.valid)
        {
            g.setColour (kVocal);
            g.strokePath (smoothPath (pointsOf (rv)), juce::PathStrokeType (2.2f));
        }

        // key
        g.setFont (juce::FontOptions (12.0f));
        float kx = r.getRight() - 230.0f;
        g.setColour (kBeat);   g.fillRoundedRectangle (kx, r.getY() + 5.0f, 14.0f, 4.0f, 2.0f);
        g.setColour (kText);   g.drawText ("Beat", juce::Rectangle<float> (kx + 18.0f, r.getY(), 40.0f, 14.0f), juce::Justification::centredLeft);
        g.setColour (kVocal);  g.fillRoundedRectangle (kx + 80.0f, r.getY() + 5.0f, 14.0f, 4.0f, 2.0f);
        g.setColour (kText);   g.drawText ("Your vocal", juce::Rectangle<float> (kx + 98.0f, r.getY(), 80.0f, 14.0f), juce::Justification::centredLeft);

        if (! beat.valid || ! vocal.valid)
        {
            g.setColour (kText.withAlpha (0.9f));
            g.setFont (juce::FontOptions (15.0f));
            g.drawFittedText (! beat.valid ? "Upload your beat to begin" : "Play your vocal through this track so it can learn how it sounds",
                              r.toNearestInt().reduced (60, 40), juce::Justification::centred, 2);
        }
    }

    void drawLow (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (kText);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("What Mix Seat does to your vocal (dB)", juce::Rectangle<float> (r.getX(), r.getY() - 18.0f, r.getWidth() - 180.0f, 14.0f), juce::Justification::centredLeft);

        const float range = 8.0f;
        drawGrid (g, r, -range, range, 4.0f, true);

        const double sr = proc.sampleRate();
        const auto& gains = proc.appliedGains();
        juce::Path curve;
        constexpr int n = 180;
        std::vector<juce::Point<float>> pts;
        for (int i = 0; i < n; ++i)
        {
            const double hz = kLoHz * std::pow (kHiHz / kLoHz, (double) i / (n - 1));
            const float db = std::clamp ((float) va::eqResponseDb (gains, hz, sr), -range, range);
            pts.push_back ({ xFor (hz, r), r.getCentreY() - r.getHeight() * 0.5f * db / range });
        }
        curve.startNewSubPath (pts.front());
        for (size_t i = 1; i < pts.size(); ++i) curve.lineTo (pts[i]);

        juce::Path area = curve;
        area.lineTo (pts.back().x, r.getCentreY());
        area.lineTo (pts.front().x, r.getCentreY());
        area.closeSubPath();
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (r.getSmallestIntegerContainer().withBottom ((int) r.getCentreY()));
            g.setColour (kBoost.withAlpha (0.35f));
            g.fillPath (area);
        }
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (r.getSmallestIntegerContainer().withTop ((int) r.getCentreY()));
            g.setColour (kCut.withAlpha (0.35f));
            g.fillPath (area);
        }
        g.setColour (kText);
        g.strokePath (curve, juce::PathStrokeType (2.0f));
    }

    SeatProcessor& proc;
    float hoverX = -1.0f;
};

// ---------------------------------------------------------------------------------------------
SeatEditor::Look::Look()
{
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff22303b));
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::Label::textColourId, kText);
    setColour (juce::Slider::thumbColourId, kVocal);
    setColour (juce::Slider::trackColourId, kVocal.withAlpha (0.7f));
    setColour (juce::Slider::backgroundColourId, juce::Colour (0xff2a3640));
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ToggleButton::textColourId, kText);
    setColour (juce::ToggleButton::tickColourId, kVocal);
    setColour (juce::ToggleButton::tickDisabledColourId, kDim);
}

SeatEditor::SeatEditor (SeatProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);

    graph = std::make_unique<Graph> (proc);
    addAndMakeVisible (*graph);

    for (auto* b : { &beatButton, &vocalButton, &resetVocalButton, &clearBeatButton })
        addAndMakeVisible (*b);
    beatButton.onClick = [this] { chooseBeat(); };
    vocalButton.onClick = [this] { chooseVocal(); };
    resetVocalButton.onClick = [this] { errorText.clear(); proc.resetVocal(); };
    clearBeatButton.onClick = [this] { errorText.clear(); proc.clearBeat(); };

    auto setupSlider = [this] (juce::Slider& s, juce::Label& l, const char* text, const char* suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        s.setTextValueSuffix (suffix);
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, kDim);
        l.setFont (juce::FontOptions (12.5f));
        addAndMakeVisible (s);
        addAndMakeVisible (l);
    };
    setupSlider (amount, amountL, "How much", " %");
    setupSlider (maxDb, maxDbL, "Biggest change", " dB");
    setupSlider (offset, offsetL, "Beat starts at", " s");
    setupSlider (trim, trimL, "Output trim", " dB");
    amountA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "amount", amount);
    maxDbA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "maxDb", maxDb);
    offsetA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "beatOffset", offset);
    trimA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "trim", trim);

    for (auto* b : { &followButton, &bypassButton, &freezeButton })
        addAndMakeVisible (*b);
    followA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "follow", followButton);
    bypassA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bypass", bypassButton);
    freezeA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "freeze", freezeButton);

    status = buildStatus();
    setSize (980, 620);                 // last: this calls resized(), which needs every child to exist
    startTimerHz (12);
}

SeatEditor::~SeatEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SeatEditor::chooseBeat()
{
    errorText.clear();
    chooser = std::make_unique<juce::FileChooser> ("Choose the beat (the instrumental, without the vocal)", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile() && ! proc.loadBeat (f))
                                  errorText = "Still analysing the last file. Wait for it to finish.";
                          });
}

void SeatEditor::chooseVocal()
{
    errorText.clear();
    chooser = std::make_unique<juce::FileChooser> ("Choose a dry vocal (optional: you can also just play the vocal through this track)", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile() && ! proc.loadVocalFile (f))
                                  errorText = "Still analysing the last file. Wait for it to finish.";
                          });
}

juce::String SeatEditor::buildStatus() const
{
    if (errorText.isNotEmpty()) return errorText;
    if (proc.isAnalysing())
        return (proc.analysingBeat() ? "Reading the beat: " : "Reading the vocal: ") + proc.analysisName() + "  ("
               + juce::String ((int) std::lround (proc.analysisProgress() * 100.0f)) + "%)";

    const auto& beat = proc.beatProfile();
    const auto& vocal = proc.vocalProfile();
    if (! beat.valid && ! vocal.valid && proc.vocalProfile().seconds <= 0.0)
        return "Step 1: upload your beat.  Step 2: play your vocal through this track.";
    if (! beat.valid)
        return "Vocal learned. Now upload your beat.";
    if (! vocal.valid)
        return "Beat loaded (" + juce::String (beat.name) + "). Now play your vocal through this track so it can learn how it sounds.";
    if (proc.bypassed())
        return "Bypassed: your vocal is untouched. Beat: " + juce::String (beat.name);
    return "Active.  Beat: " + juce::String (beat.name) + "  |  vocal learned from "
           + juce::String (vocal.seconds, 1) + " s of singing"
           + (proc.followOn() ? "  |  following the beat" : "  |  using the whole-song average");
}

void SeatEditor::timerCallback()
{
    if (auto e = proc.takeError(); e.isNotEmpty())
        errorText = e;
    auto s = buildStatus();
    const double song = proc.followOn() ? std::floor (proc.songSeconds() * 2.0) : 0.0;
    if (s != status || proc.revision() != lastRev || song != lastSong || proc.isAnalysing())
    {
        status = s;
        lastRev = proc.revision();
        lastSong = song;
        repaint();
    }
    else if (graph->isMouseOver())
        graph->repaint();
}

void SeatEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto head = r.removeFromTop (34);
    head.removeFromLeft (190);
    clearBeatButton.setBounds (head.removeFromRight (110).reduced (2, 2));
    resetVocalButton.setBounds (head.removeFromRight (110).reduced (2, 2));
    vocalButton.setBounds (head.removeFromRight (190).reduced (2, 2));
    beatButton.setBounds (head.removeFromRight (140).reduced (2, 2));

    statusArea = r.removeFromTop (26);
    graph->setBounds (r.removeFromTop (340));
    r.removeFromTop (12);

    auto left = r.removeFromLeft (r.getWidth() * 55 / 100);
    reasonsArea = left;
    r.removeFromLeft (16);

    auto row = [&] (juce::Label& l, juce::Slider& s)
    {
        auto line = r.removeFromTop (30);
        l.setBounds (line.removeFromLeft (110));
        s.setBounds (line);
    };
    row (amountL, amount);
    row (maxDbL, maxDb);
    row (offsetL, offset);
    row (trimL, trim);
    auto toggles = r.removeFromTop (28);
    followButton.setBounds (toggles);
    auto t2 = r.removeFromTop (28);
    bypassButton.setBounds (t2.removeFromLeft (t2.getWidth() / 2));
    freezeButton.setBounds (t2);
}

void SeatEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kText);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Mix Seat", 16, 12, 180, 34, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String ("v") + VA_VERSION_LABEL, 112, 20, 100, 16, juce::Justification::centredLeft);

    g.setColour (errorText.isNotEmpty() ? kCut : kDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 1);

    // why the EQ is doing what it is doing
    g.setColour (kPanel);
    g.fillRoundedRectangle (reasonsArea.toFloat(), 8.0f);
    auto box = reasonsArea.reduced (14, 10);
    g.setColour (kText);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Why it is moving what it is moving", box.removeFromTop (18), juce::Justification::centredLeft);
    box.removeFromTop (4);

    g.setFont (juce::FontOptions (12.5f));
    if (! proc.plan().valid)
    {
        g.setColour (kDim);
        g.drawFittedText ("Mix Seat measures where the beat is busy and where it leaves room, then nudges your vocal's EQ out of the busy spots and into the open ones. "
                          "It needs both: the beat file, and a few seconds of your vocal.",
                          box, juce::Justification::topLeft, 5, 1.0f);
        return;
    }

    const auto curve = proc.plan().curveAt (std::max (0.0, proc.songSeconds()), proc.followOn());
    const auto moves = ms::explain (curve, 4);
    if (moves.empty())
    {
        g.setColour (kDim);
        g.drawFittedText ("Nothing to change here: the beat and your vocal already leave each other room.", box, juce::Justification::topLeft, 3);
        return;
    }
    for (const auto& m : moves)
    {
        auto line = box.removeFromTop (std::min (44, std::max (30, box.getHeight() / 4)));
        g.setColour (m.db < 0.0f ? kCut : kBoost);
        g.fillEllipse ((float) line.getX(), (float) line.getY() + 5.0f, 8.0f, 8.0f);
        g.setColour (kText);
        g.drawFittedText (m.text, line.withTrimmedLeft (16), juce::Justification::topLeft, 2, 1.0f);
    }
}
