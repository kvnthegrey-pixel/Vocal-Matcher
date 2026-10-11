#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

namespace va
{
/** Tone matching: measure how bright, dark, boxy or thin a voice is as a
    long-term average spectrum in 25 third-octave bands, compare your voice to
    the reference, and turn the difference into a gentle 12-band EQ. This
    matches tonal balance only (not compression, reverb or the character of
    the voice itself). */
constexpr int numBands = 25;          // 63 Hz .. 16 kHz, third octaves
constexpr int numEqBands = 12;        // the filters that do the work

inline double bandCentre (int i) { return 63.0 * std::pow (2.0, (double) i / 3.0); }
using BandDb = std::array<float, (size_t) numBands>;

inline constexpr std::array<double, (size_t) numEqBands> eqCentres { 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000, 14000 };

/** Long-term average spectrum of voiced / loud material. Feed it mono samples. */
class BandSpectrum
{
public:
    static constexpr int order = 12, size = 1 << order, hop = size / 2;

    explicit BandSpectrum (double sampleRate, size_t maxFrames = 40000)
        : fft (order), sr (sampleRate), cap (maxFrames), window ((size_t) size), ring ((size_t) size, 0.0f),
          work ((size_t) size * 2, 0.0f)
    {
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) size);
        for (int b = 0; b < numBands; ++b)
        {
            const double lo = bandCentre (b) * std::pow (2.0, -1.0 / 6.0), hi = bandCentre (b) * std::pow (2.0, 1.0 / 6.0);
            lowBin[(size_t) b] = std::max (1, (int) std::ceil (lo * size / sr));
            highBin[(size_t) b] = std::min (size / 2, std::max (lowBin[(size_t) b] + 1, (int) std::ceil (hi * size / sr)));
        }
    }

    void clear() { frames.clear(); rms.clear(); filled = 0; sinceHop = 0; std::fill (ring.begin(), ring.end(), 0.0f); }

    void add (const float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) writePos] = x[i];
            writePos = (writePos + 1) % size;
            if (filled < size) ++filled;
            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                if (filled >= size) frame();
            }
        }
    }

    size_t numFrames() const { return frames.size(); }
    double seconds() const { return (double) frames.size() * hop / sr; }

    /** Average spectrum in dB over the frames that count (loud enough: within
        25 dB of the 90th-percentile level, and above -55 dBFS). False if fewer than ~1 s qualify. */
    bool result (BandDb& out) const
    {
        if (frames.empty()) return false;
        std::vector<float> r = rms;
        const size_t k = (size_t) ((double) r.size() * 0.9);
        std::nth_element (r.begin(), r.begin() + (std::ptrdiff_t) std::min (k, r.size() - 1), r.end());
        const float p90 = r[std::min (k, r.size() - 1)];
        const float gate = std::max (0.00178f, p90 * 0.0562f);        // -55 dBFS, or p90 - 25 dB
        std::array<double, (size_t) numBands> sum {};
        size_t used = 0;
        for (size_t f = 0; f < frames.size(); ++f)
        {
            if (rms[f] < gate) continue;
            for (size_t b = 0; b < (size_t) numBands; ++b) sum[b] += frames[f][b];
            ++used;
        }
        if ((double) used * hop / sr < 1.0) return false;
        for (size_t b = 0; b < (size_t) numBands; ++b)
            out[b] = (float) (10.0 * std::log10 (sum[b] / (double) used + 1.0e-20));
        return true;
    }

private:
    void frame()
    {
        double sumSq = 0.0;
        for (int i = 0; i < size; ++i)
        {
            const float s = ring[(size_t) ((writePos + i) % size)];
            sumSq += (double) s * s;
            work[(size_t) i] = s * window[(size_t) i];
        }
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data());          // magnitudes
        std::array<float, (size_t) numBands> p {};
        for (int b = 0; b < numBands; ++b)
        {
            double e = 0.0;
            const int lo = lowBin[(size_t) b], hi = highBin[(size_t) b];
            for (int k = lo; k < hi; ++k) e += (double) work[(size_t) k] * work[(size_t) k];
            p[(size_t) b] = (float) (e / (hi - lo));
        }
        if (frames.size() >= cap) { frames.erase (frames.begin(), frames.begin() + (std::ptrdiff_t) cap / 10); rms.erase (rms.begin(), rms.begin() + (std::ptrdiff_t) cap / 10); }
        frames.push_back (p);
        rms.push_back ((float) std::sqrt (sumSq / size));
    }

    juce::dsp::FFT fft;
    double sr;
    size_t cap;
    std::vector<float> window, ring, work;
    std::array<int, (size_t) numBands> lowBin {}, highBin {};
    std::vector<std::array<float, (size_t) numBands>> frames;
    std::vector<float> rms;
    int writePos = 0, filled = 0, sinceHop = 0;
};

