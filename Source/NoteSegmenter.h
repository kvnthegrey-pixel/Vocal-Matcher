#pragma once

#include "NoteBlocks.h"

namespace va
{
/** The strict document's post-processing rules. Applied automatically after
    analysis; the user can then edit the result. */
struct SegmenterRules
{
    double minNoteMs         = 80.0;   // shorter notes are dropped
    double maxDeviationCents = 40.0;   // a note may not wander further than this from its centre
    double maxGapMs          = 60.0;   // an unvoiced gap longer than this ends the note
    double mergeCents        = 30.0;   // adjacent notes this close in pitch ...
    double mergeGapMs        = 40.0;   // ... and this close in time are merged
    bool   snapToSemitone    = true;   // block pitch is the nearest whole semitone
    int    splitPersistFrames = 3;     // frames a new pitch must persist to force a split
};

/** One pitch value per analysis frame. midi <= 0 means unvoiced. */
struct FrameTrack
{
    double hopSec = 0.01;
    double firstCentreSec = 0.0;       // time of the middle of frame 0
    std::vector<float> midi;           // fractional MIDI note number (A4 = 69 at 440 Hz)
    std::vector<float> clarity;        // 0..1, how clean the frame was
};

/** A note before its pitch is rounded. Exposed so each rule can be tested alone. */
struct RawNote
{
    int64_t startUs = 0, endUs = 0;
    double  midi = 0.0;                // exact centre
};

std::vector<RawNote>   splitAndGate (const FrameTrack&, const SegmenterRules&);   // gap and deviation rules
std::vector<RawNote>   mergeAdjacent (std::vector<RawNote>, const SegmenterRules&);
std::vector<RawNote>   dropShort (std::vector<RawNote>, const SegmenterRules&);
std::vector<NoteBlock> toBlocks (const std::vector<RawNote>&, const SegmenterRules&);

/** Share of voiced frames that ended up inside a final block (within half a
    semitone of the block's pitch). Frames lost to fragments, glitches and
    doubled-voice wobble count against it. Used by the quality indicator. */
double noteCoverage (const FrameTrack&, const std::vector<NoteBlock>&);

/** The whole chain: gap and deviation rules, merge, minimum length, pitch rounding. */
std::vector<NoteBlock> segment (const FrameTrack&, const SegmenterRules&);
} // namespace va
