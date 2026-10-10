// Phase 2 unit tests for the pitch tracker (strict document, Phase 2).
// Exit code 0 only if every check passes.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/PitchEngine.h"

#include <chrono>
#include <thread>
#include <cstdio>
#include <random>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (! ok) ++failures;
}

std::vector<float> sine (double hz, double sr, int n, double amp = 0.5)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
    return v;
}

std::vector<float> harmonicTone (double hz, double sr, int n)
{
    std::vector<float> v ((size_t) n, 0.0f);
    for (int k = 1; k <= 8; ++k)
        for (int i = 0; i < n; ++i)
            v[(size_t) i] += (float) (0.25 / k * std::sin (2.0 * juce::MathConstants<double>::pi * hz * k * i / sr));
    return v;
}

struct Result { int points = 0, voiced = 0; double worstCents = 0.0; };

Result run (va::PitchTracker& t, const std::vector<float>& x, double trueHz, int skipFrames = 8)
{
    Result r;
    int idx = 0;
    // Odd block size on purpose: blocks must not need to line up with the hop.
    for (size_t pos = 0; pos < x.size(); pos += 100)
    {
        const int n = (int) std::min<size_t> (100, x.size() - pos);
        t.process (x.data() + pos, n, [&] (const va::PitchPoint& p)
        {
            ++r.points;
            if (p.voiced)
            {
                ++r.voiced;
                if (idx >= skipFrames && trueHz > 0.0)
                    r.worstCents = std::max (r.worstCents, std::abs (va::hzToCents (p.hz, trueHz)));
            }
            ++idx;
        });
    }
    return r;
}
}

