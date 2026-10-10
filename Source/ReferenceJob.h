#pragma once

#include "ReferenceModel.h"
#include <atomic>
#include <memory>

namespace va
{
struct AudioAnalysisResult
{
    bool ok = false;
    bool cancelled = false;
    juce::String error;
    std::vector<NoteBlock> blocks;
    AnalysisInfo info;
};

/** Decode, pYIN, segment. Blocking: call it from a background thread. */
AudioAnalysisResult analyseAudioFile (const juce::File& file, const SegmenterRules& rules, const PyinSettings& pyin,
                                      const std::function<bool (float)>& progress);

/** Runs analyseAudioFile on its own thread. The processor owns one, so the
    analysis carries on and finishes even if the window is closed. */
class ReferenceJob final : private juce::Thread
{
public:
    ReferenceJob() : juce::Thread ("Vocal Assistor reference analysis") {}
    ~ReferenceJob() override { cancel(); }

    bool start (const juce::File& f, const SegmenterRules& r, const PyinSettings& p)
    {
        if (isThreadRunning()) return false;
        file = f; rules = r; pyin = p;
        progress = 0.0f; cancelFlag = false; finished = false; delivered = true;
        startThread (juce::Thread::Priority::low);
        return true;
    }

    void cancel() { cancelFlag = true; stopThread (5000); }
    bool isRunning() const    { return const_cast<ReferenceJob*> (this)->isThreadRunning(); }
    float getProgress() const { return progress.load(); }
    juce::String getFileName() const { return file.getFileName(); }

    /** True once, after the thread has finished; fills result. */
    bool takeResult (AudioAnalysisResult& out)
    {
        if (! finished.load() || delivered) return false;
        out = std::move (result);
        delivered = true;
        return true;
    }

private:
    void run() override
    {
        result = analyseAudioFile (file, rules, pyin, [this] (float p)
        {
            progress = p;
            return ! threadShouldExit() && ! cancelFlag.load();
        });
        delivered = false;
        finished = true;
    }

    juce::File file;
    SegmenterRules rules;
    PyinSettings pyin;
    AudioAnalysisResult result;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> cancelFlag { false }, finished { false };
    bool delivered = true;
};
} // namespace va
