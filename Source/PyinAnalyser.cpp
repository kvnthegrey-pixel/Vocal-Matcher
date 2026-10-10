#include "PyinAnalyser.h"
#include <audio_fft/audio_fft.h>
#include <cmath>
#include <limits>

namespace va
{
namespace
{
struct Candidate { float hz; float prob; float clarity; };

double betaPdf (double x, double a, double b)
{
    return std::pow (x, a - 1.0) * std::pow (1.0 - x, b - 1.0);   // unnormalised; weights are normalised later
}
}

PyinAnalyser::PyinAnalyser (double sampleRate, PyinSettings s) : sr (sampleRate), cfg (s) {}

bool PyinAnalyser::analyse (const std::vector<float>& mono, const std::function<bool (float)>& progress, PyinResult& out) const
{
    const int N = cfg.frameSize, hop = cfg.hopSize;
    const int tauMax = std::min (N / 2, (int) std::ceil (sr / cfg.minHz) + 1);
    const int tauMin = std::max (2, (int) std::floor (sr / cfg.maxHz) - 1);
    const int W = N - tauMax - 1;
    const int M = 2 * N;

    const int numBins = (int) std::floor (1200.0 * std::log2 (cfg.maxHz / cfg.minHz) / cfg.centsPerBin) + 1;
    const int U = numBins;                      // index of the single unvoiced state
    const int J = cfg.maxJumpBins;

    // Threshold prior.
    std::vector<double> thr, wgt;
    {
        double sum = 0.0;
        for (int k = 1; k <= 50; ++k)
        {
            const double s = 0.02 * k;
            thr.push_back (s);
            wgt.push_back (betaPdf (std::min (s, 0.999), cfg.betaA, cfg.betaB));
            sum += wgt.back();
        }
        for (auto& w : wgt) w /= sum;
    }

    const size_t numFrames = mono.size() < 1 ? 0 : (size_t) (mono.size() / (size_t) hop) + 1;
    out = PyinResult {};
    out.track.hopSec = (double) hop / sr;
    out.track.firstCentreSec = 0.0;
    out.track.midi.assign (numFrames, 0.0f);
    out.track.clarity.assign (numFrames, 0.0f);
    if (numFrames == 0) return true;

    audiofft::AudioFFT fft;
    fft.init ((size_t) M);
    std::vector<float> a ((size_t) M), b ((size_t) M), ar (audiofft::AudioFFT::ComplexSize ((size_t) M)),
        ai (ar.size()), br (ar.size()), bi (ar.size()), rr ((size_t) M);
    std::vector<float> frame ((size_t) N);
    std::vector<double> cum ((size_t) N + 1), cmnd ((size_t) tauMax + 2);

    std::vector<std::vector<Candidate>> cands (numFrames);
    std::vector<float> frameRms (numFrames, 0.0f);

    // ---- 1. candidates for every frame -------------------------------------
    for (size_t f = 0; f < numFrames; ++f)
    {
        if ((f & 255u) == 0 && ! progress (0.6f * (float) f / (float) numFrames))
            return false;

        const long centre = (long) f * hop;
        const long start = centre - N / 2;
        for (int i = 0; i < N; ++i)
        {
            const long p = start + i;
            frame[(size_t) i] = (p >= 0 && p < (long) mono.size()) ? mono[(size_t) p] : 0.0f;
        }

        cum[0] = 0.0;
        for (int i = 0; i < N; ++i)
            cum[(size_t) i + 1] = cum[(size_t) i] + (double) frame[(size_t) i] * frame[(size_t) i];
        const double energy = cum[(size_t) W];
        frameRms[f] = (float) std::sqrt (energy / W);
        if (energy < 1.0e-9 * W)            // digital silence
            continue;

        std::fill (a.begin(), a.end(), 0.0f);
        std::fill (b.begin(), b.end(), 0.0f);
        std::copy (frame.begin(), frame.begin() + W, a.begin());
        std::copy (frame.begin(), frame.end(), b.begin());
        fft.fft (a.data(), ar.data(), ai.data());
        fft.fft (b.data(), br.data(), bi.data());
        for (size_t k = 0; k < ar.size(); ++k)
        {
            const float re = ar[k] * br[k] + ai[k] * bi[k];     // conj(A) * B
            const float im = ar[k] * bi[k] - ai[k] * br[k];
            ar[k] = re; ai[k] = im;
        }
        fft.ifft (rr.data(), ar.data(), ai.data());
        const double scale = rr[0] > 0.0f ? energy / (double) rr[0] : 1.0;   // r(0) must equal the window energy

        cmnd[0] = 1.0;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const double e2 = cum[(size_t) (tau + W)] - cum[(size_t) tau];
            double d = energy + e2 - 2.0 * scale * (double) rr[(size_t) tau];
            if (d < 0.0) d = 0.0;
            running += d;
            cmnd[(size_t) tau] = running > 1.0e-12 ? d * tau / running : 1.0;
        }
        cmnd[(size_t) tauMax + 1] = 1.0;

        std::vector<Candidate> found;
        for (size_t k = 0; k < thr.size(); ++k)
        {
            int tau = -1;
            for (int t = tauMin; t < tauMax; ++t)
                if (cmnd[(size_t) t] < thr[k])
                {
                    while (t + 1 < tauMax && cmnd[(size_t) t + 1] < cmnd[(size_t) t]) ++t;
                    tau = t;
                    break;
                }
            if (tau < 0) continue;

            // Parabolic refinement of the lag.
            const double c0 = cmnd[(size_t) tau - 1], c1 = cmnd[(size_t) tau], c2 = cmnd[(size_t) tau + 1];
            const double den = c0 - 2.0 * c1 + c2;
            double x = tau;
            if (std::abs (den) > 1.0e-12) x += 0.5 * (c0 - c2) / den;
            const float hz = (float) (sr / x);
            if (hz < cfg.minHz || hz > cfg.maxHz) continue;

            bool merged = false;
            for (auto& c : found)
                if (std::abs (c.hz - hz) < 1.0e-3f * hz) { c.prob += (float) wgt[k]; merged = true; break; }
            if (! merged)
                found.push_back ({ hz, (float) wgt[k], (float) std::max (0.0, 1.0 - c1) });
        }
        cands[f] = std::move (found);
    }

