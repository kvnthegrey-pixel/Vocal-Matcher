// Mix Seat maths: profiles, the nudge, the plan, the reasons, saved text, and measuring real audio files.
// Exit code 0 only if every check passes.
#include "../Source/Seat/SeatEngine.h"

#include <cstdio>
#include <random>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("  %-4s %s\n", ok ? "ok" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

using ms::BandDb;
const double kPi = juce::MathConstants<double>::pi;

BandDb flat (float v = 0.0f) { BandDb d {}; d.fill (v); return d; }

/** Adds `db` to every band whose centre is within [lo, hi] Hz. */
void bump (BandDb& d, double lo, double hi, float db)
{
    for (int b = 0; b < va::numBands; ++b)
    {
        const double f = va::bandCentre (b);
        if (f >= lo && f <= hi) d[(size_t) b] += db;
    }
}

float at (const BandDb& d, double hz)
{
    int best = 0;
    for (int b = 0; b < va::numBands; ++b)
        if (std::abs (std::log (va::bandCentre (b) / hz)) < std::abs (std::log (va::bandCentre (best) / hz))) best = b;
    return d[(size_t) best];
}

/** Steady sound with equal energy in every third-octave band between lo and hi. */
std::vector<float> bandSignal (double sr, double seconds, double lo, double hi, float level, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> ph (0.0, 2.0 * kPi);
    std::vector<double> freqs, phases;
    for (double f = lo; f <= hi; f *= std::pow (2.0, 1.0 / 24.0)) { freqs.push_back (f); phases.push_back (ph (rng)); }
    std::vector<float> x ((size_t) (seconds * sr), 0.0f);
    const double amp = level / std::sqrt ((double) freqs.size());
    for (size_t k = 0; k < freqs.size(); ++k)
    {
        const double w = 2.0 * kPi * freqs[k] / sr;
        for (size_t i = 0; i < x.size(); ++i)
            x[i] += (float) (amp * std::sin (w * (double) i + phases[k]));
    }
    return x;
}

