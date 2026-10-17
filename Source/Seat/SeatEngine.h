#pragma once

// Mix Seat: the maths. No plugin, no UI.
//
//   1. BeatProfile   - what the uploaded beat sounds like: 25 third-octave bands, for the
//                      whole song and for every few seconds of it.
//   2. VocalProfile  - what your vocal sounds like, learned while it plays through the plugin.
//   3. seatCurve()   - the EQ nudge (dB per band) that eases your vocal out of the bands where
//                      the beat is crowded and lets it into the bands where the beat leaves room.
//   4. SeatPlan      - that curve turned into the 12 EQ filter gains, for the whole song and
//                      for every window, so the plugin can follow the beat as it changes.
//
// This is a heuristic, not a law of physics: the numbers (importance of each region, how hard
// to push, what counts as a resonance) are starting values meant to be tuned by ear.

#include "../EqMatch.h"
#include "../ReferenceModel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace ms
{
using va::BandDb;
using va::numBands;
using va::numEqBands;
using Gains = std::array<float, (size_t) numEqBands>;

struct SeatSettings
{
    float amount = 0.7f;         // 0..1   how much of the full nudge is applied
    float maxDb  = 4.0f;         // cut limit in dB; boosts are limited to 60% of this
};

// -------------------------------------------------------------------------------------------
// Which parts of the spectrum matter for hearing a voice. 1 = fully, small = barely.
// -------------------------------------------------------------------------------------------
inline float importance (double hz)
{
    static const double f[] = { 63, 100, 160, 250, 400, 630, 1000, 2000, 3150, 5000, 8000, 12500, 16000 };
    static const float  w[] = { 0.15f, 0.25f, 0.5f, 0.65f, 0.8f, 0.9f, 1.0f, 1.0f, 1.0f, 0.8f, 0.55f, 0.3f, 0.2f };
    constexpr int n = (int) (sizeof (f) / sizeof (f[0]));
    if (hz <= f[0]) return w[0];
    if (hz >= f[n - 1]) return w[n - 1];
    int i = 0;
    while (i + 2 < n && hz > f[i + 1]) ++i;
    const double t = std::log (hz / f[i]) / std::log (f[i + 1] / f[i]);
    return (float) (w[i] * (1.0 - t) + w[i + 1] * t);
}

// -------------------------------------------------------------------------------------------
// Profiles
// -------------------------------------------------------------------------------------------
struct BeatProfile
{
    bool valid = false;
    std::string name;
    double durationSec = 0.0;
    double windowSec = 4.0;
    BandDb average {};
    std::vector<BandDb> windows;                 // one per windowSec, always filled (gaps copy the average)

    /** The beat's spectrum at a time in the beat file, blended between neighbouring windows. */
    BandDb at (double sec) const
    {
        if (windows.empty()) return average;
        const double pos = std::clamp (sec / windowSec - 0.5, 0.0, (double) windows.size() - 1.0);
        const size_t i = (size_t) std::floor (pos), j = std::min (windows.size() - 1, i + 1);
        const float t = (float) (pos - (double) i);
        BandDb out {};
        for (size_t b = 0; b < (size_t) numBands; ++b)
            out[b] = windows[i][b] * (1.0f - t) + windows[j][b] * t;
        return out;
    }

    std::string toText() const
    {
        std::ostringstream o;
        o.precision (5);
        o << "B1\n" << name << "\n" << durationSec << "|" << windowSec << "|" << windows.size() << "\nA";
        for (float v : average) o << '|' << v;
        o << "\n";
        for (const auto& w : windows)
        {
            o << "W";
            for (float v : w) o << '|' << v;
            o << "\n";
        }
        return o.str();
    }

    bool fromText (const std::string& text)
    {
        std::istringstream in (text);
        std::string line;
        if (! std::getline (in, line) || line != "B1") return false;
        BeatProfile p;
        if (! std::getline (in, p.name)) return false;
        if (! std::getline (in, line)) return false;
        {
            const auto f = split (line);
            if (f.size() < 3) return false;
            p.durationSec = std::atof (f[0].c_str());
            p.windowSec = std::atof (f[1].c_str());
            if (p.windowSec < 0.5) return false;
        }
        while (std::getline (in, line))
        {
            if (line.empty()) continue;
            const auto f = split (line);
            if (f.size() != (size_t) numBands + 1) return false;
            BandDb d {};
            for (size_t b = 0; b < (size_t) numBands; ++b)
            {
                d[b] = (float) std::atof (f[b + 1].c_str());
                if (! std::isfinite (d[b])) return false;
            }
            if (f[0] == "A") p.average = d;
            else if (f[0] == "W") p.windows.push_back (d);
        }
        if (p.windows.empty()) return false;
        p.valid = true;
        *this = std::move (p);
        return true;
    }

    static std::vector<std::string> split (const std::string& s)
    {
        std::vector<std::string> parts;
        std::string cur;
        for (char ch : s)
        {
            if (ch == '|') { parts.push_back (cur); cur.clear(); }
            else cur.push_back (ch);
        }
        parts.push_back (cur);
        return parts;
    }
};

struct VocalProfile
{
    bool valid = false;
    double seconds = 0.0;        // how much loud singing the average is built from
    BandDb average {};

    std::string toText() const
    {
        std::ostringstream o;
        o.precision (5);
        o << "V1\n" << seconds;
        for (float v : average) o << '|' << v;
        o << "\n";
        return o.str();
    }

    bool fromText (const std::string& text)
    {
        std::istringstream in (text);
        std::string line;
        if (! std::getline (in, line) || line != "V1") return false;
        if (! std::getline (in, line)) return false;
        const auto f = BeatProfile::split (line);
        if (f.size() != (size_t) numBands + 1) return false;
        VocalProfile p;
        p.seconds = std::atof (f[0].c_str());
        for (size_t b = 0; b < (size_t) numBands; ++b)
        {
            p.average[b] = (float) std::atof (f[b + 1].c_str());
            if (! std::isfinite (p.average[b])) return false;
        }
        p.valid = true;
        *this = p;
        return true;
    }
};

/** A vocal needs this much loud singing before the plugin trusts its own measurement. */
constexpr double kVocalReadySeconds = 3.0;

// -------------------------------------------------------------------------------------------
// Measuring (blocking: run on a background thread)
// -------------------------------------------------------------------------------------------
inline bool analyseBeatFile (const juce::File& file, BeatProfile& out, juce::String& error,
                             const std::function<bool (float)>& progress)
{
    std::vector<float> mono;
    double sr = 0.0;
    if (! va::decodeToMono (file, mono, sr, error, [&] (float p) { return progress (p * 0.5f); }))
        return false;

    BeatProfile p;
    p.name = file.getFileName().toStdString();
    p.durationSec = (double) mono.size() / sr;
    p.windowSec = 4.0;

    {
        va::BandSpectrum all (sr, 400000);
        const size_t chunk = 1 << 15;
        for (size_t i = 0; i < mono.size(); i += chunk)
            all.add (mono.data() + i, (int) std::min (chunk, mono.size() - i));
        if (! all.result (p.average))
        {
            error = "That file is too quiet or too short to measure (it needs at least a second of sound).";
            return false;
        }
    }

    const size_t win = (size_t) std::llround (p.windowSec * sr);
    const size_t nWin = std::max<size_t> (1, (mono.size() + win - 1) / win);
    for (size_t w = 0; w < nWin; ++w)
    {
        if (! progress (0.5f + 0.5f * (float) w / (float) nWin))
        {
            error = "cancelled";
            return false;
        }
        const size_t start = w * win, end = std::min (mono.size(), start + win);
        va::BandSpectrum spec (sr, 4000);
        const size_t chunk = 1 << 14;
        for (size_t i = start; i < end; i += chunk)
            spec.add (mono.data() + i, (int) std::min (chunk, end - i));
        BandDb d {};
        p.windows.push_back (spec.result (d) ? d : p.average);       // silent or tiny windows borrow the song average
    }
    p.valid = true;
    out = std::move (p);
    return true;
}

inline bool analyseVocalFile (const juce::File& file, VocalProfile& out, juce::String& error,
                              const std::function<bool (float)>& progress)
{
    std::vector<float> mono;
    double sr = 0.0;
    if (! va::decodeToMono (file, mono, sr, error, progress))
        return false;
    va::BandSpectrum spec (sr, 400000);
    const size_t chunk = 1 << 15;
    for (size_t i = 0; i < mono.size(); i += chunk)
        spec.add (mono.data() + i, (int) std::min (chunk, mono.size() - i));
    VocalProfile p;
    if (! spec.result (p.average, &p.seconds) || p.seconds < kVocalReadySeconds)
    {
        error = "That file does not have enough loud singing in it to measure (it needs about 3 seconds).";
        return false;
    }
    p.valid = true;
    out = p;
    return true;
}

/** Learns the vocal while it plays. Message thread only: it allocates. */
class VocalLearner
{
public:
    explicit VocalLearner (double sampleRate = 48000.0) { reset (sampleRate); }

    void reset (double sampleRate)
    {
        spec = std::make_unique<va::BandSpectrum> (sampleRate, 40000);
        samplesSinceResult = 0;
        sr = sampleRate;
    }
    /** Forget everything, including the profile. (reset() keeps the profile: it only restarts the measuring.) */
    void clear() { reset (sr); current = {}; }

    /** Feeds mono samples. Returns true when the profile was refreshed. */
    bool add (const float* x, int n)
    {
        spec->add (x, n);
        samplesSinceResult += n;
        if ((double) samplesSinceResult < 0.5 * sr)
            return false;
        samplesSinceResult = 0;
        VocalProfile p;
        if (! spec->result (p.average, &p.seconds))
            return false;
        p.valid = p.seconds >= kVocalReadySeconds;
        if (! p.valid && current.valid)
            return false;
        current = p;
        return true;
    }

    const VocalProfile& profile() const { return current; }
    void set (const VocalProfile& p) { current = p; }

private:
    std::unique_ptr<va::BandSpectrum> spec;
    int samplesSinceResult = 0;
    double sr = 48000.0;
    VocalProfile current;
};

// -------------------------------------------------------------------------------------------
// The nudge
// -------------------------------------------------------------------------------------------
/** Each band's level minus the straight line (in dB per octave) that best fits the whole
    spectrum. Every track has a natural slope (bass-heavy, or dark); only the bumps and dips
    away from that slope say where it is crowded and where it has room. */
inline BandDb localShape (const BandDb& d)
{
    double sw = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (int b = 0; b < numBands; ++b)
    {
        const double f = va::bandCentre (b);
        if (f < 100.0 || f > 10000.0) continue;
        const double w = importance (f) + 0.05, x = std::log2 (f / 1000.0), y = d[(size_t) b];
        sw += w; sx += w * x; sy += w * y; sxx += w * x * x; sxy += w * x * y;
    }
    const double den = sw * sxx - sx * sx;
    const double slope = den > 1.0e-9 ? (sw * sxy - sx * sy) / den : 0.0;
    const double icpt = sw > 0.0 ? (sy - slope * sx) / sw : 0.0;
    BandDb out {};
    for (int b = 0; b < numBands; ++b)
        out[(size_t) b] = d[(size_t) b] - (float) (icpt + slope * std::log2 (va::bandCentre (b) / 1000.0));
    return out;
}

/** The EQ curve (dB per band) for one moment: negative where the beat is crowded, positive
    where the beat leaves room. Only the shape matters (not how loud either track is, and not
    the natural slope of either), so the curve does not depend on faders or on how bright or
    dark the two tracks are overall. */
inline BandDb seatCurve (const BandDb& beat, const BandDb& vocal, const SeatSettings& s)
{
    const BandDb rb = localShape (beat), rv = localShape (vocal);

    BandDb crowd {};                                                // + = the beat is crowded here, - = the beat has room
    for (size_t b = 0; b < (size_t) numBands; ++b)
        crowd[b] = std::clamp (rb[b], -12.0f, 12.0f);
    {   // one pass of smoothing so a single odd band does not become a notch
        BandDb sm = crowd;
        for (int b = 0; b < numBands; ++b)
        {
            const float a = crowd[(size_t) std::max (0, b - 1)], c = crowd[(size_t) std::min (numBands - 1, b + 1)];
            sm[(size_t) b] = 0.25f * a + 0.5f * crowd[(size_t) b] + 0.25f * c;
        }
        crowd = sm;
    }

    const float cutMax = std::max (0.0f, s.maxDb), boostMax = 0.6f * cutMax;
    BandDb c {};
    for (int b = 0; b < numBands; ++b)
    {
        const double f = va::bandCentre (b);
        float v = -0.6f * importance (f) * crowd[(size_t) b] * s.amount;

        if (v < 0.0f)
        {
            // a cut only helps where your vocal actually has energy; where it is already thin, cut less
            v *= std::clamp (1.0f + rv[(size_t) b] / 12.0f, 0.3f, 1.3f);
        }
        else if (v > 0.0f)
        {
            // do not boost a band that already sticks out of the vocal (a resonance or sibilance)
            const float hot = rv[(size_t) b];
            v *= 1.0f - std::clamp ((hot - 4.0f) / 4.0f, 0.0f, 1.0f);
            if (f >= 12000.0) v = std::min (v, 1.0f);
        }
        if (f <= 80.0) v = std::min (v, 0.0f);                      // never boost rumble
        c[(size_t) b] = std::clamp (v, -cutMax, boostMax);
    }

    {   // keep the overall level where it was: the nudge moves energy, it does not add or remove it
        double m = 0.0, ws = 0.0;
        for (int b = 0; b < numBands; ++b)
        {
            const double f = va::bandCentre (b);
            if (f < 150.0 || f > 6000.0) continue;
            const double w = importance (f);
            m += c[(size_t) b] * w; ws += w;
        }
        m = ws > 0.0 ? m / ws : 0.0;
        for (int b = 0; b < numBands; ++b)
        {
            const double f = va::bandCentre (b);
            float v = c[(size_t) b] - (float) m;
            if (f <= 80.0) v = std::min (v, 0.0f);
            c[(size_t) b] = std::clamp (v, -cutMax, boostMax);
        }
    }
    return c;
}

// -------------------------------------------------------------------------------------------
// The plan: the curve, as filter gains, for the whole song and for every window
// -------------------------------------------------------------------------------------------
struct SeatPlan
{
    bool valid = false;
    bool hasWindows = false;
    BandDb staticCurve {};
    Gains staticGains {};
    double windowSec = 4.0;
    std::vector<BandDb> curves;
    std::vector<Gains> gains;

    /** Filter gains at a time in the beat file. follow=false returns the whole-song gains. */
    Gains gainsAt (double sec, bool follow) const
    {
        if (! follow || ! hasWindows || gains.empty())
            return staticGains;
        const double pos = std::clamp (sec / windowSec - 0.5, 0.0, (double) gains.size() - 1.0);
        const size_t i = (size_t) std::floor (pos), j = std::min (gains.size() - 1, i + 1);
        const float t = (float) (pos - (double) i);
        Gains out {};
        for (size_t b = 0; b < out.size(); ++b)
            out[b] = gains[i][b] * (1.0f - t) + gains[j][b] * t;
        return out;
    }

    BandDb curveAt (double sec, bool follow) const
    {
        if (! follow || ! hasWindows || curves.empty())
            return staticCurve;
        const double pos = std::clamp (sec / windowSec - 0.5, 0.0, (double) curves.size() - 1.0);
        const size_t i = (size_t) std::floor (pos), j = std::min (curves.size() - 1, i + 1);
        const float t = (float) (pos - (double) i);
        BandDb out {};
        for (size_t b = 0; b < out.size(); ++b)
            out[b] = curves[i][b] * (1.0f - t) + curves[j][b] * t;
        return out;
    }
};

inline SeatPlan buildPlan (const BeatProfile& beat, const VocalProfile& vocal, const SeatSettings& s,
                           double sampleRate, bool withWindows)
{
    SeatPlan plan;
    if (! beat.valid || ! vocal.valid)
        return plan;
    plan.valid = true;
    plan.windowSec = beat.windowSec;
    plan.staticCurve = seatCurve (beat.average, vocal.average, s);
    plan.staticGains = va::eqGainsFor (plan.staticCurve, sampleRate);
    if (withWindows)
    {
        plan.hasWindows = true;
        for (const auto& w : beat.windows)
        {
            plan.curves.push_back (seatCurve (w, vocal.average, s));
            plan.gains.push_back (va::eqGainsFor (plan.curves.back(), sampleRate));
        }
    }
    return plan;
}

// -------------------------------------------------------------------------------------------
// Plain-language reasons
// -------------------------------------------------------------------------------------------
inline std::string regionName (double hz)
{
    if (hz < 90.0)    return "the sub-bass";
    if (hz < 250.0)   return "the bass body";
    if (hz < 500.0)   return "the low-mids, where mud builds up";
    if (hz < 1000.0)  return "the boxy mids";
    if (hz < 2500.0)  return "the vocal clarity range";
    if (hz < 5000.0)  return "the presence range";
    if (hz < 9000.0)  return "the bite and sibilance range";
    return "the air range";
}

inline std::string hzText (double hz)
{
    if (hz >= 1000.0)
    {
        const double k = hz / 1000.0;
        char buf[32];
        std::snprintf (buf, sizeof (buf), k >= 10.0 ? "%.0f kHz" : "%.1f kHz", k);
        return buf;
    }
    return std::to_string ((int) std::lround (hz)) + " Hz";
}

struct SeatMove
{
    double lowHz = 0.0, highHz = 0.0, centreHz = 0.0;
    float  db = 0.0f;                 // largest change inside the range, with sign
    std::string text;
};

/** The biggest moves, merged across neighbouring bands, each with a reason. */
inline std::vector<SeatMove> explain (const BandDb& curve, size_t maxMoves = 4, float minDb = 0.7f)
{
    std::vector<SeatMove> moves;
    int b = 0;
    while (b < numBands)
    {
        if (std::abs (curve[(size_t) b]) < minDb) { ++b; continue; }
        const bool cut = curve[(size_t) b] < 0.0f;
        int e = b;
        while (e + 1 < numBands && std::abs (curve[(size_t) (e + 1)]) >= minDb && (curve[(size_t) (e + 1)] < 0.0f) == cut) ++e;

        SeatMove m;
        m.lowHz = va::bandCentre (b) * std::pow (2.0, -1.0 / 6.0);
        m.highHz = va::bandCentre (e) * std::pow (2.0, 1.0 / 6.0);
        int peak = b;
        for (int k = b; k <= e; ++k)
            if (std::abs (curve[(size_t) k]) > std::abs (curve[(size_t) peak])) peak = k;
        m.db = curve[(size_t) peak];
        m.centreHz = va::bandCentre (peak);

        char amount[32];
        std::snprintf (amount, sizeof (amount), "%.1f dB", std::abs (m.db));
        m.text = std::string (cut ? "Cut " : "Boost ") + amount + " around " + hzText (m.centreHz) + ": "
               + (cut ? "the beat is busy in " + regionName (m.centreHz) + ", so your voice would only be buried and add clutter there."
                      : "the beat leaves room in " + regionName (m.centreHz) + ", so your voice is heard there without turning it up.");
        moves.push_back (m);
        b = e + 1;
    }
    std::sort (moves.begin(), moves.end(), [] (const SeatMove& a, const SeatMove& c) { return std::abs (a.db) > std::abs (c.db); });
    if (moves.size() > maxMoves) moves.resize (maxMoves);
    return moves;
}
} // namespace ms
