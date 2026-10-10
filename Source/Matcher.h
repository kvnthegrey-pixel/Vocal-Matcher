#pragma once

#include "NoteBlocks.h"
#include <vector>

namespace va
{
enum class BlockState { Blue, Green, Amber, Red };

struct MatchSettings
{
    double greenCents  = 25.0;      // slider 5..50
    double amberCents  = 100.0;     // slider 50..200
    double a4          = 440.0;     // 430..450
    int    octaveShift = 0;         // -2..+2, moves the reference only
    double refOffsetSec = 0.0;      // where the reference starts in the project

    bool operator== (const MatchSettings& o) const
    {
        return greenCents == o.greenCents && amberCents == o.amberCents && a4 == o.a4
            && octaveShift == o.octaveShift && refOffsetSec == o.refOffsetSec;
    }
};

/** What the singer is doing right now against the block under the playhead. */
struct LiveMatch
{
    bool   hasBlock = false;        // a block is under the playhead
    bool   voiced   = false;        // a pitch was detected
    double cents    = 0.0;          // + sharp, - flat, against the block's centre pitch
    BlockState instant = BlockState::Blue;   // Green, Amber or Red for this instant
};

/** The strict colour state machine (strict document, section 3).

    Per block, measured only while the playhead is over it:
    - Blue until the playhead reaches the block.
    - Each analysis frame counts as Green (within the green threshold of the
      block's centre pitch), Amber (within the amber threshold), or Red (outside
      it, a wrong note, or no pitch detected).
    - The block earns Green once Green time is a majority of the block's whole
      duration, and Amber once time inside the amber threshold is. Earned
      colours are never lost.
    - While the block is active and nothing is earned yet, it shows the colour
      of the current moment. Once the playhead has passed it, it freezes at what
      it earned, or Red if nothing.
    Accuracy is Green time over time spent under any block, as a whole percent.
    Everything is in time, not frame counts. */
class Matcher
{
public:
    void setBlocks (const std::vector<NoteBlock>& blocks);
    void setSettings (const MatchSettings&);
    const MatchSettings& settings() const { return cfg; }

    /** A new pass starts at this project time (play, loop, punch-in, locate):
        blocks that end after it forget their results; earlier ones keep theirs. */
    void resetFrom (double projectSec);
    void resetAll();

    /** One analysis frame: project time of its centre, how long it stands for, and the pitch. */
    void onPoint (double projectSec, double frameSec, bool voiced, double hz);

    /** Freezes blocks the playhead has gone past. */
    void onPlayhead (double projectSec);

    BlockState stateOf (int blockId) const;
    int accuracyPercent() const;            // 0..100; 0 when nothing has been under a block
    bool hasAccuracy() const                { return totalActive > 0.0; }
    const LiveMatch& live() const           { return liveMatch; }

private:
    struct Rec
    {
        int id = 0;
        double startSec = 0, endSec = 0;     // reference time
        double pitch = 60.0;                 // nominal MIDI note, octave shift not applied
        double activeSec = 0, greenSec = 0, amberSec = 0;
        bool reached = false, passed = false;
        BlockState earned = BlockState::Blue;    // Blue = nothing earned yet
        BlockState instant = BlockState::Blue;
    };

    void recompute();
    double projectStart (const Rec& r) const { return r.startSec + cfg.refOffsetSec; }
    double projectEnd (const Rec& r) const   { return r.endSec + cfg.refOffsetSec; }

    std::vector<Rec> recs;
    MatchSettings cfg;
    LiveMatch liveMatch;
    double totalActive = 0.0, totalGreen = 0.0;
};
} // namespace va
