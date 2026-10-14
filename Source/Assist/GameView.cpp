#include "GameView.h"

namespace pa
{
namespace
{
constexpr float playheadFraction = 0.30f;       // the playhead sits 30% in from the left of the lane
constexpr float labelWidth = 40.0f;             // note names down the left edge

juce::String formatScore (int score)
{
    auto s = juce::String (score);
    for (int i = s.length() - 3; i > 0; i -= 3)
        s = s.substring (0, i) + "," + s.substring (i);
    return s;
}

juce::String formatTime (double sec)
{
    sec = std::max (0.0, sec);
    const int m = (int) (sec / 60.0);
    return juce::String (m) + ":" + juce::String (sec - m * 60.0, 1).paddedLeft ('0', 4);
}
}

juce::String GameView::noteName (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

GameView::GameView (AssistProcessor& p) : proc (p)
{
    proc.reference.addChangeListener (this);
    refreshBlocks();
    seenGradeRevision = proc.score().lastGradeRevision();
}

GameView::~GameView() { proc.reference.removeChangeListener (this); }

void GameView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshBlocks();
    repaint();
}

void GameView::refreshBlocks()
{
    blocks = proc.reference.blocksCopy().blocks();
    if (blocks.empty())
        return;
    double lo = 127.0, hi = 0.0;
    for (const auto& b : blocks)
    {
        lo = std::min (lo, b.midi());
        hi = std::max (hi, b.midi());
    }
    lowMidi = (int) std::floor (lo) - 3;
    highMidi = (int) std::ceil (hi) + 3;
    while (highMidi - lowMidi < 14)
    {
        --lowMidi;
        ++highMidi;
    }
    overviewStartSec = blocks.front().startSec();
    overviewEndSec = std::max (blocks.back().endSec(), overviewStartSec + 1.0);
}

void GameView::tick()
{
    const int rev = proc.score().lastGradeRevision();
    if (rev != seenGradeRevision)
    {
        seenGradeRevision = rev;
        flashGrade = proc.score().lastGrade();
        flashPoints = proc.score().lastGradePoints();
        flashStartMs = juce::Time::getMillisecondCounter();
    }
}

double GameView::yOfMidi (double midi, juce::Rectangle<float> lane) const
{
    const double span = (double) (highMidi - lowMidi);
    return lane.getBottom() - (midi - (double) lowMidi) / span * lane.getHeight();
}

int GameView::blockIndexAtRef (double refSec) const
{
    int lo = 0, hi = (int) blocks.size() - 1, found = -1;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        if (blocks[(size_t) mid].startSec() <= refSec) { found = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    if (found >= 0 && refSec < blocks[(size_t) found].endSec())
        return found;
    return -1;
}

void GameView::mouseDown (const juce::MouseEvent& e)
{
    if (! overviewArea.contains (e.getPosition()) || blocks.empty())
        return;

    // Click a phrase in the overview strip to focus it (click it again to go back to all).
    const auto& keeper = proc.score();
    const double offset = proc.viewSettings().refOffsetSec;
    const double t = overviewStartSec + offset
                     + (double) (e.x - overviewArea.getX()) / (double) overviewArea.getWidth() * (overviewEndSec - overviewStartSec);
    int best = -1;
    double bestDist = 1.0e9;
    for (int p = 0; p < keeper.phraseCount(); ++p)
    {
        double a = 0, b = 0;
        if (! keeper.phraseRange (p, a, b)) continue;
        const double d = t < a ? a - t : (t > b ? t - b : 0.0);
        if (d < bestDist) { bestDist = d; best = p; }
    }
    if (best < 0) return;
    if (auto* focus = dynamic_cast<juce::AudioParameterInt*> (proc.apvts.getParameter ("focus")))
    {
        const int current = focus->get();
        *focus = (current == best + 1) ? 0 : best + 1;
    }
}

void GameView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101216));
    if (blocks.empty())
    {
        drawEmpty (g);
        return;
    }

    auto area = getLocalBounds();
    const double tPlay = proc.playheadSeconds();

    drawHud (g, area.removeFromTop (74), tPlay);
    auto bottom = area.removeFromBottom (92);
    area.removeFromBottom (6);
    const auto lane = area.toFloat();
    const float playheadX = lane.getX() + labelWidth + (lane.getWidth() - labelWidth) * playheadFraction;
    const float pxPerSec = (lane.getWidth() - labelWidth) / (float) std::max (1.0, proc.windowSeconds());

    g.saveState();
    g.reduceClipRegion (area);
    drawLane (g, lane, tPlay);
    drawBlocks (g, lane, tPlay, playheadX, pxPerSec);
    drawVoice (g, lane, tPlay, playheadX, pxPerSec);
    g.restoreState();

    // octave warning
    const auto& live = proc.score().liveNote();
    if (proc.isPlaying() && live.octave != 0)
    {
        auto banner = juce::Rectangle<float> (0, 0, 330, 34).withCentre ({ playheadX + 150.0f, lane.getY() + 30.0f });
        g.setColour (juce::Colour (0xffe65100).withAlpha (0.92f));
        g.fillRoundedRectangle (banner, 8.0f);
        g.setColour (juce::Colours::white);
        // a small arrow: up when you are too high, down when too low
        juce::Path arrow;
        const auto c = banner.getCentre().withX (banner.getX() + 24.0f);
        if (live.octave > 0) arrow.addTriangle (c.x, c.y - 9.0f, c.x - 9.0f, c.y + 7.0f, c.x + 9.0f, c.y + 7.0f);
        else                 arrow.addTriangle (c.x, c.y + 9.0f, c.x - 9.0f, c.y - 7.0f, c.x + 9.0f, c.y - 7.0f);
        g.fillPath (arrow);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (live.octave > 0 ? "OCTAVE TOO HIGH - sing lower" : "OCTAVE TOO LOW - sing higher",
                    banner.withTrimmedLeft (44.0f), juce::Justification::centredLeft);
    }

    auto readout = bottom.removeFromLeft (420);
    bottom.removeFromLeft (10);
    drawReadout (g, readout);
    drawOverview (g, bottom, tPlay);
}

