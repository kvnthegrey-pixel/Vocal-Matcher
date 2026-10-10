#pragma once

#include <cmath>
#include <cstdint>

namespace va
{
/** The comparison sound: the reference pitch, a short gap, then your pitch.
    Pure maths, no audio device, so it can be tested. */
class ToneSequence
{
public:
    void start (double refHz, double sungHz, double lengthSec, double sampleRate)
    {
        f1 = refHz; f2 = sungHz; sr = sampleRate;
        toneLen = (int64_t) (lengthSec * sampleRate);
        gapLen = (int64_t) (0.2 * sampleRate);
        pos = 0; ph1 = ph2 = 0.0;
        active = true;
    }
    void stop() { active = false; }
    bool isActive() const { return active; }
    int64_t totalSamples() const { return toneLen * 2 + gapLen; }

    /** Fills mono samples; zeros when idle. */
    void render (float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            if (active)
            {
                if (pos < toneLen && f1 > 0.0)       s = tone (f1, ph1, pos);
                else if (pos >= toneLen + gapLen && pos < 2 * toneLen + gapLen && f2 > 0.0) s = tone (f2, ph2, pos - toneLen - gapLen);
                if (++pos >= totalSamples()) active = false;
            }
            out[i] = s;
        }
    }

private:
    float tone (double hz, double& phase, int64_t k) const
    {
        phase += 2.0 * 3.14159265358979323846 * hz / sr;
        const double fade = 0.015 * sr;
        double env = 1.0;
        if (k < fade) env = (double) k / fade;
        if (toneLen - k < fade) env = std::min (env, (double) (toneLen - k) / fade);
        // sine plus a quiet octave, a little warmer than a bare sine
        return (float) (0.28 * env * (std::sin (phase) + 0.25 * std::sin (2.0 * phase)));
    }

    double f1 = 0, f2 = 0, sr = 48000.0, ph1 = 0, ph2 = 0;
    int64_t toneLen = 0, gapLen = 0, pos = 0;
    bool active = false;
};
} // namespace va
