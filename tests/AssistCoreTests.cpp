// Performance Assist: tests for the scoring engine, take history and envelope store.
// Plain C++ with no JUCE, so these run in a second. Exit code 0 only if every check passes.
#include "../Source/Assist/ScoreKeeper.h"
#include "../Source/Assist/TakeStore.h"
#include "../Source/Assist/EnvelopeStore.h"

#include <cstdio>
#include <functional>

namespace
{
int failures = 0;
void check (bool ok, const std::string& what)
{
    std::printf ("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

constexpr double hop = 256.0 / 48000.0;     // the real analysis hop

std::vector<va::NoteBlock> makeBlocks (std::initializer_list<std::pair<double, double>> spans, std::initializer_list<int> pitches)
{
    std::vector<va::NoteBlock> v;
    auto p = pitches.begin();
    int id = 1;
    for (auto& s : spans)
    {
        va::NoteBlock b;
        b.id = id++;
        b.startUs = (int64_t) std::llround (s.first * 1.0e6);
        b.endUs = (int64_t) std::llround (s.second * 1.0e6);
        b.midiMilli = *p++ * 1000;
        v.push_back (b);
    }
    return v;
}

// Sings from t0 to t1. sing(t) returns the MIDI note sung, or a negative number for no pitch.
void sing (va::ScoreKeeper& k, double t0, double t1, const std::function<double (double)>& fn)
{
    for (double t = t0; t < t1; t += hop)
    {
        const double m = fn (t);
        k.onPoint (t, hop, m >= 0.0, m);
        k.onPlayhead (t);
    }
    k.onPlayhead (t1 + 1.0);
}

double cents (double c) { return c / 100.0; }
}

int main()
{
    std::printf ("== Grading a note ==\n");
    {
        va::ScoreKeeper k;
        k.setBlocks (makeBlocks ({ { 1.0, 2.0 }, { 2.5, 3.5 }, { 4.0, 5.0 }, { 5.5, 6.5 } }, { 60, 64, 67, 72 }));
        sing (k, 0.0, 7.0, [] (double t)
        {
            if (t >= 1.0 && t < 2.0) return 60.0;                 // dead on
            if (t >= 2.5 && t < 3.5) return 64.0 + cents (40);    // 40 cents sharp: still the right note
            if (t >= 4.0 && t < 5.0) return 67.0 + cents (70);    // 70 cents sharp: wrong
            if (t >= 5.5 && t < 6.5) return 72.0 - 12.0;          // an octave low
            return -1.0;
        });
        const auto s = k.snapshot();
        check (k.gradeOf (1) == va::Grade::Perfect, "dead on is Perfect");
        check (k.gradeOf (2) == va::Grade::Good, "40 cents sharp is Good");
        check (k.gradeOf (3) == va::Grade::Miss, "70 cents sharp is a Miss");
        check (k.gradeOf (4) == va::Grade::Miss, "an octave low is a Miss");
        check (s.blocks[3].octaveFrac > 0.9f && s.blocks[3].octaveDir == -1, "the octave slip is recorded as a whole note, down");
        check (s.blocks[1].hasBias && std::abs (s.blocks[1].biasCents - 40.0f) < 2.0f, "bias of the Good note is about +40 cents");
        check (k.score() == 100 + 55 + 0 + 0, "score: 100 x1.0, then Good 50 x1.1 = 55, then two misses");
        check (k.streak() == 0 && k.bestStreak() == 2, "streak broke on the miss; best streak was 2");
    }

    std::printf ("\n== Streak multiplier ==\n");
    {
        va::ScoreKeeper k;
        k.setBlocks (makeBlocks ({ { 1, 2 }, { 2.5, 3.5 }, { 4, 5 } }, { 60, 62, 64 }));
        sing (k, 0, 6, [] (double t)
        {
            if (t >= 1 && t < 2) return 60.0;
            if (t >= 2.5 && t < 3.5) return 62.0;
            if (t >= 4 && t < 5) return 64.0;
            return -1.0;
        });
        check (k.score() == 100 + 110 + 120, "three perfect notes: 100 + 110 + 120");
        check (k.streak() == 3 && std::abs (k.multiplier() - 1.3) < 1e-9, "streak 3, multiplier x1.3");
        check (k.accuracyPercent() >= 95, "accuracy is near 100");
    }

    std::printf ("\n== Things that must not count against you ==\n");
    {
        va::ScoreKeeper k;
        k.setBlocks (makeBlocks ({ { 1, 2 }, { 3, 4 }, { 5, 6 } }, { 60, 60, 60 }));
        sing (k, 0, 7, [] (double t)
        {
            if (t >= 1 && t < 2)        // scoop up from 2 semitones flat over the first 80 ms
            {
                const double u = t - 1.0;
                return u < 0.08 ? 58.0 + 2.0 * (u / 0.08) : 60.0;
            }
            if (t >= 3 && t < 4)        // vibrato of +-35 cents at 5.5 Hz, plus a 50 ms consonant gap
            {
                if (t > 3.5 && t < 3.55) return -1.0;
                return 60.0 + cents (35) * std::sin (2.0 * 3.14159265358979 * 5.5 * t);
            }
            if (t >= 5 && t < 6)        // sings 200 ms, then stops for 600 ms, then sings again
            {
                const double u = t - 5.0;
                return (u < 0.2 || u > 0.8) ? 60.0 : -1.0;
            }
            return -1.0;
        });
        check (k.gradeOf (1) == va::Grade::Perfect, "a scoop into the note is forgiven (grace period)");
        check (k.gradeOf (2) == va::Grade::Perfect, "vibrato and a consonant gap still score Perfect");
        check (k.gradeOf (3) != va::Grade::Perfect, "a 600 ms silence in the middle of a note is not forgiven");
    }

    std::printf ("\n== Not singing, and live readout ==\n");
    {
        va::ScoreKeeper k;
        k.setBlocks (makeBlocks ({ { 1, 2 } }, { 60 }));
        sing (k, 0, 3, [] (double) { return -1.0; });
        check (k.gradeOf (1) == va::Grade::Miss, "a block with no singing is a Miss");

        va::ScoreKeeper j;
        j.setBlocks (makeBlocks ({ { 1, 2 } }, { 60 }));
        for (double t = 1.3; t < 1.6; t += hop) j.onPoint (t, hop, true, 60.0 + 12.0);
        check (j.liveNote().octave == 1 && j.liveNote().hasBlock, "live: an octave too high is reported as +1");
        for (double t = 1.6; t < 1.9; t += hop) j.onPoint (t, hop, true, 60.0 - cents (30));
        check (j.liveNote().octave == 0 && std::abs (j.liveNote().cents + 30.0) < 3.0, "live: 30 cents flat reads about -30");
        j.onPoint (2.5, hop, true, 60.0);
        check (! j.liveNote().hasBlock, "live: between blocks there is nothing to hit");
    }

    std::printf ("\n== Listening is not a take ==\n");
    {
        va::ScoreKeeper k;
        k.setBlocks (makeBlocks ({ { 1, 2 }, { 3, 4 } }, { 60, 62 }));
        sing (k, 0, 5, [] (double) { return -1.0; });
        check (k.countedCount() == 2 && ! k.takeDirty(), "playing through in silence grades the notes but does not file a take");
        va::ScoreKeeper j;
        j.setBlocks (makeBlocks ({ { 1, 2 }, { 3, 4 } }, { 60, 62 }));
        sing (j, 0, 5, [] (double t) { return (t >= 1 && t < 2) ? 60.0 : -1.0; });
        check (j.takeDirty(), "singing one of two notes does file a take");
    }

    std::printf ("\n== Octave shift and offset move the reference only ==\n");
    {
        va::ScoreKeeper k;
        k.setBlocks (makeBlocks ({ { 1, 2 } }, { 60 }));
        va::ScoreSettings s;
        s.octaveShift = 1;
        s.refOffsetSec = 10.0;
        k.setSettings (s);
        sing (k, 10.0, 13.0, [] (double t) { return (t >= 11 && t < 12) ? 72.0 : -1.0; });
        check (k.gradeOf (1) == va::Grade::Perfect, "octave +1 and offset 10 s: singing C5 at 11 s is Perfect");
    }

    std::printf ("\n== Phrases and focus ==\n");
    {
        va::ScoreKeeper k;
        k.setBlocks (makeBlocks ({ { 1, 1.5 }, { 1.6, 2.1 }, { 4, 4.5 }, { 4.6, 5.1 }, { 8, 8.5 } }, { 60, 62, 64, 65, 67 }));
        check (k.phraseCount() == 3, "gaps of 0.6 s or more start a new phrase (3 phrases)");
        check (k.phraseOfBlock (3) == 1 && k.phraseOfBlock (5) == 2, "blocks belong to the right phrase");
        check (k.isPhraseFirst (3) && ! k.isPhraseFirst (4), "the first block of a phrase is the cue note");
        double a = 0, b = 0;
        check (k.phraseRange (1, a, b) && std::abs (a - 4.0) < 1e-9 && std::abs (b - 5.1) < 1e-9, "phrase 2 runs 4.0 to 5.1 s");

        va::ScoreSettings s;
        s.focusPhrase = 1;
        k.setSettings (s);
        sing (k, 0, 9, [] (double t)
        {
            if (t >= 1 && t < 1.5) return 60.0;
            if (t >= 4 && t < 4.5) return 64.0;
            if (t >= 4.6 && t < 5.1) return 65.0;
            return -1.0;
        });
        check (k.countedCount() == 2, "focusing phrase 2 counts only its two notes");
        check (k.gradeOf (1) == va::Grade::None, "notes outside the focus are not graded");

        va::ScoreKeeper full;
        full.setBlocks (makeBlocks ({ { 1, 1.5 }, { 1.6, 2.1 }, { 4, 4.5 }, { 4.6, 5.1 }, { 8, 8.5 } }, { 60, 62, 64, 65, 67 }));
        sing (full, 0, 6, [] (double t)          // playback stops at 6 s, before phrase 3
        {
            if (t >= 1 && t < 1.5) return 60.0;
            if (t >= 1.6 && t < 2.1) return 62.0;
            if (t >= 4 && t < 4.5) return 64.0 + 0.8;      // phrase 2 is out
            if (t >= 4.6 && t < 5.1) return 65.0 + 0.8;
            return -1.0;
        });
        const auto snap = full.snapshot();
        check (snap.phraseScore.size() == 3, "snapshot has a score per phrase");
        check (snap.phraseScore[0] > 90.0f && snap.phraseScore[1] < 30.0f, "phrase 1 scored high, phrase 2 low");
        check (snap.phraseScore[2] < 0.0f, "a phrase that was never reached has no score (-1)");
    }

    std::printf ("\n== A new pass (loop / punch-in) ==\n");
    {
        va::ScoreKeeper k;
        k.setBlocks (makeBlocks ({ { 1, 2 }, { 3, 4 }, { 5, 6 } }, { 60, 62, 64 }));
        sing (k, 0, 7, [] (double t)
        {
            if (t >= 1 && t < 2) return 60.0;
            if (t >= 3 && t < 4) return 62.0 + 1.0;       // wrong
            if (t >= 5 && t < 6) return 64.0;
            return -1.0;
        });
        check (k.gradeOf (2) == va::Grade::Miss && k.countedCount() == 3, "first pass: 3 notes graded, note 2 missed");
        check (k.takeDirty(), "a graded pass is waiting to become a take");
        k.clearDirty();

        k.resetFrom (2.9);                                // punch in at note 2
        check (k.isCarried (1) && ! k.isCarried (2), "note 1 is carried over; notes 2 and 3 start fresh");
        check (k.countedCount() == 0, "carried notes are not part of the new pass");
        sing (k, 2.9, 7, [] (double t)
        {
            if (t >= 3 && t < 4) return 62.0;
            if (t >= 5 && t < 6) return 64.0;
            return -1.0;
        });
        const auto s = k.snapshot();
        check (s.countedBlocks == 2 && ! s.blocks[0].counted, "the new take holds only the two notes sung in it");
        check (k.gradeOf (2) == va::Grade::Perfect, "note 2 re-sung right is Perfect");
    }

    std::printf ("\n== Take history, comping and saving ==\n");
    {
        auto blocks = makeBlocks ({ { 1, 2 }, { 4, 5 }, { 8, 9 } }, { 60, 64, 67 });
        auto run = [&] (std::function<double (double)> fn)
        {
            va::ScoreKeeper k;
            k.setBlocks (blocks);
            sing (k, 0, 10, fn);
            return k.snapshot();
        };
        auto great = [] (double t, double target, double from, double to) { return (t >= from && t < to) ? target : -1.0; };
        va::TakeStore store;
        // take 1: phrases 1 and 2 great, phrase 3 flat by 45 cents
        check (store.add (run ([&] (double t) {
            double m = great (t, 60, 1, 2); if (m >= 0) return m;
            m = great (t, 64, 4, 5); if (m >= 0) return m;
            return great (t, 67 - cents (45), 8, 9); })), "take 1 added");
        // take 2: phrase 1 flat by 45, phrase 2 great, phrase 3 great
        store.add (run ([&] (double t) {
            double m = great (t, 60 - cents (45), 1, 2); if (m >= 0) return m;
            m = great (t, 64, 4, 5); if (m >= 0) return m;
            return great (t, 67, 8, 9); }));
        check (store.takes().size() == 2 && store.takes()[1].number == 2, "two takes, numbered 1 and 2");

        std::vector<double> secs { 1, 1, 1 };
        const auto c = store.comp (secs);
        check (c.takeNumber.size() == 3 && c.takeNumber[0] == 1 && c.takeNumber[2] == 2, "comp: phrase 1 from take 1, phrase 3 from take 2");
        check (c.projected > 90.0, "the comp scores above 90%");
        check (store.compText (secs).find ("Phrase 1: take 1") != std::string::npos, "the comp plan reads in plain text");

        const auto t1 = store.takes()[0];
        check (tendencyText (t1).find ("flat") != std::string::npos, "take 1 is described as flat somewhere");
        std::printf ("  info %s\n", tendencyText (t1).c_str());

        va::TakeStore copy;
        check (copy.fromText (store.toText()), "saved text loads");
        check (copy.takes().size() == 2 && copy.takes()[0].blocks.size() == 3 && copy.nextNumber() == 3, "takes, notes and numbering survive a save");
        check (copy.comp (secs).takeNumber == c.takeNumber, "the comp is the same after a reload");
        check (! copy.fromText ("garbage"), "a corrupt save is refused");

        va::TakeStore none;
        check (! none.add (va::ScoreSnapshot()), "a pass with nothing graded does not become a take");
    }

    std::printf ("\n== Voice envelope store ==\n");
    {
        va::EnvelopeStore e;
        for (int i = 0; i < 1000; ++i) e.add ({ i * 0.001, -0.1f * (float) (i % 10), 0.2f * (float) (i % 7) });
        float mn = 0, mx = 0;
        check (e.range (0.100, 0.110, mn, mx) && mx > 0.0f && mn <= 0.0f, "range over 10 ms finds data");
        check (! e.range (5.0, 6.0, mn, mx), "no data beyond the end");
        e.eraseFrom (0.5);
        check (e.points().size() == 500 && e.points().back().timeSec < 0.5, "eraseFrom drops the stale part of a pass");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
