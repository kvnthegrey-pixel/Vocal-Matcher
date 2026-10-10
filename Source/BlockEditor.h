#pragma once

#include "ReferenceModel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

namespace va
{
/** Phase 3 block editor: the reference as note blocks you can select, split,
    merge, delete and pitch-shift before locking. It is a static overview (the
    scrolling piano roll with the moving playhead is Phase 5). Wheel pans,
    Ctrl+wheel zooms, dragging empty space pans. Message thread only. */
class BlockEditor final : public juce::Component,
                          private juce::ChangeListener
{
public:
    explicit BlockEditor (ReferenceModel&);
    ~BlockEditor() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Actions used by the toolbar.
    void splitAtCursor();
    void mergeSelected();
    void deleteSelected();
    void shiftSelected (int semitones);
    void fitAll();

    int  selectedCount() const { return (int) selection.size(); }
    bool hasCursor() const     { return cursorUs >= 0; }
    std::function<void()> onSelectionChanged;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    std::vector<int> selectedIds() const;
    double xToSec (float x) const;
    float secToX (double sec) const;
    void keepInRange();
    void pruneSelection();

    ReferenceModel& model;
    BlockList snapshot;
    std::set<int> selection;
    double viewStartSec = 0.0, pxPerSec = 40.0;
    int64_t cursorUs = -1;
    bool dragPanning = false;
    double dragStartViewSec = 0.0;
    int lowMidi = 48, highMidi = 84;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockEditor)
};
} // namespace va
