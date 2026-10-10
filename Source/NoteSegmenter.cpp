#include "NoteSegmenter.h"

namespace va
{
namespace
{
double medianOf (std::vector<float> v)
{
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? (double) v[n / 2] : 0.5 * ((double) v[n / 2 - 1] + (double) v[n / 2]);
}

int64_t toUs (double sec) { return (int64_t) std::llround (sec * 1.0e6); }
}

std::vector<RawNote> splitAndGate (const FrameTrack& t, const SegmenterRules& r)
{
    std::vector<RawNote> out;
    const double half = 0.5 * t.hopSec;
    const double maxGapSec = r.maxGapMs * 1.0e-3;

    std::vector<float> frames;       // accepted frames of the open note
    std::vector<float> pending;      // frames that disagree with the open note
    double noteStart = 0.0, lastAcceptedEnd = 0.0, pendingStart = 0.0;
    bool open = false;
    double unvoicedRun = 0.0;

    auto timeOf = [&] (size_t i) { return t.firstCentreSec + (double) i * t.hopSec; };

    auto close = [&]
    {
        if (open && ! frames.empty())
            out.push_back ({ toUs (std::max (0.0, noteStart)), toUs (lastAcceptedEnd), medianOf (frames) });
        frames.clear();
        pending.clear();
        open = false;
    };

    for (size_t i = 0; i < t.midi.size(); ++i)
    {
        const float m = t.midi[i];
        if (m <= 0.0f)
        {
            unvoicedRun += t.hopSec;
            if (open && unvoicedRun > maxGapSec)
                close();
            continue;
        }
        unvoicedRun = 0.0;

        if (! open)
        {
            open = true;
            frames.assign (1, m);
            noteStart = timeOf (i) - half;
            lastAcceptedEnd = timeOf (i) + half;
            continue;
        }

        const double centre = medianOf (frames);
        if (std::abs ((double) m - centre) * 100.0 > r.maxDeviationCents)
        {
            if (pending.empty()) pendingStart = timeOf (i) - half;
            pending.push_back (m);
            if ((int) pending.size() >= r.splitPersistFrames)
            {
                // The new pitch has persisted: end the old note and start a new one.
                auto carried = pending;
                close();
                open = true;
                frames = carried;
                noteStart = pendingStart;
                lastAcceptedEnd = timeOf (i) + half;
                pending.clear();
            }
            continue;
        }

        pending.clear();
        frames.push_back (m);
        lastAcceptedEnd = timeOf (i) + half;
    }
    close();
    return out;
}

std::vector<RawNote> mergeAdjacent (std::vector<RawNote> notes, const SegmenterRules& r)
{
    std::vector<RawNote> out;
    for (auto& n : notes)
    {
        if (! out.empty())
        {
            auto& prev = out.back();
            const double gapMs = (double) (n.startUs - prev.endUs) * 1.0e-3;
            if (gapMs < r.mergeGapMs && std::abs (n.midi - prev.midi) * 100.0 <= r.mergeCents)
            {
                const double wa = (double) (prev.endUs - prev.startUs), wb = (double) (n.endUs - n.startUs);
                prev.midi = (prev.midi * wa + n.midi * wb) / std::max (1.0, wa + wb);
                prev.endUs = n.endUs;
                continue;
            }
        }
        out.push_back (n);
    }
    return out;
}

std::vector<RawNote> dropShort (std::vector<RawNote> notes, const SegmenterRules& r)
{
    notes.erase (std::remove_if (notes.begin(), notes.end(),
                                 [&] (const RawNote& n) { return (double) (n.endUs - n.startUs) * 1.0e-3 < r.minNoteMs; }),
                 notes.end());
    return notes;
}

std::vector<NoteBlock> toBlocks (const std::vector<RawNote>& notes, const SegmenterRules& r)
{
    std::vector<NoteBlock> out;
    for (auto& n : notes)
    {
        NoteBlock b;
        b.startUs = n.startUs;
        b.endUs = n.endUs;
        b.midiMilli = r.snapToSemitone ? (int) std::lround (n.midi) * 1000 : (int) std::lround (n.midi * 1000.0);
        out.push_back (b);
    }
    return out;
}

double noteCoverage (const FrameTrack& t, const std::vector<NoteBlock>& blocks)
{
    size_t voiced = 0, covered = 0, b = 0;
    for (size_t i = 0; i < t.midi.size(); ++i)
    {
        if (t.midi[i] <= 0.0f) continue;
        ++voiced;
        const int64_t us = toUs (t.firstCentreSec + (double) i * t.hopSec);
        while (b < blocks.size() && blocks[b].endUs <= us) ++b;
        if (b < blocks.size() && us >= blocks[b].startUs && std::abs ((double) t.midi[i] - blocks[b].midi()) <= 0.5)
            ++covered;
    }
    return voiced ? (double) covered / (double) voiced : 0.0;
}

std::vector<NoteBlock> segment (const FrameTrack& t, const SegmenterRules& r)
{
    return toBlocks (dropShort (mergeAdjacent (splitAndGate (t, r), r), r), r);
}
} // namespace va
