#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace va
{
/** One block of the reference melody. Times are whole microseconds and pitch
    is whole thousandths of a semitone (MIDI note number * 1000), so a saved
    project restores exactly the numbers that were saved. */
struct NoteBlock
{
    int     id        = 0;
    int64_t startUs   = 0;
    int64_t endUs     = 0;
    int     midiMilli = 60000;

    double  startSec() const { return (double) startUs * 1.0e-6; }
    double  endSec()   const { return (double) endUs * 1.0e-6; }
    double  midi()     const { return (double) midiMilli * 1.0e-3; }
    int64_t durationUs() const { return endUs - startUs; }
};

/** The editable list of blocks. Plain value type: copying it is how undo works.
    Every edit returns false and changes nothing when the list is locked or the
    request does not make sense. */
class BlockList
{
public:
    static constexpr int64_t minPartUs = 20000;   // a split may not leave a part shorter than 20 ms

    const std::vector<NoteBlock>& blocks() const { return list; }
    bool isLocked() const                         { return locked; }
    void setLocked (bool shouldLock)              { locked = shouldLock; }
    bool empty() const                            { return list.empty(); }

    /** Replaces the whole list (analysis or import). Sorts by start and gives ids. */
    void replaceAll (std::vector<NoteBlock> newBlocks)
    {
        std::sort (newBlocks.begin(), newBlocks.end(),
                   [] (const NoteBlock& a, const NoteBlock& b) { return a.startUs < b.startUs; });
        list = std::move (newBlocks);
        nextId = 1;
        for (auto& b : list)
            b.id = nextId++;
    }

    /** Restores ids exactly as saved. */
    void restore (std::vector<NoteBlock> savedBlocks, bool wasLocked)
    {
        list = std::move (savedBlocks);
        locked = wasLocked;
        nextId = 1;
        for (auto& b : list)
            nextId = std::max (nextId, b.id + 1);
    }

    const NoteBlock* find (int id) const
    {
        for (auto& b : list)
            if (b.id == id)
                return &b;
        return nullptr;
    }

    /** Index of the block that contains this time, or -1. */
    int indexAt (int64_t us) const
    {
        for (size_t i = 0; i < list.size(); ++i)
            if (us >= list[i].startUs && us < list[i].endUs)
                return (int) i;
        return -1;
    }

    bool split (int id, int64_t atUs)
    {
        if (locked) return false;
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i].id != id) continue;
            auto first = list[i];
            if (atUs - first.startUs < minPartUs || first.endUs - atUs < minPartUs)
                return false;
            auto second = first;
            second.id = nextId++;
            first.endUs = atUs;
            second.startUs = atUs;
            list[i] = first;
            list.insert (list.begin() + (std::ptrdiff_t) i + 1, second);
            return true;
        }
        return false;
    }

    /** Merges the selected blocks and everything between them into one block that
        takes the pitch of its longest part. One selected block merges with the next. */
    bool merge (const std::vector<int>& ids)
    {
        if (locked || ids.empty()) return false;
        int lo = -1, hi = -1;
        for (size_t i = 0; i < list.size(); ++i)
            if (std::find (ids.begin(), ids.end(), list[i].id) != ids.end())
            {
                if (lo < 0) lo = (int) i;
                hi = (int) i;
            }
        if (lo < 0) return false;
        if (lo == hi)
        {
            if (hi + 1 >= (int) list.size()) return false;
            ++hi;
        }
        NoteBlock merged = list[(size_t) lo];
        int64_t longest = list[(size_t) lo].durationUs();
        for (int i = lo + 1; i <= hi; ++i)
        {
            merged.endUs = std::max (merged.endUs, list[(size_t) i].endUs);
            if (list[(size_t) i].durationUs() > longest)
            {
                longest = list[(size_t) i].durationUs();
                merged.midiMilli = list[(size_t) i].midiMilli;
            }
        }
        list.erase (list.begin() + lo + 1, list.begin() + hi + 1);
        list[(size_t) lo] = merged;
        return true;
    }

    bool remove (const std::vector<int>& ids)
    {
        if (locked || ids.empty()) return false;
        const auto before = list.size();
        list.erase (std::remove_if (list.begin(), list.end(),
                                    [&] (const NoteBlock& b) { return std::find (ids.begin(), ids.end(), b.id) != ids.end(); }),
                    list.end());
        return list.size() != before;
    }

    bool shift (const std::vector<int>& ids, int semitones)
    {
        if (locked || ids.empty() || semitones == 0) return false;
        bool any = false;
        for (auto& b : list)
            if (std::find (ids.begin(), ids.end(), b.id) != ids.end())
            {
                const int moved = b.midiMilli + semitones * 1000;
                if (moved < 0 || moved > 127000) continue;
                b.midiMilli = moved;
                any = true;
            }
        return any;
    }

private:
    std::vector<NoteBlock> list;
    bool locked = false;
    int nextId = 1;
};
} // namespace va