/** Decodes a file and measures its long-term spectrum. Blocking: call from a background thread. */
bool analyseToneFile (const juce::File& file, BandDb& out, juce::String& error, const std::atomic<bool>& cancel);

/** The EQ curve (dB per band) that would make `mine` sound like `ref` in tonal balance. */
inline BandDb computeCorrection (const BandDb& ref, const BandDb& mine)
{
    BandDb d {};
    double mean = 0.0; int n = 0;
    for (int b = 0; b < numBands; ++b)
    {
        d[(size_t) b] = ref[(size_t) b] - mine[(size_t) b];
        const double f = bandCentre (b);
        if (f >= 100.0 && f <= 8000.0) { mean += d[(size_t) b]; ++n; }
    }
    mean = n > 0 ? mean / n : 0.0;                 // overall loudness differences are not EQ
    for (auto& v : d) v -= (float) mean;

    for (int pass = 0; pass < 2; ++pass)           // smooth across neighbouring bands
    {
        BandDb s = d;
        for (int b = 0; b < numBands; ++b)
        {
            const float a = d[(size_t) std::max (0, b - 1)], c = d[(size_t) std::min (numBands - 1, b + 1)];
            s[(size_t) b] = 0.25f * a + 0.5f * d[(size_t) b] + 0.25f * c;
        }
        d = s;
    }
    for (int b = 0; b < numBands; ++b)
    {
        const double f = bandCentre (b);
        const float lim = (f < 100.0 || f > 10000.0) ? 6.0f : 12.0f;
        d[(size_t) b] = std::clamp (d[(size_t) b], -lim, lim);
        if (f <= 80.0) d[(size_t) b] = std::min (d[(size_t) b], 0.0f);        // never boost rumble
    }
    return d;
}

/** Correction in dB at an arbitrary frequency (log-frequency interpolation). */
inline float curveAt (const BandDb& c, double hz)
{
    const double pos = std::clamp (3.0 * std::log2 (hz / 63.0), 0.0, (double) numBands - 1.0);
    const int i = (int) std::floor (pos), j = std::min (numBands - 1, i + 1);
    const float t = (float) (pos - i);
    return c[(size_t) i] * (1.0f - t) + c[(size_t) j] * t;
}

/** Combined response (dB) of the twelve peaking filters (Q 1.4) at one frequency. */
inline double eqResponseDb (const std::array<float, (size_t) numEqBands>& gains, double hz, double sr)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    double db = 0.0;
    for (size_t b = 0; b < (size_t) numEqBands; ++b)
    {
        if (std::abs (gains[b]) < 0.005f) continue;
        const double A = std::pow (10.0, (double) gains[b] / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * eqCentres[b] / sr;
        const double alpha = std::sin (w0) / (2.0 * 1.4), c = std::cos (w0);
        const double a0 = 1.0 + alpha / A;
        const std::complex<double> num = (1.0 + alpha * A) / a0 + (-2.0 * c / a0) * z1 + ((1.0 - alpha * A) / a0) * z2;
        const std::complex<double> den = 1.0 + (-2.0 * c / a0) * z1 + ((1.0 - alpha / A) / a0) * z2;
        db += 20.0 * std::log10 (std::abs (num) / std::abs (den));
    }
    return db;
}

/** Filter gains whose COMBINED response follows the correction curve. Neighbouring
    filters overlap, so setting each gain to the curve value would overshoot; this
    solves for the gains (ridge-regularised least squares, then two refinements). */
constexpr size_t N = (size_t) numEqBands;      // namespace scope: MSVC will not use a local constant inside the lambda below

