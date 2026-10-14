#pragma once

#include "AssistProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace pa
{
/** The performance screen: the reference as note blocks scrolling past a fixed playhead,
    and your voice drawn as a wave that flows into them. The wave rides at the height of the
    note you are singing and gets thicker the louder you are; where it overlaps a block it is
    coloured by how close you are (green Perfect, amber Good, red off). Message thread only. */
class GameView final : public juce::Component,
                       private juce::ChangeListener
{
public:
    explicit GameView (AssistProcessor&);
    ~GameView() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();                        // called by the editor's frame timer

    static juce::String noteName (int midi);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshBlocks();
    double yOfMidi (double midi, juce::Rectangle<float> lane) const;
    int blockIndexAtRef (double refSec) const;
    void drawLane (juce::Graphics&, juce::Rectangle<float> lane, double tPlay);
    void drawBlocks (juce::Graphics&, juce::Rectangle<float> lane, double tPlay, float playheadX, float pxPerSec);
    void drawVoice (juce::Graphics&, juce::Rectangle<float> lane, double tPlay, float playheadX, float pxPerSec);
    void drawHud (juce::Graphics&, juce::Rectangle<int> area, double tPlay);
    void drawReadout (juce::Graphics&, juce::Rectangle<int> area);
    void drawOverview (juce::Graphics&, juce::Rectangle<int> area, double tPlay);
    void drawEmpty (juce::Graphics&);

    AssistProcessor& proc;
    std::vector<va::NoteBlock> blocks;
    int lowMidi = 48, highMidi = 84;
    juce::Rectangle<int> overviewArea;
    double overviewStartSec = 0.0, overviewEndSec = 1.0;

    // "PERFECT +120" flash
    int seenGradeRevision = 0;
    va::Grade flashGrade = va::Grade::None;
    int flashPoints = 0;
    juce::uint32 flashStartMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GameView)
};

/** Colours shared by the screens. */
namespace colours
{
inline juce::Colour perfect() { return juce::Colour (0xff3ddc6e); }
inline juce::Colour good()    { return juce::Colour (0xffffb300); }
inline juce::Colour miss()    { return juce::Colour (0xfff44336); }
inline juce::Colour upcoming(){ return juce::Colour (0xff3f6fd8); }
inline juce::Colour voice()   { return juce::Colour (0xff4fc3f7); }
inline juce::Colour panel()   { return juce::Colour (0xff1f232b); }
inline juce::Colour text()    { return juce::Colour (0xffd5d8df); }
inline juce::Colour dim()     { return juce::Colour (0xff8a8f99); }

inline juce::Colour forGrade (va::Grade g)
{
    switch (g)
    {
        case va::Grade::Perfect: return perfect();
        case va::Grade::Good:    return good();
        case va::Grade::Miss:    return miss();
        default:                 return upcoming();
    }
}
} // namespace colours
} // namespace pa