void GameView::drawEmpty (juce::Graphics& g)
{
    g.setColour (colours::text());
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Add your reference vocal to begin", getLocalBounds().withSizeKeepingCentre (620, 34).translated (0, -80), juce::Justification::centred);
    g.setColour (colours::dim());
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText ("Load the Suno vocal (a MIDI file is exact; an audio stem is analysed into notes). "
                      "Then press play in Cubase and sing: your voice appears as a wave that hits the notes.",
                      getLocalBounds().withSizeKeepingCentre (620, 60).translated (0, -26), juce::Justification::centred, 3);
}

void GameView::drawLane (juce::Graphics& g, juce::Rectangle<float> lane, double /*tPlay*/)
{
    const auto vs = proc.viewSettings();
    const float rowH = lane.getHeight() / (float) (highMidi - lowMidi);

    g.setColour (juce::Colour (0xff15171c));
    g.fillRect (lane);

    for (int m = lowMidi; m < highMidi; ++m)
    {
        const float y = (float) yOfMidi ((double) m + 1.0, lane);
        const int pc = ((m % 12) + 12) % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        const bool inKey = va::inScale (pc, vs.keyRoot, vs.scale);
        juce::Colour row = black ? juce::Colour (0xff12141a) : juce::Colour (0xff191c23);
        if (vs.scale != 0 && inKey) row = row.brighter (0.12f);
        g.setColour (row);
        g.fillRect (lane.getX() + labelWidth, y, lane.getWidth() - labelWidth, rowH);
        if (pc == 0)
        {
            g.setColour (juce::Colour (0xff2c313b));
            g.drawHorizontalLine ((int) (y + rowH), lane.getX() + labelWidth, lane.getRight());
        }
        if (pc == 0 || rowH >= 14.0f)
        {
            g.setColour (pc == 0 ? juce::Colour (0xffb8bcc6) : juce::Colour (0xff6c727f));
            g.setFont (juce::FontOptions (pc == 0 ? 12.0f : 10.0f));
            g.drawText (noteName (m), (int) lane.getX() + 2, (int) y, (int) labelWidth - 6, (int) std::max (rowH, 10.0f), juce::Justification::centredRight);
        }
    }
}

