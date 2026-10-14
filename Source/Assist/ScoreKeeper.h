#pragma once

// Performance Assist: the scoring engine. Plain C++ (no JUCE) so it can be unit tested anywhere.
//
// It listens to the sung pitch frame by frame, grades it against the reference
// blocks, and keeps score: per block (Perfect / Good / Miss), a streak with a
// multiplier, accuracy, phrases, and the numbers behind the post-take heatmap.

#include "../NoteBlocks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace va
{
enum class Grade { None = 0, Miss = 1, Good = 2, Perfect = 3 };

inline const char* gradeName (Grade g)
{
    switch (g)
    {
        case Grade::Perfect: return "PERFECT";
        case Grade::Good:    return "GOOD";
        case Grade::Miss:    return "MISS";
        default:             return "";
    }
}

inline double midiFromHz (double hz, double a4) { return 69.0 + 12.0 * std::log2 (hz / a4); }

struct ScoreSettings
{
    double perfectCents = 25.0;     // within this of the target = Perfect
    double goodCents    = 50.0;     // within this = Good (half a semitone: still the right note)
    double graceSec     = 0.10;     // start of every note ignored (scoops up to pitch)
    double smoothSec    = 0.18;     // averaging window, about one vibrato cycle
    double bridgeSec    = 0.08;     // unpitched gaps shorter than this are consonants, not mistakes
    double passFraction = 0.60;     // share of a note that must be inside a threshold to earn the grade
    double phraseGapSec = 0.60;     // silence in the reference that starts a new phrase
    double slackSec     = 0.12;     // how long after a note ends we wait for its last frames
    int    octaveShift  = 0;        // moves the reference only
    double refOffsetSec = 0.0;      // where the reference starts in the project
    int    focusPhrase  = -1;       // -1 = all phrases, otherwise a 0-based phrase index

    bool operator== (const ScoreSettings& o) const
    {
        return perfectCents == o.perfectCents && goodCents == o.goodCents && graceSec == o.graceSec
            && smoothSec == o.smoothSec && bridgeSec == o.bridgeSec && passFraction == o.passFraction
            && phraseGapSec == o.phraseGapSec && slackSec == o.slackSec && octaveShift == o.octaveShift
            && refOffsetSec == o.refOffsetSec && focusPhrase == o.focusPhrase;
    }
};

/** What one block earned. Times are reference time (no offset). */
struct BlockStat
{
    int    id = 0;
    double startSec = 0.0, endSec = 0.0;
    double targetMidi = 60.0;        // with the octave shift applied
    Grade  grade = Grade::None;
    float  quality = 0.0f;           // 0..1: Perfect time counts 1, Good time counts 0.5
    float  biasCents = 0.0f;         // + sharp, - flat, average over the pitched time
    bool   hasBias = false;
    float  octaveFrac = 0.0f;        // share of the note sung an octave away
    int    octaveDir = 0;            // +1 octave up, -1 octave down
    int    phrase = 0;
    bool   counted = false;          // graded in this pass
};

struct ScoreSnapshot
{
    std::vector<BlockStat> blocks;   // every block, counted or not
    std::vector<float> phraseScore;  // percent, or -1 when the phrase was not (fully) sung
    int score = 0;
    int accuracy = 0;                // percent
    int bestStreak = 0;
    int countedBlocks = 0;
};

class ScoreKeeper
{
public:
    /** The note under the playhead right now. */
    struct Live
    {
        bool   hasBlock = false;
        bool   voiced   = false;
        bool   inGrace  = false;
        double cents    = 0.0;      // smoothed, + sharp (pitch class only when an octave off)
        Grade  instant  = Grade::None;
        int    octave   = 0;        // +1 sung an octave too high, -1 too low
        double sungMidi = 0.0;
        double targetMidi = 0.0;
        int    blockId  = 0;
    };

    // ---- setup -------------------------------------------------------------
    void setBlocks (const std::vector<NoteBlock>& blocks)
    {
        std::vector<Rec> next;
        next.reserve (blocks.size());
        for (const auto& b : blocks)
        {
            Rec r;
            r.id = b.id;
            r.startSec = b.startSec();
            r.endSec = b.endSec();
            r.midi = b.midi();
            // keep what a block already earned if it is still the same block
            for (const auto& old : recs)
                if (old.id == b.id && old.startSec == r.startSec && old.endSec == r.endSec && old.midi == r.midi)
                {
                    r = old;
                    break;
                }
            next.push_back (r);
        }
        recs = std::move (next);
        rebuildPhrases();
        resetContext();
        recompute();
    }

    void setSettings (const ScoreSettings& s)
    {
        const bool phrasesChange = s.phraseGapSec != cfg.phraseGapSec;
        const bool wipe = s.octaveShift != cfg.octaveShift || s.refOffsetSec != cfg.refOffsetSec
                          || s.perfectCents != cfg.perfectCents || s.goodCents != cfg.goodCents
                          || s.graceSec != cfg.graceSec || s.bridgeSec != cfg.bridgeSec || s.passFraction != cfg.passFraction;
        cfg = s;
        if (phrasesChange) rebuildPhrases();
        if (wipe) resetAll();
        else
        {
            resetContext();
            recompute();
        }
    }

    const ScoreSettings& settings() const { return cfg; }

    /** A new pass starts at this project time (play, loop, punch-in, locate). Notes that
        end after it forget their results; earlier notes are kept for display only. */
    void resetFrom (double projectSec)
    {
        for (auto& r : recs)
        {
            if (projectEnd (r) > projectSec)
            {
                r.clearResult();
                r.carried = false;
            }
            else
                r.carried = true;
        }
        resetContext();
        recompute();
    }

    void resetAll()
    {
        for (auto& r : recs)
        {
            r.clearResult();
            r.carried = false;
        }
        resetContext();
        recompute();
    }

    // ---- feeding -----------------------------------------------------------
    /** One analysis frame: project time of its centre, how long it stands for, whether a pitch
        was found, and the pitch as a fractional MIDI note. */
    void onPoint (double projectSec, double frameSec, bool voiced, double midi)
    {
        live = Live();
        const int idx = indexAt (projectSec);
        if (idx != curIdx)
        {
            curIdx = idx;
            win.clear();
            pendingUnvoiced = 0.0;
            unvoicedCounted = false;
        }
        if (idx < 0)
            return;

        Rec& r = recs[(size_t) idx];
        if (! inFocus (r))
            return;

        const double target = r.midi + 12.0 * cfg.octaveShift;
        live.hasBlock = true;
        live.blockId = r.id;
        live.targetMidi = target;

        if (r.finished || r.carried)
            return;

        const double dur = r.endSec - r.startSec;
        const double grace = std::min (cfg.graceSec, 0.35 * dur);
        const bool inGrace = (projectSec - projectStart (r)) < grace;
        live.inGrace = inGrace;

        if (voiced)
        {
            const double err = midi - target;
            const int k = (int) std::lround (err / 12.0);
            const bool octaveOff = k != 0 && std::abs (err - 12.0 * k) <= 1.0;
            const double eff = octaveOff ? err - 12.0 * k : err;

            if (pendingUnvoiced > 0.0 && ! unvoicedCounted)
                r.bridged += pendingUnvoiced;          // a consonant: neither right nor wrong
            pendingUnvoiced = 0.0;
            unvoicedCounted = false;

            live.voiced = true;
            live.sungMidi = midi;
            live.octave = octaveOff ? (k > 0 ? 1 : -1) : 0;

            if (inGrace)
            {
                live.cents = eff * 100.0;
                return;
            }

            win.push_back ({ projectSec, eff });
            while (! win.empty() && win.front().t < projectSec - cfg.smoothSec)
                win.pop_front();
            double sum = 0.0;
            for (const auto& w : win) sum += w.err;
            const double smooth = sum / (double) win.size();
            live.cents = smooth * 100.0;

            Grade g = Grade::Miss;
            if (! octaveOff)
            {
                const double a = std::abs (live.cents);
                g = a <= cfg.perfectCents ? Grade::Perfect : (a <= cfg.goodCents ? Grade::Good : Grade::Miss);
            }
            live.instant = g;

            r.eligible += frameSec;
            if (g == Grade::Perfect) r.perfect += frameSec;
            else if (g == Grade::Good) r.good += frameSec;
            if (octaveOff)
            {
                if (err > 0) r.octUp += frameSec; else r.octDown += frameSec;
            }
            else
            {
                r.biasSum += eff * frameSec;
                r.biasWeight += frameSec;
            }
        }
        else
        {
            while (! win.empty() && win.front().t < projectSec - cfg.smoothSec)
                win.pop_front();
            if (inGrace)
                return;
            pendingUnvoiced += frameSec;
            if (unvoicedCounted)
                r.eligible += frameSec;
            else if (pendingUnvoiced > cfg.bridgeSec)
            {
                r.eligible += pendingUnvoiced;         // a real silence: it counts as a miss
                unvoicedCounted = true;
            }
            live.instant = Grade::None;
        }
    }

    /** Grades every note the playhead has gone past. Call every tick while playing. */
    void onPlayhead (double projectSec)
    {
        bool any = false;
        for (auto& r : recs)
            if (! r.finished && ! r.carried && inFocus (r) && projectEnd (r) + cfg.slackSec <= projectSec)
            {
                finalise (r);
                any = true;
            }
        if (any)
        {
            dirty = true;
            lastGraded = Grade::None;
            recompute();
            // the most recently graded note is the one that ends last
            const Rec* last = nullptr;
            for (const auto& r : recs)
                if (r.finished && ! r.carried && inFocus (r) && (! last || r.endSec > last->endSec))
                    last = &r;
            if (last) { lastGraded = last->grade; lastGradedRevision++; lastGradedPoints = (int) std::lround (last->points); }
        }
    }

    // ---- results -----------------------------------------------------------
    const Live& liveNote() const        { return live; }
    int  score() const                  { return (int) std::lround (totalScore); }
    int  accuracyPercent() const        { return accuracy; }
    bool hasAccuracy() const            { return countedBlocks > 0; }
    int  streak() const                 { return currentStreak; }
    int  bestStreak() const             { return maxStreak; }
    double multiplier() const           { return 1.0 + std::min (currentStreak, 30) * 0.1; }
    int  countedCount() const           { return countedBlocks; }
    Grade lastGrade() const             { return lastGraded; }
    int  lastGradeRevision() const      { return lastGradedRevision; }
    int  lastGradePoints() const        { return lastGradedPoints; }
    /** A graded pass is waiting to become a take. Listening through the song without singing does not count. */
    bool takeDirty() const              { return dirty && countedBlocks > 0 && sungShare >= 0.2; }
    void clearDirty()                   { dirty = false; }

    /** Result of one block for drawing: Grade::None until it is graded. */
    Grade gradeOf (int blockId) const
    {
        for (const auto& r : recs)
            if (r.id == blockId)
                return r.finished ? r.grade : Grade::None;
        return Grade::None;
    }
    bool isCarried (int blockId) const
    {
        for (const auto& r : recs)
            if (r.id == blockId)
                return r.carried;
        return false;
    }

    // ---- phrases -----------------------------------------------------------
    int phraseCount() const { return (int) phraseStart.size(); }
    int phraseOfBlock (int blockId) const
    {
        for (const auto& r : recs)
            if (r.id == blockId)
                return r.phrase;
        return 0;
    }
    /** Project-time range of a phrase. */
    bool phraseRange (int phrase, double& startSec, double& endSec) const
    {
        if (phrase < 0 || phrase >= phraseCount()) return false;
        startSec = phraseStart[(size_t) phrase] + cfg.refOffsetSec;
        endSec = phraseEnd[(size_t) phrase] + cfg.refOffsetSec;
        return true;
    }
    double phraseDuration (int phrase) const
    {
        return phrase >= 0 && phrase < phraseCount() ? phraseEnd[(size_t) phrase] - phraseStart[(size_t) phrase] : 0.0;
    }
    /** True for the first block of a phrase (the cue note shown in Ghost mode). */
    bool isPhraseFirst (int blockId) const
    {
        for (size_t i = 0; i < recs.size(); ++i)
            if (recs[i].id == blockId)
                return i == 0 || recs[i - 1].phrase != recs[i].phrase;
        return false;
    }

    ScoreSnapshot snapshot() const
    {
        ScoreSnapshot s;
        s.score = score();
        s.accuracy = accuracy;
        s.bestStreak = maxStreak;
        s.countedBlocks = countedBlocks;
        for (const auto& r : recs)
        {
            BlockStat b;
            b.id = r.id;
            b.startSec = r.startSec;
            b.endSec = r.endSec;
            b.targetMidi = r.midi + 12.0 * cfg.octaveShift;
            b.phrase = r.phrase;
            b.counted = r.finished && ! r.carried && inFocus (r);
            if (b.counted)
            {
                b.grade = r.grade;
                b.quality = (float) r.quality;
                b.hasBias = r.biasWeight > 0.0;
                b.biasCents = b.hasBias ? (float) (r.biasSum / r.biasWeight * 100.0) : 0.0f;
                const double den = r.denominator (cfg);
                b.octaveFrac = (float) std::clamp ((r.octUp + r.octDown) / den, 0.0, 1.0);
                b.octaveDir = r.octUp >= r.octDown ? 1 : -1;
            }
            s.blocks.push_back (b);
        }

        const int np = phraseCount();
        s.phraseScore.assign ((size_t) np, -1.0f);
        for (int p = 0; p < np; ++p)
        {
            double total = 0.0, counted = 0.0, weighted = 0.0;
            for (const auto& b : s.blocks)
            {
                if (b.phrase != p) continue;
                const double d = b.endSec - b.startSec;
                total += d;
                if (b.counted)
                {
                    counted += d;
                    weighted += d * b.quality;
                }
            }
            if (total > 0.0 && counted >= 0.8 * total)
                s.phraseScore[(size_t) p] = (float) (100.0 * weighted / counted);
        }
        return s;
    }

private:
    struct Rec
    {
        int id = 0;
        double startSec = 0, endSec = 0, midi = 60;
        int phrase = 0;
        double eligible = 0, perfect = 0, good = 0, bridged = 0, octUp = 0, octDown = 0, biasSum = 0, biasWeight = 0;
        bool finished = false, carried = false;
        Grade grade = Grade::None;
        double quality = 0, points = 0;

        void clearResult()
        {
            eligible = perfect = good = bridged = octUp = octDown = biasSum = biasWeight = 0;
            finished = false;
            grade = Grade::None;
            quality = points = 0;
        }
        double denominator (const ScoreSettings& c) const
        {
            const double dur = endSec - startSec;
            return std::max (0.02, dur - std::min (c.graceSec, 0.35 * dur) - bridged);
        }
    };

    struct WinEntry { double t, err; };

    double projectStart (const Rec& r) const { return r.startSec + cfg.refOffsetSec; }
    double projectEnd (const Rec& r) const   { return r.endSec + cfg.refOffsetSec; }
    bool inFocus (const Rec& r) const        { return cfg.focusPhrase < 0 || cfg.focusPhrase >= phraseCount() || r.phrase == cfg.focusPhrase; }

    int indexAt (double projectSec) const
    {
        // blocks are sorted by start and do not overlap
        int lo = 0, hi = (int) recs.size() - 1, found = -1;
        while (lo <= hi)
        {
            const int mid = (lo + hi) / 2;
            if (projectStart (recs[(size_t) mid]) <= projectSec) { found = mid; lo = mid + 1; }
            else hi = mid - 1;
        }
        if (found >= 0 && projectSec < projectEnd (recs[(size_t) found]))
            return found;
        return -1;
    }

    void resetContext()
    {
        curIdx = -1;
        win.clear();
        pendingUnvoiced = 0.0;
        unvoicedCounted = false;
        live = Live();
    }

    void finalise (Rec& r)
    {
        const double den = r.denominator (cfg);
        const double perfF = std::clamp (r.perfect / den, 0.0, 1.0);
        const double goodF = std::clamp ((r.perfect + r.good) / den, 0.0, 1.0);
        r.quality = std::clamp ((r.perfect + 0.5 * r.good) / den, 0.0, 1.0);
        r.grade = perfF >= cfg.passFraction ? Grade::Perfect : (goodF >= cfg.passFraction ? Grade::Good : Grade::Miss);
        r.finished = true;
    }

    static double basePoints (Grade g) { return g == Grade::Perfect ? 100.0 : (g == Grade::Good ? 50.0 : 0.0); }

    void recompute()
    {
        totalScore = 0.0;
        currentStreak = maxStreak = 0;
        countedBlocks = 0;
        double dur = 0.0, q = 0.0, sung = 0.0;
        for (auto& r : recs)
        {
            if (! (r.finished && ! r.carried && inFocus (r)))
                continue;
            ++countedBlocks;
            sung += std::min (r.biasWeight + r.octUp + r.octDown, r.endSec - r.startSec);
            const double mult = 1.0 + std::min (currentStreak, 30) * 0.1;
            r.points = basePoints (r.grade) * mult;
            totalScore += r.points;
            if (r.grade == Grade::Miss) currentStreak = 0;
            else ++currentStreak;
            maxStreak = std::max (maxStreak, currentStreak);
            const double d = r.endSec - r.startSec;
            dur += d;
            q += d * r.quality;
        }
        accuracy = dur > 0.0 ? (int) std::lround (100.0 * q / dur) : 0;
        sungShare = dur > 0.0 ? sung / dur : 0.0;
    }

    void rebuildPhrases()
    {
        phraseStart.clear();
        phraseEnd.clear();
        for (auto& r : recs) r.phrase = 0;
        if (recs.empty()) return;

        int p = 0;
        for (size_t i = 0; i < recs.size(); ++i)
        {
            if (i > 0 && recs[i].startSec - recs[i - 1].endSec >= cfg.phraseGapSec)
                ++p;
            recs[i].phrase = p;
        }
        // A melody with no real gaps would be one giant phrase: cut it into chunks instead.
        if (p == 0 && recs.size() >= 12)
            for (size_t i = 0; i < recs.size(); ++i)
                recs[i].phrase = (int) (i / 8);

        const int count = recs.back().phrase + 1;
        phraseStart.assign ((size_t) count, 0.0);
        phraseEnd.assign ((size_t) count, 0.0);
        std::vector<bool> seen ((size_t) count, false);
        for (const auto& r : recs)
        {
            const auto i = (size_t) r.phrase;
            if (! seen[i]) { phraseStart[i] = r.startSec; seen[i] = true; }
            phraseEnd[i] = r.endSec;
        }
    }

    std::vector<Rec> recs;
    std::vector<double> phraseStart, phraseEnd;
    ScoreSettings cfg;
    Live live;
    int curIdx = -1;
    std::deque<WinEntry> win;
    double pendingUnvoiced = 0.0;
    bool unvoicedCounted = false;

    double totalScore = 0.0;
    int accuracy = 0, currentStreak = 0, maxStreak = 0, countedBlocks = 0;
    double sungShare = 0.0;          // share of the graded time in which a pitch was heard
    bool dirty = false;
    Grade lastGraded = Grade::None;
    int lastGradedRevision = 0, lastGradedPoints = 0;
};
} // namespace va
