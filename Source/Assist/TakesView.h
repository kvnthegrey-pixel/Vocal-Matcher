#pragma once

#include "AssistProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace pa
{
/** Every pass over the song, kept as a take: its score, a heatmap of where each note landed
    (flat, centred or sharp), a plain-language read on your tendencies, and a comp plan that
    picks the best take for each phrase. Message thread only. */
class TakesView final : public juce::Component
{
public:
    explicit TakesView (AssistProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    /** Called by the editor's frame timer: picks up new takes. */
    void tick();

private:
    void select (int takeNumber);
    void drawList (juce::Graphics&, juce::Rectangle<int>);
    void drawHeatmap (juce::Graphics&, juce::Rectangle<int>);
    void drawPhrases (juce::Graphics&, juce::Rectangle<int>);
    juce::Rectangle<int> cellRect (juce::Rectangle<int> area, int index) const;

    AssistProcessor& proc;
    int selected = 0;               // take number, 0 = none
    int seenRevision = -1;
    int hoverBlockIndex = -1;
    std::vector<va::NoteBlock> blocks;

    juce::TextButton copyButton { "Copy comp plan" }, clearButton { "Clear takes" };
    juce::Rectangle<int> listArea, heatArea, textArea, phraseArea;
    std::vector<std::pair<juce::Rectangle<int>, int>> rowHits;    // list rows -> take numbers

    static constexpr int rowHeight = 28;
    static constexpr int cellW = 16, cellH = 22, cellGap = 3;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TakesView)
};
} // namespace pa