void GameView::drawBlocks (juce::Graphics& g, juce::Rectangle<float> lane, double tPlay, float playheadX, float pxPerSec)
{
    const auto vs = proc.viewSettings();
    const auto& keeper = proc.score();
    const auto cfg = keeper.settings();
    const bool ghost = proc.ghostMode();
    const float rowH = lane.getHeight() / (float) (highMidi - lowMidi);
    const float h = std::max (7.0f, rowH * 0.86f);
    const auto& live = keeper.liveNote();
    auto xOf = [&] (double t) { return playheadX + (float) (t - tPlay) * pxPerSec; };

    for (const auto& b : blocks)
    {
        const double ps = b.startSec() + vs.refOffsetSec, pe = b.endSec() + vs.refOffsetSec;
        const float x0 = xOf (ps), x1 = xOf (pe);
        if (x1 < lane.getX() + labelWidth || x0 > lane.getRight())
            continue;

        const bool focused = cfg.focusPhrase < 0 || cfg.focusPhrase >= keeper.phraseCount() || keeper.phraseOfBlock (b.id) == cfg.focusPhrase;
        const auto grade = keeper.gradeOf (b.id);
        const bool carried = keeper.isCarried (b.id);
        const bool finished = grade != va::Grade::None;
        const bool active = tPlay >= ps && tPlay < pe;

        // Ghost mode: only the first note of each phrase is shown ahead of time; the rest appear once graded.
        if (ghost && ! finished && ! carried && ! keeper.isPhraseFirst (b.id))
            continue;

        const double midi = b.midi() + 12.0 * vs.octaveShift;
        const float y = (float) yOfMidi (midi + 0.5, lane) - h * 0.5f;
        auto r = juce::Rectangle<float> (x0, y, std::max (3.0f, x1 - x0), h);

        juce::Colour c = colours::upcoming();
        if (finished)
            c = colours::forGrade (grade);
        else if (active && live.hasBlock && live.blockId == b.id && live.instant != va::Grade::None)
            c = colours::forGrade (live.instant);
        else if (active)
            c = colours::upcoming().brighter (0.3f);

        float alpha = 0.92f;
        if (carried) alpha = 0.45f;
        if (! focused) alpha = 0.18f;

        if (active && focused)
        {
            g.setColour (c.withAlpha (0.28f));
            g.fillRoundedRectangle (r.expanded (4.0f), 7.0f);
        }
        g.setColour (c.withAlpha (alpha));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (active && focused ? 0.9f : 0.18f));
        g.drawRoundedRectangle (r, 5.0f, active && focused ? 1.6f : 1.0f);

        if (r.getWidth() >= 36.0f && h >= 11.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (focused ? 0.85f : 0.3f));
            g.setFont (juce::FontOptions (std::min (12.0f, h - 2.0f)));
            g.drawText (noteName ((int) std::lround (midi)), r.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
        }
    }

    // phrase markers along the top edge
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (int p = 0; p < keeper.phraseCount(); ++p)
    {
        double a = 0, b = 0;
        if (! keeper.phraseRange (p, a, b)) continue;
        const float x = xOf (a);
        if (x < lane.getX() + labelWidth || x > lane.getRight() - 40.0f) continue;
        g.setColour (juce::Colour (0xff6c727f));
        g.drawText ("PHRASE " + juce::String (p + 1), (int) x + 2, (int) lane.getY() + 2, 80, 14, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0x33ffffff));
        g.drawVerticalLine ((int) x, lane.getY() + 16.0f, lane.getBottom());
    }
}

