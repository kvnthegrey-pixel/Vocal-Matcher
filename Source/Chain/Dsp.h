#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vc
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

/** One-pole smoother for gains and mixes so knob moves do not click. */
struct Smooth
{
    float v = 0.0f, target = 0.0f, k = 0.002f;
    void prepare (double sr, float ms) { k = 1.0f - std::exp (-1.0f / (ms * 0.001f * (float) sr)); }
    void snap (float x) { v = target = x; }
    float next() { v += k * (target - v); return v; }
};

/** RBJ biquad, transposed direct form II, double precision state. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }

    void lowPass (double sr, double f, double q)
    {
        const double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highPass (double sr, double f, double q)
    {
        const double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandPass (double sr, double f, double q)
    {
        const double w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peaking (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void highShelf (double sr, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double beta = 2 * std::sqrt (A) * (s / 2 * std::sqrt (2.0));       // slope 1
        set (A * ((A + 1) + (A - 1) * c + beta), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - beta),
             (A + 1) - (A - 1) * c + beta, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - beta);
    }
    void lowShelf (double sr, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double beta = 2 * std::sqrt (A) * (s / 2 * std::sqrt (2.0));
        set (A * ((A + 1) - (A - 1) * c + beta), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - beta),
             (A + 1) + (A - 1) * c + beta, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - beta);
    }
};

/** Circular delay. Read before write: read (d) is the input from d samples ago. */
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void init (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void write (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float read (int d) const { return buf[(size_t) ((w - d) & mask)]; }
    float readFrac (float d) const
    {
        const int i = (int) d;
        const float f = d - (float) i;
        const float a = read (i), b = read (i + 1);
        return a + (b - a) * f;
    }
};

/** Schroeder all-pass: w[n] = x[n] + g w[n-N], y = -g w[n] + w[n-N]. */
struct AllPass
{
    DelayLine d;
    int len = 1;
    float g = 0.5f;

    void init (int length, float gain) { len = std::max (1, length); g = gain; d.init (len + 64); }
    float process (float x)
    {
        const float wd = d.read (len);
        const float w = x + g * wd;
        d.write (w);
        return -g * w + wd;
    }
    float processMod (float x, float lenF)
    {
        const float wd = d.readFrac (lenF);
        const float w = x + g * wd;
        d.write (w);
        return -g * w + wd;
    }
};

/** Gentle safety ceiling: transparent below 0.8, never above 1.0. */
inline float softCeiling (float x)
{
    const float a = std::abs (x);
    if (a < 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
    return x < 0.0f ? -y : y;
}
} // namespace vc
