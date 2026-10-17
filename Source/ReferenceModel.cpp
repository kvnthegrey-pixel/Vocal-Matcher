#include "ReferenceModel.h"

namespace va
{
const char* qualityName (Quality q)
{
    switch (q)
    {
        case Quality::Green: return "Green";
        case Quality::Amber: return "Amber";
        case Quality::Red:   return "Red";
        default:             return "None";
    }
}

Quality assessAudioQuality (double meanClarity, double voicedFraction, double coverage)
{
    if (voicedFraction < 0.40 || meanClarity < 0.80 || coverage < 0.60) return Quality::Red;
    if (voicedFraction >= 0.70 && meanClarity >= 0.90 && coverage >= 0.85) return Quality::Green;
    return Quality::Amber;
}

BlockList ReferenceModel::blocksCopy() const { const juce::ScopedLock sl (lock); return list; }
AnalysisInfo ReferenceModel::infoCopy() const { const juce::ScopedLock sl (lock); return info; }
bool ReferenceModel::hasBlocks() const { const juce::ScopedLock sl (lock); return ! list.empty(); }
bool ReferenceModel::canUndo() const { const juce::ScopedLock sl (lock); return ! undoStack.empty(); }

void ReferenceModel::setReference (std::vector<NoteBlock> blocks, const AnalysisInfo& newInfo)
{
    {
        const juce::ScopedLock sl (lock);
        list = BlockList();
        list.replaceAll (std::move (blocks));
        info = newInfo;
        undoStack.clear();
    }
    sendChangeMessage();
}

void ReferenceModel::clear()
{
    {
        const juce::ScopedLock sl (lock);
        list = BlockList();
        info = AnalysisInfo();
        undoStack.clear();
    }
    sendChangeMessage();
}

template <typename Fn>
bool ReferenceModel::edit (Fn&& fn)
{
    bool changed;
    {
        const juce::ScopedLock sl (lock);
        auto before = list;
        changed = fn (list);
        if (changed)
        {
            undoStack.push_back (std::move (before));
            if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
        }
    }
    if (changed) sendChangeMessage();
    return changed;
}

bool ReferenceModel::split (int id, int64_t atUs)               { return edit ([&] (BlockList& l) { return l.split (id, atUs); }); }
bool ReferenceModel::merge (const std::vector<int>& ids)        { return edit ([&] (BlockList& l) { return l.merge (ids); }); }
bool ReferenceModel::remove (const std::vector<int>& ids)       { return edit ([&] (BlockList& l) { return l.remove (ids); }); }
bool ReferenceModel::shift (const std::vector<int>& ids, int s) { return edit ([&] (BlockList& l) { return l.shift (ids, s); }); }

void ReferenceModel::setLocked (bool shouldLock)
{
    {
        const juce::ScopedLock sl (lock);
        list.setLocked (shouldLock);
    }
    sendChangeMessage();
}

bool ReferenceModel::undo()
{
    {
        const juce::ScopedLock sl (lock);
        if (undoStack.empty()) return false;
        const bool wasLocked = list.isLocked();
        list = undoStack.back();
        list.setLocked (wasLocked);
        undoStack.pop_back();
    }
    sendChangeMessage();
    return true;
}

// ---------------------------------------------------------------------------
juce::ValueTree ReferenceModel::toValueTree() const
{
    const juce::ScopedLock sl (lock);
    juce::ValueTree t ("Reference");
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("kind", (int) info.kind, nullptr);
    t.setProperty ("sourceName", info.sourceName, nullptr);
    t.setProperty ("quality", (int) info.quality, nullptr);
    t.setProperty ("meanClarity", info.meanClarity, nullptr);
    t.setProperty ("voicedFraction", info.voicedFraction, nullptr);
    t.setProperty ("jumpRate", info.jumpRate, nullptr);
    t.setProperty ("coverage", info.coverage, nullptr);
    t.setProperty ("midiTrack", info.midiTrack, nullptr);
    t.setProperty ("locked", list.isLocked(), nullptr);

    juce::ValueTree rules ("Rules");
    rules.setProperty ("minNoteMs", info.rules.minNoteMs, nullptr);
    rules.setProperty ("maxDeviationCents", info.rules.maxDeviationCents, nullptr);
    rules.setProperty ("maxGapMs", info.rules.maxGapMs, nullptr);
    rules.setProperty ("mergeCents", info.rules.mergeCents, nullptr);
    rules.setProperty ("mergeGapMs", info.rules.mergeGapMs, nullptr);
    rules.setProperty ("snapToSemitone", info.rules.snapToSemitone, nullptr);
    rules.setProperty ("splitPersistFrames", info.rules.splitPersistFrames, nullptr);
    t.addChild (rules, -1, nullptr);

    juce::ValueTree py ("Pyin");
    py.setProperty ("minHz", info.pyin.minHz, nullptr);
    py.setProperty ("maxHz", info.pyin.maxHz, nullptr);
    py.setProperty ("frameSize", info.pyin.frameSize, nullptr);
    py.setProperty ("hopSize", info.pyin.hopSize, nullptr);
    py.setProperty ("centsPerBin", info.pyin.centsPerBin, nullptr);
    py.setProperty ("maxJumpBins", info.pyin.maxJumpBins, nullptr);
    py.setProperty ("switchProb", info.pyin.switchProb, nullptr);
    t.addChild (py, -1, nullptr);

    juce::ValueTree blocks ("Blocks");
    for (auto& b : list.blocks())
    {
        juce::ValueTree n ("Block");
        n.setProperty ("id", b.id, nullptr);
        n.setProperty ("startUs", (juce::int64) b.startUs, nullptr);
        n.setProperty ("endUs", (juce::int64) b.endUs, nullptr);
        n.setProperty ("midiMilli", b.midiMilli, nullptr);
        blocks.addChild (n, -1, nullptr);
    }
    t.addChild (blocks, -1, nullptr);
    return t;
}

void ReferenceModel::fromValueTree (const juce::ValueTree& t)
{
    if (! t.isValid() || ! t.hasType ("Reference")) return;
    {
        const juce::ScopedLock sl (lock);
        AnalysisInfo i;
        i.kind = (SourceKind) (int) t.getProperty ("kind", 0);
        i.sourceName = t.getProperty ("sourceName").toString();
        i.quality = (Quality) (int) t.getProperty ("quality", 0);
        i.meanClarity = t.getProperty ("meanClarity", 0.0);
        i.voicedFraction = t.getProperty ("voicedFraction", 0.0);
        i.jumpRate = t.getProperty ("jumpRate", 0.0);
        i.coverage = t.getProperty ("coverage", 0.0);
        i.midiTrack = t.getProperty ("midiTrack", -1);

        if (auto r = t.getChildWithName ("Rules"); r.isValid())
        {
            i.rules.minNoteMs = r.getProperty ("minNoteMs", i.rules.minNoteMs);
            i.rules.maxDeviationCents = r.getProperty ("maxDeviationCents", i.rules.maxDeviationCents);
            i.rules.maxGapMs = r.getProperty ("maxGapMs", i.rules.maxGapMs);
            i.rules.mergeCents = r.getProperty ("mergeCents", i.rules.mergeCents);
            i.rules.mergeGapMs = r.getProperty ("mergeGapMs", i.rules.mergeGapMs);
            i.rules.snapToSemitone = r.getProperty ("snapToSemitone", i.rules.snapToSemitone);
            i.rules.splitPersistFrames = r.getProperty ("splitPersistFrames", i.rules.splitPersistFrames);
        }
        if (auto p = t.getChildWithName ("Pyin"); p.isValid())
        {
            i.pyin.minHz = p.getProperty ("minHz", i.pyin.minHz);
            i.pyin.maxHz = p.getProperty ("maxHz", i.pyin.maxHz);
            i.pyin.frameSize = p.getProperty ("frameSize", i.pyin.frameSize);
            i.pyin.hopSize = p.getProperty ("hopSize", i.pyin.hopSize);
            i.pyin.centsPerBin = p.getProperty ("centsPerBin", i.pyin.centsPerBin);
            i.pyin.maxJumpBins = p.getProperty ("maxJumpBins", i.pyin.maxJumpBins);
            i.pyin.switchProb = p.getProperty ("switchProb", i.pyin.switchProb);
        }

        std::vector<NoteBlock> blocks;
        for (auto n : t.getChildWithName ("Blocks"))
        {
            NoteBlock b;
            b.id = n.getProperty ("id", 0);
            b.startUs = (juce::int64) n.getProperty ("startUs", 0);
            b.endUs = (juce::int64) n.getProperty ("endUs", 0);
            b.midiMilli = n.getProperty ("midiMilli", 60000);
            blocks.push_back (b);
        }
        list = BlockList();
        list.restore (std::move (blocks), (bool) t.getProperty ("locked", false));
        info = i;
        undoStack.clear();
    }
    sendChangeMessage();
}

// ---------------------------------------------------------------------------
bool decodeToMono (const juce::File& file, std::vector<float>& mono, double& sampleRate, juce::String& error,
                   const std::function<bool (float)>& progress)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
#if JUCE_USE_MP3AUDIOFORMAT
        error = "This audio file could not be opened. WAV, AIFF, FLAC, OGG and MP3 are supported.";
#else
        error = "This audio file could not be opened. WAV, AIFF, FLAC and OGG are supported.";
#endif
        return false;
    }
    sampleRate = reader->sampleRate;
    const auto total = reader->lengthInSamples;
    if (total <= 0 || sampleRate < 8000.0)
    {
        error = "The audio file is empty.";
        return false;
    }
    if (total > (juce::int64) (sampleRate * 60.0 * 20.0))
    {
        error = "The audio file is longer than 20 minutes.";
        return false;
    }

    mono.assign ((size_t) total, 0.0f);
    const int chunk = 1 << 16;
    juce::AudioBuffer<float> buf ((int) reader->numChannels, chunk);
    for (juce::int64 pos = 0; pos < total; pos += chunk)
    {
        const int n = (int) std::min<juce::int64> (chunk, total - pos);
        reader->read (&buf, 0, n, pos, true, true);
        for (int c = 0; c < buf.getNumChannels(); ++c)
        {
            const float* src = buf.getReadPointer (c);
            for (int i = 0; i < n; ++i)
                mono[(size_t) pos + (size_t) i] += src[i] / (float) buf.getNumChannels();
        }
        if (! progress ((float) pos / (float) total))
            return false;
    }
    return true;
}
} // namespace va