void GameView::drawVoice (juce::Graphics& g, juce::Rectangle<float> lane, double tPlay, float playheadX, float pxPerSec)
{
    const auto vs = proc.viewSettings();
    const auto cfg = proc.score().settings();
    const auto& tracePts = proc.trace().points();
    const auto& env = proc.envelope();
    const float rowH = lane.getHeight() / (float) (highMidi - lowMidi);
    const double gain = proc.waveGain();
    const float xStart = lane.getX() + labelWidth;
    auto tOf = [&] (float x) { return tPlay + (double) (x - playheadX) / (double) pxPerSec; };

    // playhead line
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.fillRect (playheadX - 1.0f, lane.getY(), 2.0f, lane.getHeight());

    size_t ti = proc.trace().lowerBound (tOf (xStart) - 0.02);
    bool haveMidi = false, headVoiced = false;
    double smoothMidi = 0.0;
    float headY = 0.0f, headHalf = 0.0f;
    juce::Path line;
    bool lineOpen = false;

    for (float x = xStart; x < playheadX; x += 1.0f)
    {
        const double ta = tOf (x), tb = tOf (x + 1.0f);
        float mn = 0.0f, mx = 0.0f;
        if (! env.range (ta, tb, mn, mx))
        {
            lineOpen = false;
            continue;
        }

        while (ti < tracePts.size() && tracePts[ti].timeSec < ta - 0.004)
            ++ti;
        bool voicedNow = false;
        if (ti < tracePts.size() && tracePts[ti].timeSec < tb + 0.012 && tracePts[ti].voiced)
        {
            const double m = (double) tracePts[ti].midi;
            smoothMidi = haveMidi ? smoothMidi + 0.5 * (m - smoothMidi) : m;
            haveMidi = true;
            voicedNow = true;
        }
        if (! haveMidi)
            continue;

        const float yc = juce::jlimit (lane.getY() + 6.0f, lane.getBottom() - 6.0f,
                                       (float) yOfMidi (smoothMidi + 0.5, lane));
        const float amp = std::pow (std::min (1.0f, std::max (std::abs (mn), std::abs (mx)) * (float) gain), 0.6f);
        const float half = std::max (voicedNow ? 1.2f : 0.6f, amp * rowH * 1.5f);

        // colour: by how close you are to the note you are inside
        juce::Colour col = colours::voice();
        const double refT = ta - vs.refOffsetSec;
        const int bi = blockIndexAtRef (refT);
        if (voicedNow && bi >= 0)
        {
            const double target = blocks[(size_t) bi].midi() + 12.0 * vs.octaveShift;
            const double err = smoothMidi - target;
            const int k = (int) std::lround (err / 12.0);
            const bool octaveOff = k != 0 && std::abs (err - 12.0 * k) <= 1.0;
            const double cents = std::abs (octaveOff ? 9999.0 : err * 100.0);
            col = cents <= cfg.perfectCents ? colours::perfect() : (cents <= cfg.goodCents ? colours::good() : colours::miss());
        }
        else if (! voicedNow)
            col = juce::Colour (0xff8a8f99);

        const float fade = 0.30f + 0.70f * (x - xStart) / std::max (1.0f, playheadX - xStart);
        g.setColour (col.withAlpha (fade * (voicedNow ? 0.95f : 0.45f)));
        g.fillRect (x, yc - half, 1.0f, half * 2.0f);

        if (voicedNow)
        {
            if (! lineOpen) { line.startNewSubPath (x, yc); lineOpen = true; }
            else line.lineTo (x, yc);
        }
        else
            lineOpen = false;

        headVoiced = voicedNow;
        headY = yc;
        headHalf = half;
    }

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.strokePath (line, juce::PathStrokeType (1.2f));

    // the impact at the playhead
    if (proc.isPlaying() && headVoiced)
    {
        const auto& live = proc.score().liveNote();
        const auto c = live.instant == va::Grade::None ? colours::voice() : colours::forGrade (live.instant);
        const float pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounterHiRes() * 0.012f);
        const float r = 7.0f + headHalf * 0.6f;
        g.setColour (c.withAlpha (0.22f + 0.2f * pulse));
        g.fillEllipse (playheadX - r * 1.9f, headY - r * 1.9f, r * 3.8f, r * 3.8f);
        g.setColour (c);
        g.fillEllipse (playheadX - r, headY - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::white);
        g.drawEllipse (playheadX - r, headY - r, r * 2.0f, r * 2.0f, 1.5f);
    }
}

