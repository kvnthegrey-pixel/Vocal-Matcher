#pragma once

#include "NoteBlocks.h"
#include "NoteSegmenter.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace va
{
struct MidiTrackInfo
{
    int index = 0;
    juce::String name;
    int noteCount = 0;
    int lowestNote = 127, highestNote = 0;
};

/** MIDI is the primary, preferred way to give Vocal Assistor a reference. */
namespace MidiReference
{
/** Tracks that contain notes (drum channel 10 ignored). Empty if the file cannot be read. */
std::vector<MidiTrackInfo> listTracks (const juce::MidiFile& file);

/** Notes of one track as a single melody. Overlaps are resolved by letting the
    later note cut the earlier one short. Notes shorter than the strict
    document's 80 ms are dropped. The split, gap and merge rules are for pitch
    tracks measured from audio and are not applied to MIDI, which is exact. */
std::vector<NoteBlock> importTrack (const juce::MidiFile& file, int trackIndex, const SegmenterRules& rules);

/** Writes the blocks as a one-track file at 120 bpm, 480 ticks per quarter note. */
juce::MidiFile exportBlocks (const std::vector<NoteBlock>& blocks);
}
} // namespace va
