#include "MidiReference.h"

namespace va::MidiReference
{
namespace
{
constexpr int ppq = 480;
constexpr double ticksPerSecond = 960.0;     // 120 bpm * 480 ppq / 60

struct Raw { double start, end; int note; };

std::vector<Raw> collect (const juce::MidiMessageSequence& seq)
{
    std::vector<Raw> notes;
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        auto* ev = seq.getEventPointer (i);
        const auto& m = ev->message;
        if (! m.isNoteOn() || m.getChannel() == 10) continue;
        double end = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : m.getTimeStamp();
        notes.push_back ({ m.getTimeStamp(), end, m.getNoteNumber() });
    }
    return notes;
}
}

std::vector<MidiTrackInfo> listTracks (const juce::MidiFile& original)
{
    juce::MidiFile file (original);
    file.convertTimestampTicksToSeconds();
    std::vector<MidiTrackInfo> out;
    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        auto* seq = file.getTrack (t);
        MidiTrackInfo info;
        info.index = t;
        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const auto& m = seq->getEventPointer (i)->message;
            if (m.isTrackNameEvent()) info.name = m.getTextFromTextMetaEvent();
            if (m.isNoteOn() && m.getChannel() != 10)
            {
                ++info.noteCount;
                info.lowestNote = std::min (info.lowestNote, m.getNoteNumber());
                info.highestNote = std::max (info.highestNote, m.getNoteNumber());
            }
        }
        if (info.noteCount > 0) out.push_back (info);
    }
    return out;
}

std::vector<NoteBlock> importTrack (const juce::MidiFile& original, int trackIndex, const SegmenterRules& rules)
{
    juce::MidiFile file (original);
    file.convertTimestampTicksToSeconds();
    if (trackIndex < 0 || trackIndex >= file.getNumTracks()) return {};

    auto notes = collect (*file.getTrack (trackIndex));
    std::sort (notes.begin(), notes.end(), [] (const Raw& a, const Raw& b) { return a.start < b.start; });

    // One melody: a note that starts before the previous one ends cuts it short.
    for (size_t i = 0; i + 1 < notes.size(); ++i)
        if (notes[i].end > notes[i + 1].start)
            notes[i].end = notes[i + 1].start;

    std::vector<NoteBlock> out;
    for (auto& n : notes)
    {
        if ((n.end - n.start) * 1000.0 < rules.minNoteMs) continue;
        NoteBlock b;
        b.startUs = (int64_t) std::llround (n.start * 1.0e6);
        b.endUs = (int64_t) std::llround (n.end * 1.0e6);
        b.midiMilli = n.note * 1000;
        out.push_back (b);
    }
    return out;
}

juce::MidiFile exportBlocks (const std::vector<NoteBlock>& blocks)
{
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (500000));      // 120 bpm
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Vocal Assistor reference"));
    for (auto& b : blocks)
    {
        const int note = juce::jlimit (0, 127, (int) std::lround (b.midi()));
        seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), (double) std::llround (b.startSec() * ticksPerSecond));
        seq.addEvent (juce::MidiMessage::noteOff (1, note), (double) std::llround (b.endSec() * ticksPerSecond));
    }
    seq.updateMatchedPairs();
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);
    file.addTrack (seq);
    return file;
}
} // namespace va::MidiReference
