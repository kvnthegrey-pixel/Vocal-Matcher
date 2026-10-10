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

BlockEditor::BlockEditor (ReferenceModel& m) : model (m)
{
    model.addChangeListener (this);
    snapshot = model.blocksCopy();
    fitAll();
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
        fitAll();
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void BlockEditor::fitAll()
{
    if (snapshot.empty()) { lowMidi = 48; highMidi = 84; viewStartSec = 0.0; pxPerSec = 40.0; return; }
    int lo = 127, hi = 0;
    double firstSec = 1.0e9, lastSec = 0.0;
    for (auto& b : snapshot.blocks())
    {
        lo = std::min (lo, (int) std::floor (b.midi())); hi = std::max (hi, (int) std::ceil (b.midi()));
        firstSec = std::min (firstSec, b.startSec()); lastSec = std::max (lastSec, b.endSec());
    }
    lowMidi = std::max (0, lo - 4);
    highMidi = std::min (127, hi + 4);
    while (highMidi - lowMidi < 24) { if (lowMidi > 0) --lowMidi; if (highMidi < 127) ++highMidi; }
    viewStartSec = std::max (0.0, firstSec - 0.5);
    const double span = std::max (2.0, lastSec - viewStartSec + 0.5);
    pxPerSec = juce::jlimit (4.0, 400.0, (double) (getWidth() - labelWidth) / span);
    repaint();
}

void BlockEditor::keepInRange()
{
    viewStartSec = std::max (-1.0, viewStartSec);
    pxPerSec = juce::jlimit (4.0, 600.0, pxPerSec);
}

void BlockEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101216));
    const auto area = getLocalBounds();
    const float rows = (float) (highMidi - lowMidi + 1);
    const float rowH = (float) area.getHeight() / rows;
    auto yOf = [&] (double midi) { return (float) area.getHeight() - ((float) midi - (float) lowMidi + 1.0f) * rowH; };

    for (int m = lowMidi; m <= highMidi; ++m)
    {
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
    for (auto& b : snapshot.blocks())
    {
        const float x0 = secToX (b.startSec()), x1 = secToX (b.endSec());
        if (x1 < (float) labelWidth || x0 > (float) area.getWidth()) continue;
        const float y = yOf (b.midi());
        juce::Rectangle<float> r (x0, y + 1.0f, std::max (2.0f, x1 - x0), rowH - 2.0f);
        const bool sel = selection.count (b.id) > 0;
        g.setColour (sel ? juce::Colour (0xffffd166) : juce::Colour (0xff4a90e2));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (sel ? juce::Colours::white : juce::Colour (0xff9cc6f5));
        g.drawRoundedRectangle (r, 3.0f, sel ? 2.0f : 1.0f);
        if (r.getWidth() > 34.0f && rowH > 11.0f)
        {
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (juce::jmin (12.0f, rowH - 3.0f)));
            g.drawText (noteLabel ((int) std::lround (b.midi())), r.reduced (3.0f, 0.0f), juce::Justification::centredLeft);
        }
    }
    if (cursorUs >= 0)
    {
        const float x = secToX ((double) cursorUs * 1.0e-6);
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
    const int64_t us = (int64_t) std::llround (xToSec ((float) e.x) * 1.0e6);
    const float rowH = (float) getHeight() / (float) (highMidi - lowMidi + 1);
    const double midiAtMouse = (double) lowMidi + ((double) getHeight() - (double) e.y) / (double) rowH - 1.0;

    int hit = -1;
    for (auto& b : snapshot.blocks())
        if (us >= b.startUs && us < b.endUs && std::abs (b.midi() - midiAtMouse) < 0.75) { hit = b.id; break; }

    if (hit >= 0)
    {
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
    viewStartSec = dragStartViewSec - (double) e.getDistanceFromDragStartX() / pxPerSec;
    keepInRange();
    repaint();
}

void BlockEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const double before = xToSec ((float) e.x);
        pxPerSec *= std::pow (1.25, (double) w.deltaY * 4.0);
        keepInRange();
        viewStartSec = before - (double) (e.x - labelWidth) / pxPerSec;
    }
    else
        viewStartSec -= (double) (w.deltaX != 0.0f ? w.deltaX : w.deltaY) * 600.0 / pxPerSec;
    keepInRange();
    repaint();
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