void GameView::drawHud (juce::Graphics& g, juce::Rectangle<int> area, double tPlay)
{
    const auto& keeper = proc.score();
    g.setColour (colours::panel());
    g.fillRoundedRectangle (area.reduced (0, 2).toFloat(), 8.0f);
    auto box = area.reduced (14, 6);

    // score and accuracy
    auto left = box.removeFromLeft (250);
    g.setColour (colours::dim());
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SCORE", left.removeFromTop (14), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
    g.drawText (formatScore (keeper.score()), left.removeFromTop (38), juce::Justification::centredLeft);
    g.setColour (colours::text());
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (keeper.hasAccuracy() ? "Accuracy " + juce::String (keeper.accuracyPercent()) + "%   ("
                                           + juce::String (keeper.countedCount()) + (keeper.countedCount() == 1 ? " note graded)" : " notes graded)")
                                     : juce::String ("Accuracy --"),
                left, juce::Justification::centredLeft);

    // streak and multiplier
    auto mid = box.removeFromLeft (190);
    g.setColour (colours::dim());
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("COMBO", mid.removeFromTop (14), juce::Justification::centredLeft);
    const double mult = keeper.multiplier();
    g.setColour (mult >= 3.0 ? colours::perfect() : (mult > 1.0 ? colours::good() : juce::Colours::white));
    g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
    g.drawText ("x" + juce::String (mult, 1), mid.removeFromTop (38), juce::Justification::centredLeft);
    g.setColour (colours::text());
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String (keeper.streak()) + " in a row   (best " + juce::String (keeper.bestStreak()) + ")", mid, juce::Justification::centredLeft);

    // right side: where you are
    auto right = box.removeFromRight (250);
    int phrase = -1;
    for (int p = 0; p < keeper.phraseCount(); ++p)
    {
        double a = 0, b = 0;
        keeper.phraseRange (p, a, b);
        if (tPlay <= b + 0.5) { phrase = p; break; }
    }
    if (phrase < 0 && keeper.phraseCount() > 0) phrase = keeper.phraseCount() - 1;
    g.setColour (colours::dim());
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("PHRASE", right.removeFromTop (14), juce::Justification::centredRight);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText (phrase >= 0 ? juce::String (phrase + 1) + " / " + juce::String (keeper.phraseCount()) : juce::String ("--"),
                right.removeFromTop (30), juce::Justification::centredRight);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const auto cfg = keeper.settings();
    juce::String modeText = proc.ghostMode() ? "GHOST: notes hidden until sung" : "PLAY";
    if (cfg.focusPhrase >= 0 && cfg.focusPhrase < keeper.phraseCount())
        modeText += "   FOCUS " + juce::String (cfg.focusPhrase + 1);
    g.setColour (proc.ghostMode() || cfg.focusPhrase >= 0 ? colours::good() : colours::dim());
    g.drawText (modeText, right, juce::Justification::centredRight);

    // grade flash in the middle
    const auto age = (double) (juce::Time::getMillisecondCounter() - flashStartMs);
    if (flashGrade != va::Grade::None && age < 1400.0)
    {
        const float a = (float) juce::jlimit (0.0, 1.0, 1.0 - (age - 700.0) / 700.0);
        const float pop = 1.0f + 0.25f * (float) std::max (0.0, 1.0 - age / 160.0);
        g.setColour (colours::forGrade (flashGrade).withAlpha (a));
        g.setFont (juce::FontOptions (30.0f * pop, juce::Font::bold));
        auto text = juce::String (va::gradeName (flashGrade));
        if (flashPoints > 0) text += "  +" + juce::String (flashPoints);
        g.drawText (text, box, juce::Justification::centred);
    }
}

