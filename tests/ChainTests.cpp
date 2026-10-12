// Vocal Chain DSP tests: compressor curve, pitch correction accuracy and latency,
// reverb decay / stability / ducking, and the whole chain. Exit code 0 only if all pass.
#include "../Source/Chain/ChainEngine.h"

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

constexpr double SR = 48000.0;

juce::AudioBuffer<float> stereo (const std::vector<float>& x)
{
    juce::AudioBuffer<float> b (2, (int) x.size());
    for (int c = 0; c < 2; ++c) std::copy (x.begin(), x.end(), b.getWritePointer (c));
    return b;
}

// run a long signal through a processor in blocks of 512
template <typename Fn>
std::vector<float> runBlocks (const std::vector<float>& in, Fn&& fn)
{
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); i += 512)
    {
        const int n = (int) std::min<size_t> (512, in.size() - i);
        juce::AudioBuffer<float> b (2, n);
        for (int c = 0; c < 2; ++c) std::copy (in.begin() + (std::ptrdiff_t) i, in.begin() + (std::ptrdiff_t) i + n, b.getWritePointer (c));
        fn (b);
        std::copy (b.getReadPointer (0), b.getReadPointer (0) + n, out.begin() + (std::ptrdiff_t) i);
    }
    return out;
}

// a voice-like tone: harmonics with a falling spectrum
std::vector<float> voice (double hz, double seconds, double vibratoCents = 0.0, double vibHz = 5.5)
{
    std::vector<float> v ((size_t) (seconds * SR));
    double ph = 0.0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = (double) i / SR;
        const double f = hz * std::pow (2.0, vibratoCents * std::sin (2 * vc::kPi * vibHz * t) / 1200.0);
        ph += 2 * vc::kPi * f / SR;
        double s = 0;
        for (int h = 1; h <= 10; ++h) s += std::sin (h * ph) / h;
        v[i] = (float) (0.3 * s);
    }
    return v;
}

// pitch of a stretch of audio by normalised autocorrelation (fine, parabolic)
double pitchOf (const std::vector<float>& x, size_t from, size_t len, double lo = 80.0, double hi = 1000.0)
{
    const int minLag = (int) (SR / hi), maxLag = (int) (SR / lo);
    double best = -1; int bestLag = minLag;
    std::vector<double> r ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double num = 0, e1 = 0, e2 = 0;
        for (size_t i = from; i + (size_t) lag < from + len; ++i) { num += x[i] * x[i + (size_t) lag]; e1 += x[i] * x[i]; e2 += x[i + (size_t) lag] * x[i + (size_t) lag]; }
        r[(size_t) lag] = num / std::sqrt (e1 * e2 + 1e-12);
    }
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (r[(size_t) lag] > best) { best = r[(size_t) lag]; bestLag = lag; }
    for (int lag = minLag; lag < bestLag; ++lag)            // prefer the shortest period that is nearly as good (not a multiple of it)
        if (r[(size_t) lag] >= 0.97 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1]) { bestLag = lag; break; }
    const double a = r[(size_t) bestLag - 1], b = r[(size_t) bestLag], c = r[(size_t) bestLag + 1];
    const double d = (a - 2 * b + c);
    const double shift = std::abs (d) > 1e-12 ? 0.5 * (a - c) / d : 0.0;
    return SR / ((double) bestLag + shift);
}
double centsOf (double hz, double ref) { return 1200.0 * std::log2 (hz / ref); }
double rms (const std::vector<float>& x, size_t from, size_t len)
{ double s = 0; for (size_t i = from; i < from + len; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / (double) len); }
}

