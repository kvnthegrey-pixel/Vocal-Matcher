#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PitchTracker.h"
#include <atomic>

namespace va
{
/** Runs the PitchTracker on its own thread so the audio thread only copies
    samples into a lock-free FIFO. Nothing here writes to the audio buffer. */
class PitchEngine final : private juce::Thread
{
public:
    PitchEngine() : juce::Thread ("Vocal Assistor pitch"),
                    audioFifo (fifoSize), pointFifo (pointFifoSize),
                    audioStore ((size_t) fifoSize, 0.0f), pointStore ((size_t) pointFifoSize),
                    chunk ((size_t) chunkSize, 0.0f), mono ((size_t) 8192, 0.0f)
    {}

    ~PitchEngine() override { stop(); }

    void start (double sampleRate, int maxBlock)
    {
        stop();
        sr = sampleRate;
        monoCapacity = std::max (maxBlock, 1);
        mono.assign ((size_t) monoCapacity, 0.0f);
        audioFifo.reset();
        pointFifo.reset();
        tracker.prepare (sampleRate);
        applySettings();
        droppedSamples = 0;
        startThread (juce::Thread::Priority::normal);
    }

    void stop()
    {
        if (isThreadRunning())
            stopThread (2000);
    }

    double getSampleRate() const noexcept { return sr; }

    // ---- settings, any thread ------------------------------------------
    void setMethod (PitchTracker::Method m) { method = (int) m; }
    void setMcLeodGate (float g)            { mcleodGate = g; }
    void setYinGate (float g)               { yinGate = g; }

    // ---- audio thread ---------------------------------------------------
    /** Mixes the block down to mono and queues it. Reads the buffer only. */
    void pushAudio (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const int chans = buffer.getNumChannels();
        if (chans == 0 || n == 0 || ! isThreadRunning())
            return;

        int done = 0;
        while (done < n)
        {
            const int count = std::min (n - done, monoCapacity);
            float* m = mono.data();
            const float* first = buffer.getReadPointer (0) + done;
            for (int i = 0; i < count; ++i) m[i] = first[i];
            for (int c = 1; c < chans; ++c)
            {
                const float* src = buffer.getReadPointer (c) + done;
                for (int i = 0; i < count; ++i) m[i] += src[i];
            }
            if (chans > 1)
            {
                const float k = 1.0f / (float) chans;
                for (int i = 0; i < count; ++i) m[i] *= k;
            }

            int s1, n1, s2, n2;
            audioFifo.prepareToWrite (count, s1, n1, s2, n2);
            std::copy (m, m + n1, audioStore.begin() + s1);
            if (n2 > 0) std::copy (m + n1, m + n1 + n2, audioStore.begin() + s2);
            audioFifo.finishedWrite (n1 + n2);
            if (n1 + n2 < count)
                droppedSamples += count - (n1 + n2);
            done += count;
        }
    }

    // ---- UI thread ------------------------------------------------------
    int popPoints (PitchPoint* dst, int maxPoints) noexcept
    {
        int s1, n1, s2, n2;
        pointFifo.prepareToRead (maxPoints, s1, n1, s2, n2);
        std::copy (pointStore.begin() + s1, pointStore.begin() + s1 + n1, dst);
        if (n2 > 0) std::copy (pointStore.begin() + s2, pointStore.begin() + s2 + n2, dst + n1);
        pointFifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    int getDroppedSamples() const noexcept { return droppedSamples.load(); }

    /** Time from a sung sample reaching the plugin to its point being queued:
        half a window (the point describes the window's middle) plus up to one hop. */
    static double algorithmicDelayMs (double sampleRate)
    {
        return 1000.0 * (PitchTracker::windowSize / 2.0 + PitchTracker::hopSize) / sampleRate;
    }

private:
    static constexpr int fifoSize = 1 << 16;      // about 1.4 s of audio
    static constexpr int pointFifoSize = 4096;
    static constexpr int chunkSize = 1024;

    void applySettings()
    {
        tracker.setMethod ((PitchTracker::Method) method.load());
        tracker.setMcLeodGate (mcleodGate.load());
        tracker.setYinGate (yinGate.load());
    }

    void run() override
    {
        int lastMethod = method.load();
        float lastMc = mcleodGate.load(), lastYin = yinGate.load();

        while (! threadShouldExit())
        {
            if (lastMethod != method.load() || lastMc != mcleodGate.load() || lastYin != yinGate.load())
            {
                lastMethod = method.load(); lastMc = mcleodGate.load(); lastYin = yinGate.load();
                applySettings();
            }

            const int ready = audioFifo.getNumReady();
            if (ready == 0) { wait (2); continue; }

            const int count = std::min (ready, chunkSize);
            int s1, n1, s2, n2;
            audioFifo.prepareToRead (count, s1, n1, s2, n2);
            std::copy (audioStore.begin() + s1, audioStore.begin() + s1 + n1, chunk.begin());
            if (n2 > 0) std::copy (audioStore.begin() + s2, audioStore.begin() + s2 + n2, chunk.begin() + n1);
            audioFifo.finishedRead (n1 + n2);

            tracker.process (chunk.data(), n1 + n2, [this] (const PitchPoint& p)
            {
                int a, b, c, d;
                pointFifo.prepareToWrite (1, a, b, c, d);
                if (b > 0) { pointStore[(size_t) a] = p; pointFifo.finishedWrite (1); }
            });
        }
    }

    PitchTracker tracker;
    juce::AbstractFifo audioFifo, pointFifo;
    std::vector<float> audioStore;
    std::vector<PitchPoint> pointStore;
    std::vector<float> chunk, mono;
    int monoCapacity = 8192;
    double sr = 48000.0;

    std::atomic<int> method { (int) PitchTracker::Method::McLeod };
    std::atomic<float> mcleodGate { PitchTracker::defaultMcLeodGate };
    std::atomic<float> yinGate { PitchTracker::defaultYinGate };
    std::atomic<int> droppedSamples { 0 };
};
} // namespace va