    // ---- 2. Viterbi over voiced pitch bins plus one unvoiced state ----------
    const double logStayV = std::log (1.0 - cfg.switchProb), logSwitch = std::log (cfg.switchProb);
    // As in the published pYIN, the unvoiced probability is shared over one state
    // per pitch bin, so an unvoiced frame only wins when no pitch candidate has
    // any real weight. Entering a voiced state costs the same as a move inside it.
    const double logUV = std::log (cfg.switchProb / (2 * cfg.maxJumpBins + 1));
    std::vector<double> logTri ((size_t) J + 1);
    {
        double norm = 0.0;
        for (int d = -J; d <= J; ++d) norm += (J + 1 - std::abs (d));
        for (int d = 0; d <= J; ++d) logTri[(size_t) d] = logStayV + std::log ((J + 1 - d) / norm);
    }

    auto binOf = [&] (float hz) { return (int) std::lround (1200.0 * std::log2 ((double) hz / cfg.minHz) / cfg.centsPerBin); };

    std::vector<double> delta ((size_t) numBins + 1), next ((size_t) numBins + 1), obs ((size_t) numBins + 1);
    std::vector<int16_t> back (numFrames * (size_t) (numBins + 1));
    const double neg = -1.0e30;

    std::fill (delta.begin(), delta.end(), 0.0);
    for (size_t f = 0; f < numFrames; ++f)
    {
        if ((f & 255u) == 0 && ! progress (0.6f + 0.35f * (float) f / (float) numFrames))
            return false;

        std::fill (obs.begin(), obs.end(), 1.0e-6);
        double voicedMass = 0.0;
        for (auto& c : cands[f])
        {
            const int bin = binOf (c.hz);
            if (bin < 0 || bin >= numBins) continue;
            obs[(size_t) bin] += c.prob;
            voicedMass += c.prob;
        }
        obs[(size_t) U] = std::max ((1.0 - std::min (voicedMass, 1.0)) / numBins, 1.0e-9);

        int16_t* bp = &back[f * (size_t) (numBins + 1)];

        // Best predecessor for the unvoiced state.
        {
            double best = delta[(size_t) U] + logStayV;
            int arg = U;
            // from any voiced state: use the best voiced delta.
            double bestV = neg; int argV = 0;
            for (int s = 0; s < numBins; ++s)
                if (delta[(size_t) s] > bestV) { bestV = delta[(size_t) s]; argV = s; }
            if (bestV + logSwitch > best) { best = bestV + logSwitch; arg = argV; }
            next[(size_t) U] = best + std::log (obs[(size_t) U]);
            bp[U] = (int16_t) arg;
        }

        const double fromU = delta[(size_t) U] + logUV;
        for (int s = 0; s < numBins; ++s)
        {
            double best = fromU;
            int arg = U;
            const int lo = std::max (0, s - J), hi = std::min (numBins - 1, s + J);
            for (int p = lo; p <= hi; ++p)
            {
                const double v = delta[(size_t) p] + logTri[(size_t) std::abs (p - s)];
                if (v > best) { best = v; arg = p; }
            }
            next[(size_t) s] = best + std::log (obs[(size_t) s]);
            bp[s] = (int16_t) arg;
        }
        delta.swap (next);
        // Keep the numbers small.
        double mx = *std::max_element (delta.begin(), delta.end());
        for (auto& v : delta) v -= mx;
    }