int main()
{
    std::printf ("\n== Compressor ==\n");
    {
        vc::Compressor comp;
        comp.prepare (SR, 2);
        vc::Compressor::Params p;
        p.thresholdDb = -20.0f; p.ratio = 4.0f; p.attackMs = 0.1f; p.releaseMs = 50.0f; p.kneeDb = 0.0f; p.makeupDb = 0.0f;
        std::vector<float> sine ((size_t) (SR * 1.0));
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.3981f * std::sin (2.0f * 3.14159265f * 1000.0f * (float) i / 48000.0f);   // -8 dBFS peak
        auto out = runBlocks (sine, [&] (juce::AudioBuffer<float>& b) { comp.process (b, p); });
        double peak = 0; for (size_t i = (size_t) (SR * 0.5); i < out.size(); ++i) peak = std::max (peak, (double) std::abs (out[i]));
        const double outDb = 20 * std::log10 (peak);
        std::printf ("       -8 dB in, threshold -20, 4:1 -> %.2f dB out (expected -17)\n", outDb);
        check (std::abs (outDb - (-17.0)) < 0.8, "static curve: -8 dB peak through -20 dB / 4:1 comes out at about -17 dB");

        std::vector<float> quiet ((size_t) SR, 0.0f);
        for (size_t i = 0; i < quiet.size(); ++i) quiet[i] = 0.03f * std::sin (2.0f * 3.14159265f * 1000.0f * (float) i / 48000.0f);
        comp.prepare (SR, 2);
        auto q = runBlocks (quiet, [&] (juce::AudioBuffer<float>& b) { comp.process (b, p); });
        double qpk = 0; for (size_t i = 4800; i < q.size(); ++i) qpk = std::max (qpk, (double) std::abs (q[i]));
        check (std::abs (qpk - 0.03) < 0.001, "a signal under the threshold passes untouched");

        // release: loud burst then quiet, gain reduction fades back
        comp.prepare (SR, 2);
        std::vector<float> burst ((size_t) (SR * 0.6), 0.0f);
        for (size_t i = 0; i < burst.size(); ++i)
            burst[i] = (i < (size_t) (SR * 0.2) ? 0.8f : 0.01f) * std::sin (2.0f * 3.14159265f * 440.0f * (float) i / 48000.0f);
        auto bo = runBlocks (burst, [&] (juce::AudioBuffer<float>& b) { comp.process (b, p); });
        const double early = rms (bo, (size_t) (SR * 0.21), 2400) / rms (burst, (size_t) (SR * 0.21), 2400);
        const double late  = rms (bo, (size_t) (SR * 0.55), 2400) / rms (burst, (size_t) (SR * 0.55), 2400);
        check (early < 0.7 && late > 0.95, "after the loud part the gain comes back up (release)");
    }

    std::printf ("\n== Pitch correction ==\n");
    {
        auto runTune = [&] (const std::vector<float>& in, const vc::PitchCorrector::Params& tp)
        {
            vc::PitchCorrector t; t.prepare (SR, 2);
            return runBlocks (in, [&] (juce::AudioBuffer<float>& b) { t.process (b, tp); });
        };
        vc::PitchCorrector::Params hard; hard.speedMs = 0.0f; hard.amount = 1.0f; hard.scale = 0; hard.keyRoot = 0;

        {   // A4 sung 30 cents flat -> 440 Hz
            const double fl = 440.0 * std::pow (2.0, -30.0 / 1200.0);
            auto in = voice (fl, 2.5);
            auto out = runTune (in, hard);
            const double before = centsOf (pitchOf (in, 60000, 9600), 440.0), after = centsOf (pitchOf (out, 60000, 9600), 440.0);
            std::printf ("       30 cents flat: %.1f cents before, %.1f after\n", before, after);
            check (std::abs (after) < 5.0, "hard tune: 30 cents flat comes out within 5 cents of A4");
            const double lvl = rms (out, 60000, 24000) / rms (in, 60000, 24000);
            check (lvl > 0.85 && lvl < 1.2, "the level stays within 1.5 dB");
        }
        {   // 40 cents sharp of E4 -> E4
            const double e4 = 329.6276;
            auto in = voice (e4 * std::pow (2.0, 40.0 / 1200.0), 2.5);
            auto out = runTune (in, hard);
            const double after = centsOf (pitchOf (out, 60000, 9600), e4);
            std::printf ("       E4+40: in %.1f cents, out %.1f cents\n", centsOf (pitchOf (in, 60000, 9600), e4), after);
            check (std::abs (after) < 5.0, "hard tune: 40 cents sharp of E4 comes out at E4");
        }
        {   // half amount
            const double fl = 440.0 * std::pow (2.0, -30.0 / 1200.0);
            auto p = hard; p.amount = 0.5f;
            auto out = runTune (voice (fl, 2.5), p);
            const double after = centsOf (pitchOf (out, 60000, 9600), 440.0);
            check (std::abs (after - (-15.0)) < 5.0, "amount 50%: moves half way (about -15 cents)");
        }
        {   // key and scale: in C major, C#4 snaps to C4 or D4, never stays at C#
            const double cs4 = 277.18;
            auto p = hard; p.scale = 1;
            auto out = runTune (voice (cs4 * 1.003, 2.5), p);
            const double hz = pitchOf (out, 60000, 9600);
            std::printf ("       C#4 in C major -> %.2f Hz\n", hz);
            const double toC = std::abs (centsOf (hz, 261.6256)), toD = std::abs (centsOf (hz, 293.6648));
            check (std::min (toC, toD) < 8.0, "C major: a C#4 is pulled onto C4 or D4");
        }
        {   // vibrato is flattened by hard tune, kept by slow retune
            const double a4 = 440.0;
            auto in = voice (a4, 3.0, 40.0);
            auto outHard = runTune (in, hard);
            auto slow = hard; slow.speedMs = 400.0f; slow.amount = 1.0f;
            auto outSlow = runTune (in, slow);
            auto spread = [&] (const std::vector<float>& x)
            {
                double lo = 1e9, hi = -1e9;
                for (size_t s = 72000; s + 3000 < 130000; s += 1500) { const double c = centsOf (pitchOf (x, s, 2400), a4); lo = std::min (lo, c); hi = std::max (hi, c); }
                return hi - lo;
            };
            const double sIn = spread (in), sHard = spread (outHard), sSlow = spread (outSlow);
            std::printf ("       vibrato spread: input %.0f, hard %.0f, slow %.0f cents\n", sIn, sHard, sSlow);
            check (sHard < sIn * 0.5, "hard tune flattens a +/-40 cent vibrato");
            check (sSlow > sHard + 8.0, "slow retune keeps more of the vibrato than hard tune");
        }
        {   // disabled = a plain delay of exactly kLatency samples
            std::mt19937 rng (3);
            std::normal_distribution<float> nd (0.0f, 0.1f);
            std::vector<float> in ((size_t) SR * 2);
            for (auto& s : in) s = nd (rng);
            auto p = hard; p.enabled = false;
            auto out = runTune (in, p);
            double worst = 0;
            for (size_t i = 8000; i < in.size() - 3000; ++i) worst = std::max (worst, (double) std::abs (out[i + 2048] - in[i]));
            std::printf ("       bypassed path vs delayed input: worst difference %.2e\n", worst);
            check (worst < 1.0e-4, "with correction off the output is the input delayed by exactly 2048 samples");
        }
        {   // unvoiced noise and silence stay finite and quiet
            std::vector<float> in ((size_t) SR, 0.0f);
            auto out = runTune (in, hard);
            bool ok = true; for (float s : out) if (! std::isfinite (s) || std::abs (s) > 1e-6f) ok = false;
            check (ok, "silence stays silence");
        }
    }

    std::printf ("\n== Reverb ==\n");
    {
        auto impulse = [&] (vc::PlateReverb::Params p, double seconds, std::vector<float>& L, std::vector<float>& R)
        {
            vc::PlateReverb r; r.prepare (SR);
            p.mix = 1.0f; p.duck = 0.0f; p.shimmer = 0.0f;
            const size_t n = (size_t) (seconds * SR);
            L.assign (n, 0.0f); R.assign (n, 0.0f);
            for (size_t i = 0; i < n; i += 512)
            {
                const int m = (int) std::min<size_t> (512, n - i);
                juce::AudioBuffer<float> b (2, m); b.clear();
                if (i == 0) { b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); }
                r.process (b, p);
                if (i == 0) { b.setSample (0, 0, b.getSample (0, 0) - 1.0f); b.setSample (1, 0, b.getSample (1, 0) - 1.0f); }     // wet only
                std::copy (b.getReadPointer (0), b.getReadPointer (0) + m, L.begin() + (std::ptrdiff_t) i);
                std::copy (b.getReadPointer (1), b.getReadPointer (1) + m, R.begin() + (std::ptrdiff_t) i);
            }
        };
        // measured RT60 from a Schroeder backward integration (T20 extrapolated)
        auto rt60 = [&] (const std::vector<float>& L)
        {
            std::vector<double> e (L.size());
            double s = 0; for (size_t i = L.size(); i-- > 0;) { s += (double) L[i] * L[i]; e[i] = s; }
            auto db = [&] (size_t i) { return 10.0 * std::log10 (e[i] / e[0] + 1e-30); };
            size_t i5 = 0, i25 = 0;
            for (size_t i = 0; i < e.size(); ++i) { if (! i5 && db (i) < -5.0) i5 = i; if (! i25 && db (i) < -25.0) { i25 = i; break; } }
            return i25 > i5 ? 3.0 * (double) (i25 - i5) / SR : 0.0;
        };
        for (float target : { 1.0f, 2.2f, 4.0f })
        {
            vc::PlateReverb::Params q; q.decaySec = target; q.dampingHz = 16000.0f; q.lowCutHz = 20.0f; q.toneDb = 0.0f; q.preDelayMs = 0.0f;
            std::vector<float> L, R; impulse (q, 9.0, L, R);
            const double m = rt60 (L);
            std::printf ("       decay setting %.1f s -> measured RT60 %.2f s\n", target, m);
            check (m > target * 0.6 && m < target * 1.6, "decay " + juce::String (target, 1) + " s: measured within 0.6x..1.6x of the setting");
        }
        {
            vc::PlateReverb::Params q; q.preDelayMs = 40.0f; q.lowCutHz = 20.0f;
            std::vector<float> L, R; impulse (q, 1.0, L, R);
            size_t first = 0; while (first < L.size() && std::abs (L[first]) < 1e-5f && std::abs (R[first]) < 1e-5f) ++first;
            std::printf ("       pre-delay 40 ms: first sound at %.1f ms\n", 1000.0 * (double) first / SR);
            check (first > (size_t) (0.038 * SR) && first < (size_t) (0.075 * SR), "pre-delay: nothing before 40 ms, the first reflections right after");
        }
        {
            vc::PlateReverb::Params q; q.width = 1.0f;
            std::vector<float> L, R; impulse (q, 3.0, L, R);
            double sl = 0, sr2 = 0, sx = 0;
            for (size_t i = (size_t) (0.3 * SR); i < L.size(); ++i) { sl += (double) L[i] * L[i]; sr2 += (double) R[i] * R[i]; sx += (double) L[i] * R[i]; }
            const double corr = sx / std::sqrt (sl * sr2 + 1e-30);
            std::printf ("       left/right correlation of the tail %.2f\n", corr);
            check (std::abs (corr) < 0.5, "the tail is wide: left and right are not the same signal");
            q.width = 0.0f;
            impulse (q, 3.0, L, R);
            double d = 0; for (size_t i = 0; i < L.size(); ++i) d = std::max (d, (double) std::abs (L[i] - R[i]));
            check (d < 1e-4, "width 0 collapses the reverb to mono");
        }
        {   // never blows up, even at the extremes
            vc::PlateReverb r; r.prepare (SR);
            vc::PlateReverb::Params q; q.decaySec = 10.0f; q.shimmer = 1.0f; q.modulation = 1.0f; q.mix = 1.0f; q.duck = 0.0f; q.dampingHz = 16000.0f; q.preDelayMs = 200.0f;
            std::mt19937 rng (1); std::normal_distribution<float> nd (0.0f, 0.25f);
            float peak = 0; bool finite = true;
            for (int blk = 0; blk < (int) (20 * SR / 512); ++blk)
            {
                juce::AudioBuffer<float> b (2, 512);
                for (int i = 0; i < 512; ++i) { const float v = nd (rng); b.setSample (0, i, v); b.setSample (1, i, v); }
                r.process (b, q);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { const float v = b.getSample (c, i); if (! std::isfinite (v)) finite = false; peak = std::max (peak, std::abs (v)); }
            }
            std::printf ("       extreme settings, 20 s of noise: peak %.2f\n", peak);
            check (finite && peak < 8.0f, "extreme settings stay stable (decay 10 s, shimmer 100%, mod 100%)");
        }
        {   // ducking: reverb is quieter while the dry vocal is loud
            auto wetLevel = [&] (float duck)
            {
                vc::PlateReverb r; r.prepare (SR);
                vc::PlateReverb::Params q; q.mix = 1.0f; q.duck = duck; q.lowCutHz = 20.0f;
                auto in = voice (330.0, 3.0);
                double e = 0; size_t cnt = 0;
                for (size_t i = 0; i < in.size(); i += 512)
                {
                    const int m = (int) std::min<size_t> (512, in.size() - i);
                    juce::AudioBuffer<float> b (2, m);
                    for (int c = 0; c < 2; ++c) std::copy (in.begin() + (std::ptrdiff_t) i, in.begin() + (std::ptrdiff_t) i + m, b.getWritePointer (c));
                    juce::AudioBuffer<float> dry; dry.makeCopyOf (b);
                    r.process (b, q);
                    if (i > (size_t) SR)
                        for (int k = 0; k < m; ++k) { const double w = b.getSample (0, k) - dry.getSample (0, k); e += w * w; ++cnt; }
                }
                return std::sqrt (e / (double) cnt);
            };
            const double none = wetLevel (0.0f), full = wetLevel (1.0f);
            std::printf ("       wet level without duck %.4f, with full duck %.4f\n", none, full);
            check (full < none * 0.5, "ducking tucks the reverb under a loud vocal");
        }
    }

    std::printf ("\n== Whole chain ==\n");
    {
        vc::ChainEngine eng; eng.prepare (SR, 512);
        vc::ChainParams p;
        std::mt19937 rng (9); std::normal_distribution<float> nd (0.0f, 0.3f);
        bool finite = true; float peak = 0;
        auto in = voice (330.0, 6.0, 25.0);
        for (size_t i = 0; i < in.size(); i += 512)
        {
            const int m = (int) std::min<size_t> (512, in.size() - i);
            juce::AudioBuffer<float> b (2, m);
            for (int c = 0; c < 2; ++c) for (int k = 0; k < m; ++k) b.setSample (c, k, in[i + (size_t) k] * 2.5f + 0.02f * nd (rng));   // loud, plus a little noise
            eng.process (b, p);
            for (int c = 0; c < 2; ++c) for (int k = 0; k < m; ++k) { const float v = b.getSample (c, k); if (! std::isfinite (v)) finite = false; peak = std::max (peak, std::abs (v)); }
        }
        std::printf ("       default settings on a hot vocal: peak %.3f, compressor reduction %.1f dB\n", peak, eng.gainReductionDb());
        check (finite, "output is finite");
        check (peak <= 1.0f, "the safety ceiling keeps the output at or below 0 dBFS");
        check (vc::ChainEngine::kLatencySamples == 2048, "reported latency is 2048 samples");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL CHAIN TESTS PASSED" : "CHAIN TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
