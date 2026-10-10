#pragma once

#include <pitch_detector/pitch_detector.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace va
{
/** One analysed frame. centreSample is the position (in samples since the
    tracker was reset) of the middle of the analysis window. */
struct PitchPoint
{
    int64_t centreSample = 0;
    float   hz           = 0.0f;   // median-filtered; 0 when unvoiced
    float   rawHz        = 0.0f;   // before the median filter; 0 when unvoiced
    float   confidence   = 0.0f;   // McLeod: NSDF peak height. YIN: CMND value.
    bool    voiced       = false;
    // Host timeline, filled in by PitchEngine (not by the tracker).
    int64_t hostSample   = 0;       // host timeline position of the window centre
    bool    hostValid    = false;   // false while the transport is stopped
    int     passId       = 0;       // changes at every play / loop / locate
};

inline double hzToCents (double hz, double referenceHz) { return 1200.0 * std::log2 (hz / referenceHz); }

/** Latency compensation: moves a point along the time axis by the user's
    slider value (-50..+50 ms). Only the live trace uses this, never the audio
    and never the reference blocks. */
inline int64_t shiftedCentreSample (const PitchPoint& p, double latencyMs, double sampleRate)
{
    return p.centreSample + (int64_t) std::llround (latencyMs * 0.001 * sampleRate);
}

/** Real-time pitch tracker. Fixed 2048-sample window, 256-sample hop (the
    strict document's values), never resized. Allocation happens in the
    constructor only; process() is allocation-free. */
class PitchTracker
{
public:
    enum class Method { McLeod, Yin };

    static constexpr int windowSize = 2048;
    static constexpr int hopSize    = 256;
    static constexpr float minHz    = 80.0f;
    static constexpr float maxHz    = 1200.0f;
    static constexpr float silenceRms = 1.0e-4f;       // about -80 dBFS

    static constexpr float defaultMcLeodGate = 0.8f;   // voiced when clarity >= gate
    static constexpr float defaultYinGate    = 0.15f;  // voiced when CMND  <= gate

    PitchTracker()
        : ring ((size_t) windowSize, 0.0f), frame ((size_t) windowSize, 0.0f)
    {
        prepare (48000.0);
    }

    void prepare (double sampleRate)
    {
        // The window and hop are fixed in samples. At 48 kHz that is 42.7 ms
        // and 5.3 ms; at other rates the times scale.
        mpm = std::make_unique<adamski::PitchMPM> ((int) sampleRate, (size_t) windowSize);
        yin = std::make_unique<adamski::PitchYIN> ((int) sampleRate, (unsigned int) windowSize);
        mpm->setRange (minHz, maxHz);
        yin->setRange (minHz, maxHz);
        applyYinGate();
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0;
        sinceHop = 0;
        totalSamples = 0;
        histCount = 0;
    }

    void setMethod (Method m)        { method = m; }
    void setMcLeodGate (float g)     { mcleodGate = g; }
    void setYinGate (float g)        { yinGate = g; applyYinGate(); }
    Method getMethod() const         { return method; }

    /** Feeds mono samples. onPoint (const PitchPoint&) is called once per hop
        once a full window has been seen. */
    template <typename Callback>
    void process (const float* mono, int numSamples, Callback&& onPoint)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            ring[(size_t) writePos] = mono[i];
            writePos = (writePos + 1) % windowSize;
            ++totalSamples;

            if (++sinceHop >= hopSize)
            {
                sinceHop = 0;
                if (totalSamples >= windowSize)
                    onPoint (analyseFrame());
            }
        }
    }

private:
    void applyYinGate() { yin->setTolerence (yinGate); }

    PitchPoint analyseFrame()
    {
        // Unwrap the ring into a contiguous window, oldest sample first.
        for (int i = 0; i < windowSize; ++i)
            frame[(size_t) i] = ring[(size_t) ((writePos + i) % windowSize)];

        PitchPoint p;
        p.centreSample = totalSamples - windowSize / 2;

        double sumSq = 0.0;
        for (float s : frame)
            sumSq += (double) s * s;
        const float rms = (float) std::sqrt (sumSq / windowSize);

        float hz = 0.0f, conf = 0.0f;
        bool found = false;
        if (rms >= silenceRms)
        {
            found = (method == Method::McLeod) ? mpm->analyse (frame.data(), hz, conf)
                                               : yin->analyse (frame.data(), hz, conf);
        }

        p.confidence = conf;
        const bool passesGate = found && hz >= minHz && hz <= maxHz
                                && (method == Method::McLeod ? conf >= mcleodGate : conf <= yinGate);

        if (! passesGate)
        {
            histCount = 0;          // a gap breaks the smoothing
            return p;               // voiced = false, hz = 0
        }

        p.voiced = true;
        p.rawHz = hz;

        // 3-frame median on frequency, before anything converts to cents.
        if (histCount < 3)
            hist[(size_t) histCount++] = hz;
        else
        {
            hist[0] = hist[1];
            hist[1] = hist[2];
            hist[2] = hz;
        }
        p.hz = median (hist, histCount);
        return p;
    }

    static float median (const std::array<float, 3>& h, int n)
    {
        if (n == 1) return h[0];
        if (n == 2) return 0.5f * (h[0] + h[1]);
        return std::max (std::min (h[0], h[1]), std::min (std::max (h[0], h[1]), h[2]));
    }

    std::unique_ptr<adamski::PitchMPM> mpm;
    std::unique_ptr<adamski::PitchYIN> yin;
    Method method = Method::McLeod;
    float mcleodGate = defaultMcLeodGate;
    float yinGate = defaultYinGate;

    std::vector<float> ring, frame;
    int writePos = 0, sinceHop = 0;
    int64_t totalSamples = 0;

    std::array<float, 3> hist { 0.0f, 0.0f, 0.0f };
    int histCount = 0;
};
} // namespace va
