#pragma once

#include "Matcher.h"
#include "TraceStore.h"

namespace va
{
/** View settings the piano roll needs (all saved with the project). */
struct ViewSettings
{
    double refOffsetSec = 0.0;
    int    octaveShift  = 0;
    int    keyRoot      = 0;       // 0 = C .. 11 = B
    int    scale        = 0;       // 0 off, 1 major, 2 minor
    double a4           = 440.0;
};

/** What the piano roll and the big readout read from the plugin. Message thread only. */
class PerformanceSource
{
public:
    virtual ~PerformanceSource() = default;
    virtual double playheadSeconds() const = 0;      // project time, smoothly extrapolated while playing
    virtual bool   isPlaying() const = 0;
    virtual const Matcher& matcher() const = 0;
    virtual const TraceStore& trace() const = 0;
    virtual ViewSettings viewSettings() const = 0;
};

inline bool inScale (int pitchClass, int keyRoot, int scale)
{
    if (scale == 0) return true;
    static const bool major[12] = { 1,0,1,0,1,1,0,1,0,1,0,1 };
    static const bool minor[12] = { 1,0,1,1,0,1,0,1,1,0,1,0 };
    const int d = ((pitchClass - keyRoot) % 12 + 12) % 12;
    return scale == 1 ? major[d] : minor[d];
}
} // namespace va
