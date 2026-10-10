#pragma once

#include "NoteBlocks.h"
#include "NoteSegmenter.h"
#include "PyinAnalyser.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace va
{
enum class SourceKind { None, Midi, Audio };
enum class Quality { None, Green, Amber, Red };

const char* qualityName (Quality);

/** Maps the numbers measured during audio analysis to the traffic-light
    indicator. MIDI is always Green. These cut-offs are first guesses that need
    your real stems to tune, which is why they live in one place. */
Quality assessAudioQuality (double meanClarity, double voicedFraction, double coverage);

/** What produced the blocks, stored with them so a reloaded project shows the
    same thing without any re-analysis. */
struct AnalysisInfo
{
    SourceKind kind = SourceKind::None;
    juce::String sourceName;
    Quality quality = Quality::None;
    double meanClarity = 0.0, voicedFraction = 0.0, jumpRate = 0.0, coverage = 0.0;
    int midiTrack = -1;
    SegmenterRules rules;
    PyinSettings pyin;
};

/** The reference vocal: blocks, how they were made, undo history. Owned by the
    processor. Edit from the message thread; the editor listens for changes. */
class ReferenceModel : public juce::ChangeBroadcaster
{
public:
    ReferenceModel() = default;

    // -- read (any thread, takes the lock) --
    BlockList blocksCopy() const;
    AnalysisInfo infoCopy() const;
    bool hasBlocks() const;

    // -- set by import or analysis --
    void setReference (std::vector<NoteBlock> blocks, const AnalysisInfo& info);
    void clear();

    // -- editing (all refuse when locked) --
    bool split (int id, int64_t atUs);
    bool merge (const std::vector<int>& ids);
    bool remove (const std::vector<int>& ids);
    bool shift (const std::vector<int>& ids, int semitones);
    void setLocked (bool);
    bool undo();
    bool canUndo() const;

    // -- saved state --
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);

private:
    template <typename Fn> bool edit (Fn&& fn);

    mutable juce::CriticalSection lock;
    BlockList list;
    AnalysisInfo info;
    std::vector<BlockList> undoStack;
};

/** Decodes any file JUCE can read into one mono channel. */
bool decodeToMono (const juce::File& file, std::vector<float>& mono, double& sampleRate, juce::String& error,
                   const std::function<bool (float)>& progress);
} // namespace va
