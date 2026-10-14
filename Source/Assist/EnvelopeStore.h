#pragma once

// Performance Assist: what your voice looked like, as a min/max envelope over time.
// The audio thread cuts the incoming audio into short slices and the editor draws
// those slices as the wave that flows into the note blocks.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace va
{
/** One slice of audio, as the audio thread reports it. */
struct EnvSlice
{
    int64_t hostSample = 0;     // host timeline position of the first sample
    float   mn = 0.0f, mx = 0.0f;
    bool    newPass = false;    // playback started or jumped here
};

/** One slice in project time, for drawing. */
struct EnvPoint
{
    double timeSec = 0.0;
    float  mn = 0.0f, mx = 0.0f;
};

class EnvelopeStore
{
public:
    void add (const EnvPoint& p)
    {
        pts.push_back (p);
        if (pts.size() > maxPoints)
            pts.erase (pts.begin(), pts.begin() + (std::ptrdiff_t) (maxPoints / 10));
    }

    void eraseFrom (double timeSec)
    {
        pts.erase (std::lower_bound (pts.begin(), pts.end(), timeSec,
                                     [] (const EnvPoint& a, double t) { return a.timeSec < t; }),
                   pts.end());
    }

    void clear() { pts.clear(); }
    const std::vector<EnvPoint>& points() const { return pts; }

    size_t lowerBound (double timeSec) const
    {
        return (size_t) (std::lower_bound (pts.begin(), pts.end(), timeSec,
                                           [] (const EnvPoint& a, double t) { return a.timeSec < t; }) - pts.begin());
    }

    /** Smallest and largest sample over [t0, t1). Returns false when there is no data there. */
    bool range (double t0, double t1, float& mn, float& mx) const
    {
        size_t i = lowerBound (t0);
        bool any = false;
        for (; i < pts.size() && pts[i].timeSec < t1; ++i)
        {
            if (! any) { mn = pts[i].mn; mx = pts[i].mx; any = true; }
            else { mn = std::min (mn, pts[i].mn); mx = std::max (mx, pts[i].mx); }
        }
        return any;
    }

private:
    static constexpr size_t maxPoints = 600000;     // about 27 minutes at 2.7 ms per slice
    std::vector<EnvPoint> pts;
};
} // namespace va
