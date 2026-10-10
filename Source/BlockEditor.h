#pragma once

#include "ReferenceModel.h"
#include "PerformanceSource.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

namespace va
{
/** The piano roll: the reference as note blocks you can select, split, merge,
    delete and pitch-shift before locking, and, while the host plays, the live
    pitch trace against them. Blocks are coloured by the Matcher (blue until
    reached, then green / amber / red). The roll scrolls past a stationary
    playhead when Follow is on; wheel or drag scrolls freely and turns Follow
    off. Ctrl+wheel zooms. Message thread only. */
class BlockEditor final : public juce::Component,
                          private juce::ChangeListener
{
public:
    BlockEditor (ReferenceModel&, PerformanceSource&);
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
    void fitAll();                         // whole piece in view (turns Follow off)
    void setFollow (bool);
    bool isFollowing() const               { return following; }
    /** Called by the editor's frame timer: keeps the view on the playhead when following. */
    void tick();
    std::function<void()> onFollowChanged;

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
    PerformanceSource& perf;
    BlockList snapshot;
    std::set<int> selection;
    double viewStartSec = 0.0, pxPerSec = 40.0;
    int64_t cursorUs = -1;
    bool dragPanning = false;
    double dragStartViewSec = 0.0;
    int lowMidi = 48, highMidi = 84;
    bool following = true, wasPlaying = false;
    int lastOctave = 0;
    static constexpr double followFraction = 0.3;     // playhead sits 30% in from the left

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockEditor)
};
} // namespace va