int main()
{
    const double sr = 48000.0;
    const int seconds = 1;

    for (auto method : { va::PitchTracker::Method::McLeod, va::PitchTracker::Method::Yin })
    {
        const bool mcleod = method == va::PitchTracker::Method::McLeod;
        std::printf ("\n== %s ==\n", mcleod ? "McLeod" : "YIN");
        va::PitchTracker t;
        t.prepare (sr);
        t.setMethod (method);

        for (double hz : { 110.0, 220.0, 440.0, 880.0, 123.47, 329.63, 1000.0 })
        {
            t.reset();
            auto r = run (t, sine (hz, sr, (int) sr * seconds), hz);
            char buf[160];
            std::snprintf (buf, sizeof buf, "sine %7.2f Hz: %d/%d frames voiced, worst error %.2f cents (limit 5)",
                           hz, r.voiced, r.points, r.worstCents);
            check (r.voiced > r.points - 10 && r.worstCents <= 5.0, buf);
        }

        for (double hz : { 130.81, 196.0, 261.63 })
        {
            t.reset();
            auto r = run (t, harmonicTone (hz, sr, (int) sr * seconds), hz);
            char buf[160];
            std::snprintf (buf, sizeof buf, "8-harmonic tone %7.2f Hz: %d/%d voiced, worst %.2f cents (limit 5)",
                           hz, r.voiced, r.points, r.worstCents);
            check (r.voiced > r.points - 10 && r.worstCents <= 5.0, buf);
        }

        {
            t.reset();
            auto r = run (t, std::vector<float> ((size_t) sr * seconds, 0.0f), 0.0);
            char buf[120];
            std::snprintf (buf, sizeof buf, "silence: %d voiced points out of %d (must be 0)", r.voiced, r.points);
            check (r.voiced == 0 && r.points > 0, buf);
        }

        for (float amp : { 0.5f, 0.05f, 0.9f })
        {
            std::mt19937 rng (4242);
            std::uniform_real_distribution<float> d (-amp, amp);
            std::vector<float> noise ((size_t) sr * 2);
            for (auto& s : noise) s = d (rng);
            t.reset();
            auto r = run (t, noise, 0.0);
            char buf[120];
            std::snprintf (buf, sizeof buf, "white noise amp %.2f: %d voiced points out of %d (must be 0)", (double) amp, r.voiced, r.points);
            check (r.voiced == 0 && r.points > 0, buf);
        }

        for (double hz : { 60.0, 1500.0 })
        {
            t.reset();
            auto r = run (t, sine (hz, sr, (int) sr * seconds), 0.0);
            char buf[120];
            std::snprintf (buf, sizeof buf, "out-of-range sine %.0f Hz: %d voiced (must be 0)", hz, r.voiced);
            check (r.voiced == 0, buf);
        }

        // 44.1 kHz host: same window in samples, frequency must still be right.
        {
            va::PitchTracker t441;
            t441.prepare (44100.0);
            t441.setMethod (method);
            auto r = run (t441, sine (220.0, 44100.0, 44100), 220.0);
            char buf[120];
            std::snprintf (buf, sizeof buf, "44.1 kHz sine 220 Hz: worst %.2f cents (limit 5)", r.worstCents);
            check (r.voiced > 0 && r.worstCents <= 5.0, buf);
        }

        // The gate is a user control: a stricter gate must reject a noisy tone.
        {
            std::mt19937 rng (7);
            std::uniform_real_distribution<float> d (-0.1f, 0.1f);
            auto x = sine (220.0, sr, (int) sr);
            for (auto& s : x) s += d (rng);
            t.reset();
            if (mcleod) t.setMcLeodGate (0.8f); else t.setYinGate (0.15f);
            auto loose = run (t, x, 0.0);
            t.reset();
            if (mcleod) t.setMcLeodGate (0.999f); else t.setYinGate (0.001f);
            auto strict = run (t, x, 0.0);
            if (mcleod) t.setMcLeodGate (va::PitchTracker::defaultMcLeodGate); else t.setYinGate (va::PitchTracker::defaultYinGate);
            char buf[140];
            std::snprintf (buf, sizeof buf, "voicing gate is adjustable: noisy tone voiced %d at default, %d at strict gate", loose.voiced, strict.voiced);
            check (strict.voiced < loose.voiced, buf);
        }

        // Cost per frame, for the phase report.
        {
            t.reset();
            auto x = sine (220.0, sr, (int) sr * 4);
            int frames = 0;
            auto t0 = std::chrono::steady_clock::now();
            t.process (x.data(), (int) x.size(), [&] (const va::PitchPoint&) { ++frames; });
            auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count();
            std::printf ("  info %d frames in %.1f ms = %.3f ms per frame (hop is 5.333 ms)\n", frames, ms, ms / frames);
            check (ms / frames < 5.333, "analysis is faster than real time");
        }
    }

    // Median filter: a single wrong frame between good ones must not get through.
    {
        va::PitchTracker t;
        t.prepare (sr);
        // 220 Hz, with a 1-frame blip is hard to build from audio, so test the
        // visible behaviour: a clean step 220 -> 233.08 Hz settles to the new
        // note and never reports anything outside the two notes.
        auto a = sine (220.0, sr, 24000), b = sine (233.08, sr, 24000);
        a.insert (a.end(), b.begin(), b.end());
        double lo = 1e9, hi = 0;
        t.process (a.data(), (int) a.size(), [&] (const va::PitchPoint& p)
        {
            if (p.voiced) { lo = std::min (lo, (double) p.hz); hi = std::max (hi, (double) p.hz); }
        });
        check (lo > 215.0 && hi < 238.0, "pitch step stays between the two notes (no wild values)");
    }

    // End to end through the threaded engine, paced like a real audio device:
    // 512-sample blocks at 48 kHz. Measures how long after a note starts the
    // first voiced point reaches the queue the UI reads from.
    for (auto method : { va::PitchTracker::Method::McLeod, va::PitchTracker::Method::Yin })
    {
        va::PitchEngine engine;
        engine.setMethod (method);
        engine.start (sr, 512);

        const int block = 512;
        juce::AudioBuffer<float> silent (1, block), tone (1, block);
        silent.clear();
        auto sig = sine (220.0, sr, block * 400);

        const double blockMs = 1000.0 * block / sr;
        auto next = std::chrono::steady_clock::now();
        auto pace = [&] { next += std::chrono::microseconds ((long long) (blockMs * 1000.0)); std::this_thread::sleep_until (next); };

        for (int i = 0; i < 20; ++i) { engine.pushAudio (silent); pace(); }   // 213 ms of silence

        const auto onset = std::chrono::steady_clock::now();
        double firstVoicedMs = -1.0;
        va::PitchPoint pts[64];
        for (int i = 0; i < 40 && firstVoicedMs < 0.0; ++i)
        {
            std::copy (sig.begin() + i * block, sig.begin() + (i + 1) * block, tone.getWritePointer (0));
            engine.pushAudio (tone);
            // The next block is not available yet, so the earliest an offline
            // reader could see this block is when it ends. Count from block start.
            pace();
            int n;
            while ((n = engine.popPoints (pts, 64)) > 0)
                for (int k = 0; k < n; ++k)
                    if (pts[k].voiced && firstVoicedMs < 0.0)
                        firstVoicedMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - onset).count();
        }
        engine.stop();
        char buf[200];
        std::snprintf (buf, sizeof buf, "%s: first voiced point %.1f ms after the note started (limit 80 ms incl. screen)",
                       method == va::PitchTracker::Method::McLeod ? "McLeod" : "YIN", firstVoicedMs);
        std::printf ("  info %s\n", buf);
        check (firstVoicedMs > 0.0 && firstVoicedMs < 60.0, "note onset reaches the point queue in under 60 ms");
        check (engine.getDroppedSamples() == 0, "no audio dropped on the way to the analysis thread");
    }

    // Latency compensation shifts a point in time and nothing else.
    {
        va::PitchPoint p;
        p.centreSample = 48000; p.hz = 220.0f; p.voiced = true;
        check (va::shiftedCentreSample (p, 0.0, 48000.0) == 48000, "latency 0 ms leaves the point where it is");
        check (va::shiftedCentreSample (p, 50.0, 48000.0) == 50400, "latency +50 ms moves the point 2400 samples later");
        check (va::shiftedCentreSample (p, -50.0, 48000.0) == 45600, "latency -50 ms moves the point 2400 samples earlier");
        check (p.hz == 220.0f, "latency shift does not touch the pitch value");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PITCH TESTS PASSED" : "PITCH TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