juce::File writeWav (const std::vector<float>& x, double sr, const char* name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> out (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), sr, 1, 16, {}, 0));
    out.release();
    const float* ch[1] = { x.data() };
    w->writeFromFloatArrays (ch, 1, (int) x.size());
    return f;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const ms::SeatSettings def;

    std::printf ("\n== Importance of each region ==\n");
    {
        bool inRange = true;
        for (int b = 0; b < va::numBands; ++b)
        {
            const float w = ms::importance (va::bandCentre (b));
            if (w <= 0.0f || w > 1.0f) inRange = false;
        }
        check (inRange, "every band has an importance between 0 and 1");
        check (ms::importance (2000.0) > ms::importance (100.0) && ms::importance (2000.0) > ms::importance (12000.0),
               "vocal clarity range matters more than sub-bass and air");
    }

    std::printf("\n== The nudge ==\n");
    {
        const auto neutral = ms::seatCurve (flat(), flat(), def);
        float biggest = 0.0f;
        for (float v : neutral) biggest = std::max (biggest, std::abs (v));
        check (biggest < 0.05f, "identical beat and vocal shape -> no change");

        BandDb beat = flat();
        bump (beat, 220.0, 420.0, 12.0f);                          // heavy low-mids in the beat
        const auto c = ms::seatCurve (beat, flat(), def);
        check (at (c, 300.0) <= -1.5f, "beat heavy at 300 Hz -> vocal is cut there");
        check (at (c, 300.0) >= -def.maxDb - 1.0e-4f, "the cut never exceeds the limit");
        check (at (c, 2500.0) > 0.1f, "...and the vocal is lifted where the beat has room (2.5 kHz)");
        float top = 0.0f;
        for (float v : c) top = std::max (top, v);
        check (top <= 0.6f * def.maxDb + 1.0e-4f, "no boost exceeds 60% of the limit");

        double m = 0.0, w = 0.0;
        for (int b = 0; b < va::numBands; ++b)
        {
            const double f = va::bandCentre (b);
            if (f < 150.0 || f > 6000.0) continue;
            m += c[(size_t) b] * ms::importance (f); w += ms::importance (f);
        }
        check (std::abs (m / w) < 0.35, "overall level stays where it was (weighted mean near 0 dB)");

        BandDb louder = beat;
        for (auto& v : louder) v += 20.0f;
        const auto c2 = ms::seatCurve (louder, flat(), def);
        float diff = 0.0f;
        for (size_t b = 0; b < c.size(); ++b) diff = std::max (diff, std::abs (c[b] - c2[b]));
        check (diff < 1.0e-4f, "a beat 20 dB louder gives the same curve (shape only, not level)");

        BandDb tilted = flat();
        for (int b = 0; b < va::numBands; ++b) tilted[(size_t) b] = -6.0f * (float) std::log2 (va::bandCentre (b) / 1000.0);
        const auto c4 = ms::seatCurve (beat, tilted, def);
        diff = 0.0f;
        for (size_t b = 0; b < c.size(); ++b) diff = std::max (diff, std::abs (c[b] - c4[b]));
        check (diff < 1.0e-3f, "a vocal with a natural -6 dB/octave slope gives the same curve (slope is not a problem to fix)");
        BandDb darkBeat = flat();
        for (int b = 0; b < va::numBands; ++b) darkBeat[(size_t) b] = -4.0f * (float) std::log2 (va::bandCentre (b) / 1000.0);
        const auto c5 = ms::seatCurve (darkBeat, flat(), def);
        float mx5 = 0.0f;
        for (float v : c5) mx5 = std::max (mx5, std::abs (v));
        check (mx5 < 0.05f, "a beat that is simply darker or brighter overall (even slope) -> no change");

        BandDb v2 = flat();
        for (auto& x : v2) x -= 15.0f;
        const auto c3 = ms::seatCurve (beat, v2, def);
        diff = 0.0f;
        for (size_t b = 0; b < c.size(); ++b) diff = std::max (diff, std::abs (c[b] - c3[b]));
        check (diff < 1.0e-4f, "a vocal 15 dB quieter gives the same curve");

        ms::SeatSettings off = def;
        off.amount = 0.0f;
        const auto c0 = ms::seatCurve (beat, flat(), off);
        float mx = 0.0f;
        for (float v : c0) mx = std::max (mx, std::abs (v));
        check (mx < 1.0e-4f, "amount 0 -> no change at all");

        ms::SeatSettings full = def;
        full.amount = 1.0f;
        const auto cf = ms::seatCurve (beat, flat(), full), ch = ms::seatCurve (beat, flat(), def);
        check (at (cf, 300.0) < at (ch, 300.0), "more amount -> deeper cut");

        ms::SeatSettings small = def;
        small.maxDb = 1.5f;
        const auto cs = ms::seatCurve (beat, flat(), small);
        float lowest = 0.0f, highest = 0.0f;
        for (float v : cs) { lowest = std::min (lowest, v); highest = std::max (highest, v); }
        check (lowest >= -1.5f - 1.0e-4f && highest <= 0.9f + 1.0e-4f, "max change 1.5 dB limits the cut to 1.5 dB and the boost to 0.9 dB");
    }

    std::printf("\n== Guards ==\n");
    {
        BandDb beat = flat();
        bump (beat, 2200.0, 4000.0, -12.0f);                      // a hole in the beat at 2.2-4 kHz
        const auto c = ms::seatCurve (beat, flat(), def);
        check (at (c, 3000.0) > 0.5f, "a hole in the beat -> boost your vocal into it");

        BandDb vocal = flat();
        bump (vocal, 2800.0, 3500.0, 12.0f);                      // ...but your vocal already sticks out there
        const auto g = ms::seatCurve (beat, vocal, def);
        check (at (g, 3150.0) < at (c, 3150.0) - 0.4f, "no extra boost into a band where the vocal already sticks out");

        BandDb rumble = flat();
        bump (rumble, 20.0, 90.0, -15.0f);
        const auto r = ms::seatCurve (rumble, flat(), def);
        check (at (r, 63.0) <= 1.0e-4f, "never boosts rumble below 80 Hz");

        BandDb air = flat();
        bump (air, 12500.0, 20000.0, -20.0f);
        const auto a = ms::seatCurve (air, flat(), ms::SeatSettings { 1.0f, 8.0f });
        check (at (a, 16000.0) <= 1.0f + 1.0e-4f, "boost above 12 kHz stays at or under 1 dB");
    }

    std::printf("\n== The plan ==\n");
    {
        ms::BeatProfile beat;
        beat.valid = true;
        beat.windowSec = 4.0;
        beat.average = flat();
        bump (beat.average, 220.0, 420.0, 6.0f);
        BandDb w0 = flat(), w1 = flat();
        bump (w0, 220.0, 420.0, 12.0f);                            // first part: low-mids busy
        bump (w1, 2200.0, 5000.0, 12.0f);                          // second part: presence busy
        beat.windows = { w0, w0, w1, w1 };
        beat.durationSec = 16.0;
        ms::VocalProfile vocal;
        vocal.valid = true; vocal.seconds = 10.0; vocal.average = flat();

        const auto plan = ms::buildPlan (beat, vocal, def, 48000.0, true);
        check (plan.valid && plan.hasWindows && plan.gains.size() == 4, "plan has one set of filter gains per window");
        check (! ms::buildPlan (beat, ms::VocalProfile(), def, 48000.0, true).valid, "no vocal yet -> no plan");
        check (! ms::buildPlan (ms::BeatProfile(), vocal, def, 48000.0, true).valid, "no beat yet -> no plan");
        check (! ms::buildPlan (beat, vocal, def, 48000.0, false).hasWindows, "whole-song plan skips the per-window work");

        // the 12 filters reproduce the curve closely enough
        double err = 0.0; int n = 0;
        for (int b = 0; b < va::numBands; ++b)
        {
            const double f = va::bandCentre (b);
            if (f < 100.0 || f > 10000.0) continue;
            const double resp = va::eqResponseDb (plan.staticGains, f, 48000.0);
            err += std::pow (resp - plan.staticCurve[(size_t) b], 2.0); ++n;
        }
        check (std::sqrt (err / n) < 1.0, "the 12 filters follow the whole-song curve within 1 dB (RMS)");

        const auto early = plan.gainsAt (3.0, true), late = plan.gainsAt (13.0, true), stat = plan.gainsAt (13.0, false);
        const double e300 = va::eqResponseDb (early, 300.0, 48000.0), l300 = va::eqResponseDb (late, 300.0, 48000.0);
        const double e3k = va::eqResponseDb (early, 3000.0, 48000.0), l3k = va::eqResponseDb (late, 3000.0, 48000.0);
        check (e300 < -1.0 && l300 > e300 + 1.0, "following the beat: cut at 300 Hz early, released later");
        check (l3k < -1.0 && e3k > l3k + 1.0, "following the beat: cut at 3 kHz later, not early");
        check (stat == plan.staticGains, "follow off -> the whole-song gains at any time");
        const auto mid = plan.gainsAt (8.0, true);
        const double m300 = va::eqResponseDb (mid, 300.0, 48000.0);
        check (m300 > std::min (e300, l300) - 0.01 && m300 < std::max (e300, l300) + 0.01, "between two windows the gains are blended, not jumped");
        check (plan.gainsAt (-5.0, true) == plan.gains.front() && plan.gainsAt (999.0, true) == plan.gains.back(), "before the start and after the end hold the first and last window");
    }

    std::printf("\n== Reasons in plain language ==\n");
    {
        BandDb c = flat (0.0f);
        bump (c, 220.0, 420.0, -3.2f);
        bump (c, 2200.0, 4000.0, 2.1f);
        bump (c, 7000.0, 7500.0, 0.3f);                           // too small to mention
        const auto moves = ms::explain (c);
        check (moves.size() == 2, "two moves worth mentioning (the tiny one is left out)");
        if (moves.size() == 2)
        {
            check (moves[0].db < 0.0f && moves[0].text.rfind ("Cut", 0) == 0, "largest move first, and it is a cut");
            check (moves[0].text.find ("low-mids") != std::string::npos, "the cut names the region (low-mids)");
            check (moves[1].text.rfind ("Boost", 0) == 0 && moves[1].text.find ("room") != std::string::npos, "the boost says the beat leaves room");
        }
        check (ms::explain (flat (0.0f)).empty(), "flat curve -> nothing to explain");
        BandDb many = flat (0.0f);
        for (int b = 0; b < va::numBands; b += 4) many[(size_t) b] = (b % 8 == 0) ? -3.0f : 3.0f;
        check (ms::explain (many, 3).size() <= 3, "never more moves than asked for");
    }

    std::printf("\n== Saved text ==\n");
    {
        ms::BeatProfile b;
        b.valid = true; b.name = "my beat v2.wav"; b.durationSec = 187.25; b.windowSec = 4.0;
        for (int i = 0; i < 5; ++i)
        {
            BandDb d {};
            for (int k = 0; k < va::numBands; ++k) d[(size_t) k] = -40.0f + (float) k * 0.5f + (float) i;
            b.windows.push_back (d);
        }
        b.average = b.windows[2];
        ms::BeatProfile r;
        check (r.fromText (b.toText()), "beat text loads back");
        check (r.name == b.name && r.windows.size() == 5 && std::abs (r.durationSec - 187.25) < 0.01, "name, duration and windows survive");
        float worst = 0.0f;
        for (size_t i = 0; i < 5; ++i) for (size_t k = 0; k < (size_t) va::numBands; ++k) worst = std::max (worst, std::abs (r.windows[i][k] - b.windows[i][k]));
        check (worst < 0.001f, "band values survive to within 0.001 dB");
        check (! ms::BeatProfile().fromText ("nonsense"), "garbage is rejected");
        check (! ms::BeatProfile().fromText ("B1\nname\n10|4|1\nA|1|2|3\n"), "a truncated beat is rejected");
        {
            auto t = b.toText();
            t.replace (t.find ("-39.5"), 5, "nan");
            check (! ms::BeatProfile().fromText (t), "a NaN value is rejected");
        }
        const auto at2 = b.at (6.0);
        check (std::abs (at2[0] - b.windows[1][0]) < 0.6f || std::abs (at2[0] - b.windows[2][0]) < 0.6f, "a time in the beat finds its window");

        ms::VocalProfile v;
        v.valid = true; v.seconds = 12.5; v.average = b.windows[3];
        ms::VocalProfile vr;
        check (vr.fromText (v.toText()) && std::abs (vr.seconds - 12.5) < 0.01 && std::abs (vr.average[7] - v.average[7]) < 0.001f, "vocal text round-trips");
        check (! ms::VocalProfile().fromText ("V1\n3|1|2"), "a truncated vocal is rejected");
    }

    std::printf("\n== Measuring real audio files ==\n");
    {
        const double sr = 44100.0;
        // 12 s beat: 0-4 s low-mid heavy, 4-8 s balanced, 8-12 s bright
        std::vector<float> beat;
        auto low = bandSignal (sr, 4.0, 100.0, 500.0, 0.4f, 1);
        auto mid = bandSignal (sr, 4.0, 100.0, 8000.0, 0.4f, 2);
        auto high = bandSignal (sr, 4.0, 2500.0, 9000.0, 0.4f, 3);
        beat.insert (beat.end(), low.begin(), low.end());
        beat.insert (beat.end(), mid.begin(), mid.end());
        beat.insert (beat.end(), high.begin(), high.end());
        const auto file = writeWav (beat, sr, "ms_core_beat.wav");

        ms::BeatProfile p;
        juce::String err;
        const bool ok = ms::analyseBeatFile (file, p, err, [] (float) { return true; });
        check (ok && p.valid, "a 12 s beat is measured");
        check (std::abs (p.durationSec - 12.0) < 0.05 && p.windows.size() == 3, "12 s -> three 4 s windows");
        if (ok && p.windows.size() == 3)
        {
            check (at (p.windows[0], 250.0) > at (p.windows[0], 4000.0) + 10.0f, "first window is heavy in the low-mids");
            check (at (p.windows[2], 4000.0) > at (p.windows[2], 250.0) + 10.0f, "last window is heavy in the highs");
            check (at (p.average, 250.0) > at (p.average, 20000.0), "the average has both");
        }
        bool cancelledOk = ! ms::analyseBeatFile (file, p, err, [] (float) { return false; });
        check (cancelledOk, "cancelling stops the analysis");
        check (! ms::analyseBeatFile (juce::File ("/nonexistent/none.wav"), p, err, [] (float) { return true; }) && err.isNotEmpty(), "a missing file gives a message");
        file.deleteFile();

        std::vector<float> quiet (44100, 0.0f);
        const auto qf = writeWav (quiet, sr, "ms_core_quiet.wav");
        check (! ms::analyseBeatFile (qf, p, err, [] (float) { return true; }) && err.contains ("too quiet"), "a silent file says it is too quiet");
        qf.deleteFile();

        // vocal-like: harmonic series, 6 s
        std::vector<float> voice ((size_t) (6.0 * sr));
        for (size_t i = 0; i < voice.size(); ++i)
        {
            const double t = (double) i / sr;
            double s = 0.0;
            for (int h = 1; h <= 25; ++h) s += std::sin (2.0 * kPi * 220.0 * h * t) / h;
            voice[i] = (float) (0.2 * s);
        }
        const auto vf = writeWav (voice, sr, "ms_core_voice.wav");
        ms::VocalProfile vp;
        check (ms::analyseVocalFile (vf, vp, err, [] (float) { return true; }) && vp.valid && vp.seconds > 4.0, "a 6 s vocal file is measured");
        vf.deleteFile();
        const auto shortf = writeWav (std::vector<float> (voice.begin(), voice.begin() + (long) (1.5 * sr)), sr, "ms_core_short.wav");
        check (! ms::analyseVocalFile (shortf, vp, err, [] (float) { return true; }) && err.isNotEmpty(), "a 1.5 s vocal is too short to trust");
        shortf.deleteFile();
    }

    std::printf("\n== Learning the vocal while it plays ==\n");
    {
        const double sr = 48000.0;
        ms::VocalLearner learner (sr);
        const auto sig = bandSignal (sr, 8.0, 150.0, 6000.0, 0.3f, 5);
        bool everValid = false;
        double validAt = -1.0;
        for (size_t i = 0; i < sig.size(); i += 512)
        {
            if (learner.add (sig.data() + i, (int) std::min<size_t> (512, sig.size() - i)) && learner.profile().valid && ! everValid)
            {
                everValid = true;
                validAt = (double) i / sr;
            }
        }
        check (everValid, "the learner becomes ready");
        check (validAt >= 2.5 && validAt <= 5.0, "...after about 3 s of singing, not before (" + juce::String (validAt, 1) + " s)");
        check (learner.profile().seconds > 6.0, "it keeps counting singing time");
        ms::VocalLearner silent (sr);
        std::vector<float> zeros (48000 * 6, 0.0f);
        for (size_t i = 0; i < zeros.size(); i += 512) silent.add (zeros.data() + i, 512);
        check (! silent.profile().valid, "silence never counts as a vocal");
        learner.reset (44100.0);
        check (learner.profile().valid, "a sample-rate change keeps the learned profile");
        learner.clear();
        check (! learner.profile().valid, "forgetting the vocal clears it");
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
