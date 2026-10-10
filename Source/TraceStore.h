#pragma once

#include <algorithm>
#include <vector>

namespace va
{
/** The live pitch line: one entry per analysis frame, in project time. */
struct TracePoint
{
    double timeSec = 0.0;
    float  midi = 0.0f;         // fractional MIDI note at the user's A4; 0 when unvoiced
    float  confidence = 0.0f;   // 0..1
    bool   voiced = false;
};

class TraceStore
{
public:
    void add (const TracePoint& p)
    {
        pts.push_back (p);
        if (pts.size() > maxPoints)
            pts.erase (pts.begin(), pts.begin() + (std::ptrdiff_t) (maxPoints / 10));
    }

    /** A new pass starts here (loop, locate, punch-in): the old line from here on is stale. */
    void eraseFrom (double timeSec)
    {
        pts.erase (std::lower_bound (pts.begin(), pts.end(), timeSec,
                                     [] (const TracePoint& a, double t) { return a.timeSec < t; }),
                   pts.end());
    }

    void clear() { pts.clear(); }
    const std::vector<TracePoint>& points() const { return pts; }

    /** Index of the first point at or after this time. */
    size_t lowerBound (double timeSec) const
    {
        return (size_t) (std::lower_bound (pts.begin(), pts.end(), timeSec,
                                           [] (const TracePoint& a, double t) { return a.timeSec < t; }) - pts.begin());
    }

private:
    static constexpr size_t maxPoints = 400000;      // well over an hour of frames
    std::vector<TracePoint> pts;
};
} // namespace va
