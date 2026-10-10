#include "BlockEditor.h"

namespace va
{
namespace
{
constexpr int labelWidth = 44;

bool isBlackKey (int midi) { const int n = ((midi % 12) + 12) % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }

juce::String noteLabel (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}
}

BlockEditor::BlockEditor (ReferenceModel& m, PerformanceSource& p) : model (m), perf (p)
{
    model.addChangeListener (this);
    snapshot = model.blocksCopy();
    lastOctave = perf.viewSettings().octaveShift;
    fitAll();
    pxPerSec = 100.0;          // performance zoom: about ten seconds across the window
    following = true;
}

BlockEditor::~BlockEditor() { model.removeChangeListener (this); }

double BlockEditor::xToSec (float x) const { return viewStartSec + (double) (x - labelWidth) / pxPerSec; }
float  BlockEditor::secToX (double sec) const { return (float) labelWidth + (float) ((sec - viewStartSec) * pxPerSec); }

std::vector<int> BlockEditor::selectedIds() const { return { selection.begin(), selection.end() }; }

void BlockEditor::pruneSelection()
{
    for (auto it = selection.begin(); it != selection.end();)
        it = snapshot.find (*it) ? std::next (it) : selection.erase (it);
}

void BlockEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const bool hadBlocks = ! snapshot.empty();
    snapshot = model.blocksCopy();
    pruneSelection();
    if (! hadBlocks && ! snapshot.empty())
    {
        fitAll();
        pxPerSec = 100.0;
        setFollow (true);
    }
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void BlockEditor::fitAll()
{
    const auto vs = perf.viewSettings();
    const int shift = 12 * vs.octaveShift;
    if (snapshot.empty()) { lowMidi = 48; highMidi = 84; viewStartSec = 0.0; pxPerSec = 40.0; return; }
    int lo = 127, hi = 0;
    double firstSec = 1.0e9, lastSec = 0.0;
    for (auto& b : snapshot.blocks())
    {
        lo = std::min (lo, (int) std::floor (b.midi()) + shift); hi = std::max (hi, (int) std::ceil (b.midi()) + shift);
        firstSec = std::min (firstSec, b.startSec() + vs.refOffsetSec); lastSec = std::max (lastSec, b.endSec() + vs.refOffsetSec);
    }
    lowMidi = std::max (0, lo - 4);
    highMidi = std::min (127, hi + 4);
    while (highMidi - lowMidi < 24) { if (lowMidi > 0) --lowMidi; if (highMidi < 127) ++highMidi; }
    viewStartSec = std::max (0.0, firstSec - 0.5);
    const double span = std::max (2.0, lastSec - viewStartSec + 0.5);
    pxPerSec = juce::jlimit (4.0, 400.0, (double) (getWidth() - labelWidth) / span);
    lastOctave = vs.octaveShift;
    following = false;
    if (onFollowChanged) onFollowChanged();
    repaint();
}

void BlockEditor::setFollow (bool on)
{
    if (following == on) return;
    following = on;
    if (on) tick();
    if (onFollowChanged) onFollowChanged();
    repaint();
}

void BlockEditor::tick()
{
    const auto vs = perf.viewSettings();
    if (vs.octaveShift != lastOctave)
    {
        const int d = 12 * (vs.octaveShift - lastOctave);
        lowMidi = juce::jlimit (0, 100, lowMidi + d);
        highMidi = juce::jlimit (lowMidi + 24, 127, highMidi + d);
        lastOctave = vs.octaveShift;
    }

    const bool playing = perf.isPlaying();
    if (playing && ! wasPlaying)
        setFollow (true);
    wasPlaying = playing;

    if (following)
        viewStartSec = perf.playheadSeconds() - (double) (getWidth() - labelWidth) * followFraction / pxPerSec;
    repaint();
}

void BlockEditor::keepInRange()
{
    viewStartSec = std::max (-60.0, viewStartSec);
    pxPerSec = juce::jlimit (4.0, 600.0, pxPerSec);
}

void BlockEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101216));
    const auto area = getLocalBounds();
    const float rows = (float) (highMidi - lowMidi + 1);
    const float rowH = (float) area.getHeight() / rows;
    auto yOf = [&] (double midi) { return (float) area.getHeight() - ((float) midi - (float) lowMidi + 1.0f) * rowH; };

    const auto vs = perf.viewSettings();
    const int octShift = 12 * vs.octaveShift;
    const double off = vs.refOffsetSec;

    for (int m = lowMidi; m <= highMidi; ++m)
    {
        if (vs.scale != 0)
            g.setColour (inScale (m % 12, vs.keyRoot, vs.scale) ? juce::Colour (0xff2a2f3a) : juce::Colour (0xff121419));
        else
            g.setColour (isBlackKey (m) ? juce::Colour (0xff15171c) : juce::Colour (0xff1c1f26));
        g.fillRect ((float) labelWidth, yOf (m), (float) area.getWidth() - labelWidth, rowH);
        if (m % 12 == 0)
        {
            g.setColour (juce::Colour (0xff2c303a));
            g.drawHorizontalLine ((int) (yOf (m) + rowH), (float) labelWidth, (float) area.getWidth());
        }
    }

    // Time grid every second (or wider when zoomed out).
    const double step = pxPerSec >= 40 ? 1.0 : (pxPerSec >= 12 ? 5.0 : 15.0);
    g.setFont (juce::FontOptions (11.0f));
    for (double t = std::ceil (viewStartSec / step) * step; secToX (t) < (float) area.getWidth(); t += step)
    {
        const float x = secToX (t);
        g.setColour (juce::Colour (0xff262a33));
        g.drawVerticalLine ((int) x, 0.0f, (float) area.getHeight());
        g.setColour (juce::Colour (0xff6c727f));
        g.drawText (juce::String ((int) t / 60) + ":" + juce::String ((int) t % 60).paddedLeft ('0', 2),
                    (int) x + 3, area.getHeight() - 14, 40, 12, juce::Justification::left);
    }

    // Blocks.
    g.saveState();
    g.reduceClipRegion (labelWidth, 0, area.getWidth() - labelWidth, area.getHeight());
    const auto& matcher = perf.matcher();
    for (auto& b : snapshot.blocks())
    {
        const float x0 = secToX (b.startSec() + off), x1 = secToX (b.endSec() + off);
        if (x1 < (float) labelWidth || x0 > (float) area.getWidth()) continue;
        const float y = yOf (b.midi() + octShift);
        juce::Rectangle<float> r (x0, y + 1.0f, std::max (2.0f, x1 - x0), rowH - 2.0f);
        const bool sel = selection.count (b.id) > 0;
        juce::Colour fill, edge;
        switch (matcher.stateOf (b.id))
        {
            case BlockState::Green: fill = juce::Colour (0xff4caf50); edge = juce::Colour (0xff9be29f); break;
            case BlockState::Amber: fill = juce::Colour (0xffffb300); edge = juce::Colour (0xffffd980); break;
            case BlockState::Red:   fill = juce::Colour (0xfff44336); edge = juce::Colour (0xffff9a92); break;
            default:                fill = juce::Colour (0xff4a90e2); edge = juce::Colour (0xff9cc6f5); break;
        }
        g.setColour (fill);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (sel ? juce::Colours::white : edge);
        g.drawRoundedRectangle (r, 3.0f, sel ? 3.0f : 1.0f);
        if (r.getWidth() > 34.0f && rowH > 11.0f)
        {
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (juce::jmin (12.0f, rowH - 3.0f)));
            g.drawText (noteLabel ((int) std::lround (b.midi()) + octShift), r.reduced (3.0f, 0.0f), juce::Justification::centredLeft);
        }
    }
    // The live pitch line: thin, brighter where the detector is more confident,
    // broken wherever there is no pitch.
    {
        const auto& pts = perf.trace().points();
        const double t0 = xToSec ((float) labelWidth) - 0.1, t1 = xToSec ((float) area.getWidth()) + 0.1;
        size_t i = perf.trace().lowerBound (t0);
        const size_t stride = (size_t) std::max (1.0, std::floor (1.0 / pxPerSec / 0.0053));     // about one point per pixel
        const float half = rowH * 0.5f;
        bool havePrev = false;
        float px = 0, py = 0;
        double pt = 0;
        for (; i < pts.size() && pts[i].timeSec <= t1; i += stride)
        {
            const auto& p = pts[i];
            if (! p.voiced) { havePrev = false; continue; }
            const float x = secToX (p.timeSec);
            const float y = yOf ((double) p.midi) + half;
            if (havePrev && p.timeSec - pt < 0.03)
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawLine (px, py, x, y, 3.0f);
                g.setColour (juce::Colours::white.withAlpha (0.2f + 0.5f * p.confidence));
                g.drawLine (px, py, x, y, 1.5f);
            }
            px = x; py = y; pt = p.timeSec; havePrev = true;
        }
    }

    // The notes you sang, cut from the live line, at their true pitch (not snapped), so
    // sitting above or below a reference block shows high or low at a glance.
    {
        const auto sung = segmentSung (perf.trace(), xToSec ((float) labelWidth) - 2.0, xToSec ((float) area.getWidth()) + 0.5);
        for (auto& n : sung)
        {
            const float x0 = secToX (n.startSec), x1 = secToX (n.endSec);
            const float cy = yOf (n.midi) + rowH * 0.5f;
            const float hh = std::max (4.0f, rowH * 0.32f);
            juce::Rectangle<float> r (x0, cy - hh, std::max (3.0f, x1 - x0), hh * 2.0f);
            g.setColour (juce::Colour (0xff00e5ff).withAlpha (0.35f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (juce::Colour (0xffe0fbff));
            g.drawRoundedRectangle (r, 3.0f, 1.5f);
        }
    }

    // The stationary playhead.
    {
        const float x = secToX (perf.playheadSeconds());
        if (x >= (float) labelWidth && x <= (float) area.getWidth())
        {
            g.setColour (juce::Colour (0xffff5252));
            g.drawVerticalLine ((int) x, 0.0f, (float) area.getHeight());
            g.fillRect (x - 1.0f, 0.0f, 3.0f, (float) area.getHeight());
        }
    }

    if (cursorUs >= 0)
    {
        const float x = secToX ((double) cursorUs * 1.0e-6 + off);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawVerticalLine ((int) x, 0.0f, (float) area.getHeight());
    }
    g.restoreState();

    // Pitch labels.
    g.setColour (juce::Colour (0xff15171c));
    g.fillRect (0, 0, labelWidth, area.getHeight());
    g.setColour (juce::Colour (0xff8a8f99));
    g.setFont (juce::FontOptions (11.0f));
    for (int m = lowMidi; m <= highMidi; ++m)
        if (rowH >= 11.0f || m % 12 == 0)
            g.drawText (noteLabel (m), 2, (int) yOf (m), labelWidth - 6, (int) rowH + 1, juce::Justification::centredRight);

    if (snapshot.isLocked())
    {
        g.setColour (juce::Colour (0xffff8a65));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("LOCKED", area.getWidth() - 86, 6, 80, 16, juce::Justification::centredRight);
    }
}

void BlockEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < labelWidth) return;
    const auto vs = perf.viewSettings();
    const int64_t us = (int64_t) std::llround ((xToSec ((float) e.x) - vs.refOffsetSec) * 1.0e6);     // reference time
    const float rowH = (float) getHeight() / (float) (highMidi - lowMidi + 1);
    const double midiAtMouse = (double) lowMidi + ((double) getHeight() - (double) e.y) / (double) rowH - 1.0;

    // A sung note under the mouse: compare it with the reference.
    {
        const double projSec = xToSec ((float) e.x);
        for (auto& n : segmentSung (perf.trace(), projSec - 6.0, projSec + 6.0))
            if (projSec >= n.startSec && projSec <= n.endSec && std::abs (n.midi - midiAtMouse) < 0.7)
            {
                compareAt (us, projSec, n.midi, -1);
                return;
            }
    }

    int hit = -1;
    for (auto& b : snapshot.blocks())
        if (us >= b.startUs && us < b.endUs && std::abs (b.midi() + 12 * vs.octaveShift - midiAtMouse) < 0.75) { hit = b.id; break; }

    if (hit >= 0)
    {
        compareAt (us, xToSec ((float) e.x), midiAtMouse, hit);
        cursorUs = us;
        if (e.mods.isShiftDown())
        {
            if (selection.count (hit)) selection.erase (hit); else selection.insert (hit);
        }
        else
        {
            selection.clear();
            selection.insert (hit);
        }
        dragPanning = false;
    }
    else
    {
        if (! e.mods.isShiftDown()) selection.clear();
        cursorUs = us;
        dragPanning = true;
        dragStartViewSec = viewStartSec;
    }
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void BlockEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragPanning) return;
    if (std::abs (e.getDistanceFromDragStartX()) > 3) setFollow (false);
    viewStartSec = dragStartViewSec - (double) e.getDistanceFromDragStartX() / pxPerSec;
    keepInRange();
    repaint();
}

void BlockEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const double before = xToSec ((float) e.x);
        const double anchorPx = following ? (double) (getWidth() - labelWidth) * followFraction : (double) (e.x - labelWidth);
        const double anchorSec = following ? perf.playheadSeconds() : before;
        pxPerSec *= std::pow (1.25, (double) w.deltaY * 4.0);
        keepInRange();
        viewStartSec = anchorSec - anchorPx / pxPerSec;
    }
    else
    {
        setFollow (false);
        viewStartSec -= (double) (w.deltaX != 0.0f ? w.deltaX : w.deltaY) * 600.0 / pxPerSec;
    }
    keepInRange();
    repaint();
}

namespace
{
double hzOf (double midi, double a4) { return a4 * std::pow (2.0, (midi - 69.0) / 12.0); }
juce::String nameOf (double midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = (int) std::lround (midi);
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}
}

void BlockEditor::compareAt (int64_t, double projectSec, double midiAtMouse, int hitBlockId)
{
    if (! onCompare) return;
    const auto vs = perf.viewSettings();
    const double shift = 12.0 * vs.octaveShift;

    // Work out the pair: a clicked block is paired with the sung note that overlaps it most,
    // a clicked sung note with the block that overlaps it most.
    const NoteBlock* block = nullptr;
    const SungNote* note = nullptr;
    std::vector<SungNote> sung;

    if (hitBlockId >= 0)
    {
        block = snapshot.find (hitBlockId);
        if (block == nullptr) return;
        const double s = block->startSec() + vs.refOffsetSec, en = block->endSec() + vs.refOffsetSec;
        sung = segmentSung (perf.trace(), s - 0.5, en + 0.5);
        double best = 0.0;
        for (auto& n : sung)
        {
            const double ov = std::min (en, n.endSec) - std::max (s, n.startSec);
            if (ov > best) { best = ov; note = &n; }
        }
    }
    else
    {
        sung = segmentSung (perf.trace(), projectSec - 6.0, projectSec + 6.0);
        for (auto& n : sung)
            if (projectSec >= n.startSec && projectSec <= n.endSec && std::abs (n.midi - midiAtMouse) < 0.7) { note = &n; break; }
        if (note == nullptr) return;
        double best = 0.0;
        for (auto& b : snapshot.blocks())
        {
            const double ov = std::min (b.endSec() + vs.refOffsetSec, note->endSec) - std::max (b.startSec() + vs.refOffsetSec, note->startSec);
            if (ov > best) { best = ov; block = &b; }
        }
    }

    const double refMidi = block != nullptr ? block->midi() + shift : 0.0;
    const double len = block != nullptr ? std::min (0.9, std::max (0.35, block->durationUs() * 1.0e-6))
                                        : std::min (0.9, std::max (0.35, note->endSec - note->startSec));
    juce::String text;
    if (block != nullptr && note != nullptr)
    {
        const int c = (int) std::lround ((note->midi - refMidi) * 100.0);
        text = "Reference " + nameOf (refMidi) + " then you " + nameOf (note->midi) + ": "
               + (std::abs (c) < 1 ? juce::String ("spot on") : juce::String (std::abs (c)) + " cents " + (c < 0 ? "LOW" : "HIGH"));
    }
    else if (block != nullptr)
        text = "Reference " + nameOf (refMidi) + " (nothing sung here)";
    else
        text = "You sang " + nameOf (note->midi) + " (no reference block under it)";

    onCompare (block != nullptr ? hzOf (refMidi, vs.a4) : 0.0, note != nullptr ? hzOf (note->midi, vs.a4) : 0.0, len, text);
}

void BlockEditor::splitAtCursor()
{
    if (selection.size() != 1 || cursorUs < 0) return;
    model.split (*selection.begin(), cursorUs);
}

void BlockEditor::mergeSelected()  { model.merge (selectedIds()); }

void BlockEditor::deleteSelected()
{
    if (model.remove (selectedIds())) selection.clear();
}

void BlockEditor::shiftSelected (int semitones) { model.shift (selectedIds(), semitones); }
} // namespace va