    // Trace back.
    std::vector<int> path (numFrames);
    {
        int s = (int) (std::max_element (delta.begin(), delta.end()) - delta.begin());
        for (size_t f = numFrames; f-- > 0;)
        {
            path[f] = s;
            s = back[f * (size_t) (numBins + 1) + (size_t) s];
        }
    }

    // ---- 3. frame track and quality numbers ---------------------------------
    size_t voiced = 0, pairs = 0, jumps = 0;
    double clarSum = 0.0;
    float prevMidi = 0.0f;
    for (size_t f = 0; f < numFrames; ++f)
    {
        if (path[f] == U) { prevMidi = 0.0f; continue; }
        const int bin = path[f];
        const float binHz = (float) (cfg.minHz * std::pow (2.0, (double) bin * cfg.centsPerBin / 1200.0));
        float hz = binHz, clar = 0.0f;
        float bestDist = 1.0e9f;
        for (auto& c : cands[f])
            if (binOf (c.hz) == bin && std::abs (c.hz - binHz) < bestDist) { hz = c.hz; clar = c.clarity; bestDist = std::abs (c.hz - binHz); }
        const float midi = (float) (69.0 + 12.0 * std::log2 ((double) hz / 440.0));
        out.track.midi[f] = midi;
        out.track.clarity[f] = clar;
        ++voiced;
        clarSum += clar;
        if (prevMidi > 0.0f)
        {
            ++pairs;
            if (std::abs (midi - prevMidi) > 7.0f) ++jumps;
        }
        prevMidi = midi;
    }
    // Voiced share of the frames that are actually loud enough to be singing
    // (more than 20 dB under the loud frames), so rests and silence are not held
    // against the file.
    std::vector<float> sorted (frameRms);
    std::sort (sorted.begin(), sorted.end());
    const float loud = sorted[(size_t) ((double) (sorted.size() - 1) * 0.95)];
    size_t active = 0, activeVoiced = 0;
    for (size_t f = 0; f < numFrames; ++f)
        if (frameRms[f] >= 0.1f * loud && loud > 0.0f)
        {
            ++active;
            if (path[f] != U) ++activeVoiced;
        }
    out.voicedFraction = active ? (double) activeVoiced / (double) active : 0.0;
    out.meanClarity = voiced ? clarSum / (double) voiced : 0.0;
    out.jumpRate = pairs ? (double) jumps / (double) pairs : 0.0;
    progress (1.0f);
    return true;
}
} // namespace va
