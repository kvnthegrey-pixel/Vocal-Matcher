#pragma once

#include "TraceStore.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace va
{
/** One note the singer actually sang, cut out of the live trace. */
struct SungNote
{
    double startSec = 0.0, endSec = 0.0;
    double midi = 0.0;          // median pitch, fractional (not snapped)
};

/** Cuts the live trace into the notes you sang between t0 and t1. A note ends
    at a gap in pitch longer than 60 ms or when the pitch moves more than 60
    cents from the note's median. Shorter than 60 ms is dropped. */
inline std::vector<SungNote> segmentSung (const TraceStore& trace, double t0, double t1)
{
    constexpr double maxGap = 0.06, minLen = 0.06, splitSemis = 0.6;
    std::vector<SungNote> out;
    const auto& pts = trace.points();

    std::vector<float> cur;
    double start = 0, last = 0;
    auto flush = [&]
    {
        if (! cur.empty() && last - start >= minLen)
        {
            std::vector<float> s = cur;
            std::nth_element (s.begin(), s.begin() + (std::ptrdiff_t) s.size() / 2, s.end());
            out.push_back ({ start, last, (double) s[s.size() / 2] });
        }
        cur.clear();
    };

    for (size_t i = trace.lowerBound (t0); i < pts.size() && pts[i].timeSec <= t1; ++i)
    {
        const auto& p = pts[i];
        if (! p.voiced) continue;
        if (! cur.empty())
        {
            std::vector<float> s = cur;
            std::nth_element (s.begin(), s.begin() + (std::ptrdiff_t) s.size() / 2, s.end());
            const double med = (double) s[s.size() / 2];
            if (p.timeSec - last > maxGap || std::abs ((double) p.midi - med) > splitSemis)
                flush();
        }
        if (cur.empty()) start = p.timeSec;
        cur.push_back (p.midi);
        last = p.timeSec;
    }
    flush();
    return out;
}
} // namespace va