void GameView::drawReadout (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (colours::panel());
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    auto box = area.reduced (14, 8);
    const auto& live = proc.score().liveNote();
    const auto cfg = proc.score().settings();
    const bool active = proc.isPlaying() && live.hasBlock;

    g.setFont (juce::FontOptions (13.0f));
    g.setColour (colours::text());
    juce::String line = proc.isPlaying() ? "No reference note here" : "Press play in Cubase and sing";
    if (active)
    {
        line = "Target " + noteName ((int) std::lround (live.targetMidi));
        if (live.voiced)
            line += "     You " + noteName ((int) std::lround (live.sungMidi))
                    + (live.octave != 0 ? (live.octave > 0 ? "  (octave up)" : "  (octave down)") : juce::String());
        else
            line += "     no pitch";
    }
    g.drawText (line, box.removeFromTop (20), juce::Justification::centredLeft);

    // cents meter: centre is on pitch, left is flat, right is sharp
    auto meter = box.removeFromTop (26).toFloat().reduced (0.0f, 4.0f);
    const float range = 100.0f;
    auto xOfCents = [&] (double c) { return meter.getCentreX() + (float) juce::jlimit (-1.0, 1.0, c / range) * meter.getWidth() * 0.5f; };
    g.setColour (juce::Colour (0xff15171c));
    g.fillRoundedRectangle (meter, 4.0f);
    g.setColour (colours::good().withAlpha (0.28f));
    g.fillRect (juce::Rectangle<float> (xOfCents (-cfg.goodCents), meter.getY(), xOfCents (cfg.goodCents) - xOfCents (-cfg.goodCents), meter.getHeight()));
    g.setColour (colours::perfect().withAlpha (0.38f));
    g.fillRect (juce::Rectangle<float> (xOfCents (-cfg.perfectCents), meter.getY(), xOfCents (cfg.perfectCents) - xOfCents (-cfg.perfectCents), meter.getHeight()));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.fillRect (meter.getCentreX() - 0.5f, meter.getY(), 1.0f, meter.getHeight());
    if (active && live.voiced)
    {
        const float nx = xOfCents (live.cents);
        const auto c = live.instant == va::Grade::None ? colours::voice() : colours::forGrade (live.instant);
        g.setColour (c);
        g.fillRoundedRectangle (nx - 3.0f, meter.getY() - 2.0f, 6.0f, meter.getHeight() + 4.0f, 3.0f);
    }

    g.setColour (colours::dim());
    g.setFont (juce::FontOptions (11.0f));
    auto labels = box.removeFromTop (16);
    g.drawText ("flat", labels, juce::Justification::centredLeft);
    g.drawText ("sharp", labels, juce::Justification::centredRight);
    if (active && live.voiced)
    {
        const int c = (int) std::lround (std::abs (live.cents));
        g.setColour (colours::text());
        g.drawText (live.octave != 0 ? juce::String ("wrong octave") : (c < 8 ? juce::String ("on the note") : juce::String (c) + " cents " + (live.cents < 0 ? "flat" : "sharp")),
                    labels, juce::Justification::centred);
    }
}

void GameView::drawOverview (juce::Graphics& g, juce::Rectangle<int> area, double tPlay)
{
    overviewArea = area;
    g.setColour (colours::panel());
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    auto strip = area.reduced (12, 10).toFloat();
    strip.removeFromTop (12.0f);

    const auto vs = proc.viewSettings();
    const auto& keeper = proc.score();
    const auto cfg = keeper.settings();
    const double span = overviewEndSec - overviewStartSec;
    auto xOf = [&] (double refSec) { return strip.getX() + (float) ((refSec - overviewStartSec) / span) * strip.getWidth(); };
    const double lo = (double) lowMidi, hi = (double) highMidi;

    // phrases: faint columns, the focused one lit
    for (int p = 0; p < keeper.phraseCount(); ++p)
    {
        double a = 0, b = 0;
        if (! keeper.phraseRange (p, a, b)) continue;
        const float x0 = xOf (a - vs.refOffsetSec), x1 = xOf (b - vs.refOffsetSec);
        const bool focus = cfg.focusPhrase == p;
        g.setColour (focus ? juce::Colour (0x40ffffff) : (p % 2 ? juce::Colour (0x12ffffff) : juce::Colour (0x08ffffff)));
        g.fillRect (x0, strip.getY(), std::max (1.0f, x1 - x0), strip.getHeight());
        if (keeper.phraseCount() <= 30 && x1 - x0 > 14.0f)
        {
            g.setColour (colours::dim());
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (p + 1), (int) x0, (int) strip.getY() - 12, (int) (x1 - x0), 11, juce::Justification::centred);
        }
    }

    for (const auto& b : blocks)
    {
        const auto grade = keeper.gradeOf (b.id);
        const float y = strip.getBottom() - (float) ((b.midi() + 12.0 * vs.octaveShift - lo) / (hi - lo)) * strip.getHeight();
        const float x0 = xOf (b.startSec()), x1 = xOf (b.endSec());
        g.setColour (colours::forGrade (grade).withAlpha (keeper.isCarried (b.id) ? 0.45f : 0.95f));
        g.fillRect (x0, y - 1.5f, std::max (1.5f, x1 - x0), 3.0f);
    }

    const float px = xOf (tPlay - vs.refOffsetSec);
    if (px >= strip.getX() && px <= strip.getRight())
    {
        g.setColour (juce::Colours::white);
        g.fillRect (px - 1.0f, strip.getY() - 4.0f, 2.0f, strip.getHeight() + 8.0f);
    }

    g.setColour (colours::dim());
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("Click a phrase to focus it. Click again for all.   Song " + formatTime (overviewEndSec + vs.refOffsetSec),
                area.reduced (12, 0).removeFromBottom (14), juce::Justification::centredLeft);
}
} // namespace pa