inline std::array<float, (size_t) numEqBands> eqGainsFor (const BandDb& corr, double sr)
{
    std::array<double, N> t {};
    for (size_t j = 0; j < N; ++j) t[j] = curveAt (corr, eqCentres[j]);

    // slope matrix: response at centre j for 1 dB on filter i
    double H[N][N];
    for (size_t i = 0; i < N; ++i)
    {
        std::array<float, N> unit {}; unit[i] = 1.0f;
        for (size_t j = 0; j < N; ++j) H[j][i] = eqResponseDb (unit, eqCentres[j], sr);
    }
    auto solve = [&] (const std::array<double, N>& target)
    {
        double A[N][N + 1];
        for (size_t r = 0; r < N; ++r)
        {
            for (size_t c = 0; c < N; ++c)
            {
                double s = 0.0;
                for (size_t k = 0; k < N; ++k) s += H[k][r] * H[k][c];
                A[r][c] = s + (r == c ? 0.05 : 0.0);
            }
            double s = 0.0;
            for (size_t k = 0; k < N; ++k) s += H[k][r] * target[k];
            A[r][N] = s;
        }
        for (size_t p = 0; p < N; ++p)
        {
            size_t best = p;
            for (size_t r = p + 1; r < N; ++r) if (std::abs (A[r][p]) > std::abs (A[best][p])) best = r;
            for (size_t c = 0; c <= N; ++c) std::swap (A[p][c], A[best][c]);
            for (size_t r = p + 1; r < N; ++r)
            {
                const double f = A[r][p] / A[p][p];
                for (size_t c = p; c <= N; ++c) A[r][c] -= f * A[p][c];
            }
        }
        std::array<double, N> x {};
        for (size_t r = N; r-- > 0;)
        {
            double s = A[r][N];
            for (size_t c = r + 1; c < N; ++c) s -= A[r][c] * x[c];
            x[r] = s / A[r][r];
        }
        return x;
    };

    std::array<double, N> g = solve (t);
    for (int it = 0; it < 2; ++it)
    {
        std::array<float, N> gf {};
        for (size_t i = 0; i < N; ++i) gf[i] = (float) std::clamp (g[i], -18.0, 18.0);
        std::array<double, N> resid {};
        for (size_t j = 0; j < N; ++j) resid[j] = t[j] - eqResponseDb (gf, eqCentres[j], sr);
        const auto d = solve (resid);
        for (size_t i = 0; i < N; ++i) g[i] += d[i];
    }
    std::array<float, N> out {};
    for (size_t i = 0; i < N; ++i) out[i] = (float) std::clamp (g[i], -18.0, 18.0);
    return out;
}

/** Twelve peaking filters, minimum phase, no latency. Gains ramp between blocks
    so changes do not click. When every gain is back at 0 and EQ is off it is skipped completely. */
class EqFilterBank
{
public:
    void prepare (double sampleRate, int channels)
    {
        sr = sampleRate;
        state.assign ((size_t) std::max (channels, 1), {});
        for (auto& g : current) g = 0.0f;
        recalc();
    }

    /** Message thread: where the gains should go. */
    void setTargets (const std::array<float, (size_t) numEqBands>& g) { for (size_t i = 0; i < g.size(); ++i) target[i].store (g[i]); }

    /** Audio thread. Returns true if it changed the audio. */
    bool process (juce::AudioBuffer<float>& buf)
    {
        bool moving = false, any = false;
        for (size_t i = 0; i < (size_t) numEqBands; ++i)
        {
            const float t = target[i].load();
            if (std::abs (t - current[i]) > 0.005f) { current[i] += 0.25f * (t - current[i]); moving = true; }
            else current[i] = t;
            if (std::abs (current[i]) > 0.005f) any = true;
        }
        if (moving) recalc();
        if (! any) return false;

        const int chans = std::min (buf.getNumChannels(), (int) state.size());
        for (int c = 0; c < chans; ++c)
        {
            float* d = buf.getWritePointer (c);
            auto& st = state[(size_t) c];
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                double x = d[i];
                for (size_t b = 0; b < (size_t) numEqBands; ++b)
                {
                    if (! active[b]) continue;
                    const auto& k = coef[b];
                    const double y = k.b0 * x + st[b].z1;
                    st[b].z1 = k.b1 * x - k.a1 * y + st[b].z2;
                    st[b].z2 = k.b2 * x - k.a2 * y;
                    x = y;
                }
                d[i] = (float) x;
            }
        }
        return true;
    }

private:
    struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct Z { double z1 = 0, z2 = 0; };

    void recalc()
    {
        for (size_t b = 0; b < (size_t) numEqBands; ++b)
        {
            active[b] = std::abs (current[b]) > 0.005f;
            if (! active[b]) { coef[b] = {}; continue; }
            const double A = std::pow (10.0, (double) current[b] / 40.0);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * eqCentres[b] / sr;
            const double alpha = std::sin (w0) / (2.0 * 1.4);
            const double a0 = 1.0 + alpha / A;
            coef[b] = { (1.0 + alpha * A) / a0, -2.0 * std::cos (w0) / a0, (1.0 - alpha * A) / a0,
                        -2.0 * std::cos (w0) / a0, (1.0 - alpha / A) / a0 };
        }
    }

    double sr = 48000.0;
    std::array<std::atomic<float>, (size_t) numEqBands> target {};
    std::array<float, (size_t) numEqBands> current {};
    std::array<Coef, (size_t) numEqBands> coef {};
    std::array<bool, (size_t) numEqBands> active {};
    std::vector<std::array<Z, (size_t) numEqBands>> state;
};
} // namespace va
