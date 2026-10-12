#pragma once

#include "Dsp.h"
#include "../PitchTracker.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace vc
{
inline int scaleMask (int scale)       // bit i set = pitch class i above the key is allowed
{
    switch (scale)
    {
        case 0:  return 0xFFF;                                                    // chromatic
        case 1:  return (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11);   // major
        case 2:  return (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10);   // natural minor
        case 3:  return (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9);     // major pentatonic
        default: return (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10);    // minor pentatonic
    }
}

/** Nearest allowed note (MIDI, whole number) for a fractional pitch. Keeps the
    current note until the singer is clearly closer to another one, so a note
    that sits between two does not flicker. */
inline int snapToScale (double midi, int keyRoot, int scale, int previousNote)
{
    const int mask = scaleMask (scale);
    auto allowed = [&] (int note) { return ((mask >> (((note - keyRoot) % 12 + 12) % 12)) & 1) != 0; };
    if (previousNote > 0 && allowed (previousNote) && std::abs (midi - previousNote) < 0.65)
        return previousNote;
    int best = (int) std::lround (midi);
    double bestDist = 1.0e9;
    for (int n = (int) std::floor (midi) - 6; n <= (int) std::ceil (midi) + 6; ++n)
        if (allowed (n) && std::abs (midi - n) < bestDist) { bestDist = std::abs (midi - n); best = n; }
    return best;
}

/** Automatic pitch correction by pitch-synchronous overlap-add (PSOLA): the
    voice is cut into one-period grains and re-laid at the corrected spacing,
    so the pitch moves while the formants (the vowel and the character of the
    voice) stay where they were. Fixed latency of kLatency samples. */
class PitchCorrector
{
public:
    static constexpr int kLatency = 2048;

    struct Params
    {
        bool  enabled = true;
        int   keyRoot = 0, scale = 1;       // key C..B, scale index (see scaleMask)
        float speedMs = 20.0f;              // 0 = instant (hard tune), larger = natural glide
        float amount  = 1.0f;               // 0..1 how far to move towards the note
    };

    void prepare (double sampleRate, int channels)
    {
        sr = sampleRate;
        chans = std::max (1, std::min (channels, 2));
        for (int c = 0; c < 2; ++c)
        {
            hist[(size_t) c].assign ((size_t) kHist, 0.0f);
            acc[(size_t) c].assign ((size_t) kAcc, 0.0f);
        }
        mono.assign ((size_t) kHist, 0.0f);
        wsum.assign ((size_t) kAcc, 0.0f);
        tracker.prepare (sampleRate);
        tracker.setMethod (va::PitchTracker::Method::McLeod);
        lhMax = (int) std::ceil (sr / 80.0) + 8;
        puSamples = std::max (32, (int) std::lround (sr * 0.006));
        reset();
    }

    void reset()
    {
        inCount = 0;
        nextEpoch = (double) lhMax;
        marks.clear();
        lastMark = INT64_MIN;
        dets.clear();
        corrCents = 0.0;
        curNote = 0; octRun = 0; prevHz = 0.0f;
        std::fill (wsum.begin(), wsum.end(), 0.0f);
        for (auto& a : acc) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
        std::fill (mono.begin(), mono.end(), 0.0f);
        tracker.reset();
    }

    void process (juce::AudioBuffer<float>& buffer, const Params& p)
    {
        const int n = buffer.getNumSamples();
        const int nc = std::min (buffer.getNumChannels(), chans);
        for (int i = 0; i < n; ++i)
        {
            // 1. take the input sample in
            float m = 0.0f;
            for (int c = 0; c < nc; ++c)
            {
                const float x = buffer.getReadPointer (c)[i];
                hist[(size_t) c][(size_t) (inCount & kHistMask)] = x;
                m += x;
            }
            m /= (float) nc;
            mono[(size_t) (inCount & kHistMask)] = m;
            tracker.process (&m, 1, [this] (const va::PitchPoint& pt)
            {
                // a voiced run that suddenly reads an octave off is almost always a detector slip: fold it back
                float hz = pt.hz;
                if (pt.voiced && prevHz > 0.0f)
                {
                    const float r = hz / prevHz;
                    const int oct = (r > 1.85f && r < 2.15f) ? 1 : (r > 0.465f && r < 0.54f) ? -1 : 0;
                    if (oct != 0 && ++octRun < 6) hz = oct > 0 ? hz * 0.5f : hz * 2.0f;
                    else octRun = 0;
                }
                prevHz = pt.voiced ? hz : 0.0f;
                if (! pt.voiced) octRun = 0;
                dets.push_back ({ pt.centreSample, hz, pt.voiced });
                if (dets.size() > 96) dets.erase (dets.begin(), dets.begin() + 32);
            });
            // the tracker counts from 0 like inCount does, so centreSample lines up with input time

            // 2. place every grain that can still touch this output sample
            while (nextEpoch <= (double) (inCount + lhMax))
                placeGrain (p, nc);

            // 3. emit
            const size_t ai = (size_t) (inCount & kAccMask);
            for (int c = 0; c < nc; ++c)
            {
                const float w = std::max (wsum[ai], 0.3f);
                buffer.getWritePointer (c)[i] = acc[(size_t) c][ai] / w;
                acc[(size_t) c][ai] = 0.0f;
            }
            wsum[ai] = 0.0f;
            ++inCount;
        }
    }

    // for the display (any thread)
    float detectedMidi() const { return uiDetected.load(); }
    float targetMidi() const   { return uiTarget.load(); }

private:
    static constexpr int kHist = 1 << 14, kHistMask = kHist - 1;
    static constexpr int kAcc = 1 << 13, kAccMask = kAcc - 1;

    struct Det { int64_t centre; float hz; bool voiced; };

    // pitch of the input at input time t: interpolated between two detections, 0 if unvoiced
    float pitchAt (int64_t t) const
    {
        if (dets.empty()) return 0.0f;
        size_t j = dets.size();
        while (j > 0 && dets[j - 1].centre > t) --j;       // dets[j-1] is the last one at or before t
        if (j == 0) return dets.front().voiced ? dets.front().hz : 0.0f;
        const Det& a = dets[j - 1];
        if (j >= dets.size()) return a.voiced ? a.hz : 0.0f;
        const Det& b = dets[j];
        if (! a.voiced || ! b.voiced) return (t - a.centre < b.centre - t ? a.voiced : b.voiced) ? (a.voiced ? a.hz : b.hz) : 0.0f;
        const float f = (float) (t - a.centre) / (float) std::max<int64_t> (1, b.centre - a.centre);
        return std::exp (std::log (a.hz) * (1.0f - f) + std::log (b.hz) * f);
    }

    float monoAt (int64_t t) const { return t < 0 ? 0.0f : mono[(size_t) (t & kHistMask)]; }

    // extend the list of analysis marks (one per period, snapped to the local waveform peak) to cover input time upto
    void extendMarks (int64_t tIn, int64_t upto, int64_t newest)
    {
        if (lastMark == INT64_MIN) { lastMark = tIn; marks.push_back (tIn); }
        while (lastMark < upto)
        {
            const float f = pitchAt (lastMark);
            const bool voiced = f > 0.0f;
            const int P = voiced ? (int) std::lround (sr / f) : puSamples;
            int64_t next = lastMark + P;
            if (voiced)
            {
                const int span = std::max (1, P / 4);
                float best = -1.0e9f;
                int64_t bestT = next;
                for (int64_t t = next - span; t <= next + span && t <= newest; ++t)
                    if (monoAt (t) > best) { best = monoAt (t); bestT = t; }
                next = std::max (bestT, lastMark + P / 2);
            }
            marks.push_back (next);
            lastMark = next;
        }
        while (marks.size() > 3 && marks[1] < upto - 6000) marks.erase (marks.begin());
    }

    void placeGrain (const Params& p, int nc)
    {
        const int64_t e = (int64_t) std::llround (nextEpoch);
        const int64_t tIn = e - kLatency;                    // input moment this grain stands for
        const float fIn = (p.enabled && tIn > 0) ? pitchAt (tIn) : 0.0f;
        const bool voiced = fIn > 0.0f;

        int Lh;
        double spacing;
        int64_t centre;
        if (voiced)
        {
            const double midiIn = 69.0 + 12.0 * std::log2 ((double) fIn / 440.0);
            const int note = snapToScale (midiIn, p.keyRoot, p.scale, curNote);
            curNote = note;
            const double wantCents = (double) p.amount * ((double) note - midiIn) * 100.0;
            const double period = sr / (double) fIn;
            const double tau = (double) p.speedMs * 0.001 * sr;
            const double alpha = tau < 1.0 ? 1.0 : 1.0 - std::exp (-period / tau);
            corrCents += alpha * (wantCents - corrCents);
            corrCents = std::clamp (corrCents, -600.0, 600.0);
            const double fOut = (double) fIn * std::pow (2.0, corrCents / 1200.0);
            Lh = std::clamp ((int) std::lround (period), 16, lhMax);
            spacing = sr / fOut;
            extendMarks (tIn, tIn + (int64_t) (period * 1.5) + 2, inCount);
            centre = tIn;
            int64_t bestD = INT64_MAX;
            for (auto m : marks)
                if (std::llabs (m - tIn) < bestD) { bestD = std::llabs (m - tIn); centre = m; }
            uiDetected.store ((float) midiIn);
            uiTarget.store ((float) (midiIn + corrCents / 100.0));
        }
        else
        {
            Lh = puSamples;
            spacing = (double) puSamples;                     // a plain delayed copy: ratio 1, Hann grains at half overlap
            centre = tIn;
            corrCents *= 0.5;
            curNote = 0;
            lastMark = INT64_MIN;
            marks.clear();
            uiDetected.store (0.0f);
            uiTarget.store (0.0f);
        }

        // lay the Hann grain around `centre` at output time e (never into samples already sent out)
        const int64_t firstOut = inCount;
        for (int i = -Lh; i <= Lh; ++i)
        {
            const int64_t o = e + i;
            if (o < firstOut) continue;
            const int64_t src = centre + i;
            if (src > inCount) break;                         // not heard yet (should not happen with this latency)
            const float w = 0.5f + 0.5f * std::cos ((float) kPi * (float) i / (float) Lh);
            const size_t ao = (size_t) (o & kAccMask);
            for (int c = 0; c < nc; ++c)
                acc[(size_t) c][ao] += w * (src < 0 ? 0.0f : hist[(size_t) c][(size_t) (src & kHistMask)]);
            wsum[ao] += w;
        }
        nextEpoch += spacing;
    }

    double sr = 48000.0;
    int chans = 2, lhMax = 608, puSamples = 288;
    va::PitchTracker tracker;
    std::array<std::vector<float>, 2> hist, acc;
    std::vector<float> mono, wsum;
    std::vector<Det> dets;
    std::vector<int64_t> marks;
    int64_t inCount = 0, lastMark = INT64_MIN;
    double nextEpoch = 0.0, corrCents = 0.0;
    int curNote = 0, octRun = 0;
    float prevHz = 0.0f;
    std::atomic<float> uiDetected { 0.0f }, uiTarget { 0.0f };
};
} // namespace vc
