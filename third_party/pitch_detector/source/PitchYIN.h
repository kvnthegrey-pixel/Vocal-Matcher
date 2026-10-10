// PATCHED for Vocal Assistor (Phase 2). See third_party/NOTICE.txt.
// Original: adamski/pitch_detector PitchYIN (based on aubio's YIN).
//
// What changed against the original:
//  - The uninitialised Logger pointer that was dereferenced in getPitchInHz is gone,
//    as is the DBG call inside the loop.
//  - The difference function no longer reads past the end of the window. The
//    integration length is bufferSize - maxTau.
//  - Nothing is allocated after construction; AudioSampleBuffer is replaced by
//    std::vector.
//  - The search is limited to [minHz, maxHz], so the cost per window is bounded.
//  - The CMND value at the chosen lag is returned, so a voicing gate can use it.
//    Lower is more periodic, 0.15 is the usual gate.

class PitchYIN
{
public:
    PitchYIN (int sampleRateIn, unsigned int bufferSizeIn)
        : bufferSize (bufferSizeIn), sampleRate ((float) sampleRateIn), cmnd (bufferSizeIn, 1.0f)
    {
        updateTauRange();
    }

    PitchYIN (unsigned int bufferSizeIn) : PitchYIN (44100, bufferSizeIn) {}

    /** Analyses one window of exactly bufferSize samples. Returns true and fills
        hz and cmndValue when a lag was found (cmndValue is the CMND at that lag). */
    bool analyse (const float* x, float& hz, float& cmndValue) noexcept
    {
        hz = 0.0f;
        cmndValue = 1.0f;
        if (x == nullptr)
            return false;

        const int w = (int) bufferSize - tauMax - 1;   // integration length
        if (w <= 0)
            return false;

        cmnd[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < w; ++j)
            {
                const double diff = (double) x[j] - (double) x[j + tau];
                d += diff * diff;
            }
            running += d;
            cmnd[(size_t) tau] = running > 1.0e-12 ? (float) (d * tau / running) : 1.0f;
        }

        // First lag under the threshold, followed down to its local minimum.
        int best = -1;
        for (int tau = std::max (tauMin, 2); tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }
        // Otherwise the global minimum, which will normally fail the gate.
        if (best < 0)
        {
            float lowest = 2.0f;
            for (int tau = std::max (tauMin, 2); tau < tauMax; ++tau)
                if (cmnd[(size_t) tau] < lowest) { lowest = cmnd[(size_t) tau]; best = tau; }
            if (best < 0)
                return false;
        }

        const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
        const float den = a - 2.0f * b + c;
        float x0 = (float) best, y = b;
        if (den != 0.0f)
        {
            x0 += 0.5f * (a - c) / den;
            y = b - (a - c) * (a - c) / (8.0f * den);
        }
        if (x0 <= 0.0f)
            return false;
        hz = sampleRate / x0;
        cmndValue = std::max (0.0f, y);
        return hz >= minHz && hz <= maxHz;
    }

    /** Original API: frequency in Hz, or 0 when nothing was found. */
    float getPitchInHz (const float* x) noexcept
    {
        float hz, v;
        return analyse (x, hz, v) ? hz : 0.0f;
    }

    void setSampleRate (unsigned int s) { sampleRate = (float) s; updateTauRange(); }
    void setTolerence (float t) { threshold = t; }
    void setRange (float newMinHz, float newMaxHz) { minHz = newMinHz; maxHz = newMaxHz; updateTauRange(); }

private:
    void updateTauRange() noexcept
    {
        // Search one octave above maxHz so an out-of-range tone is discarded
        // instead of being reported an octave low.
        tauMin = std::max (2, (int) std::floor (sampleRate / (2.0f * maxHz)) - 1);
        tauMax = std::min ((int) bufferSize / 2, (int) std::ceil (sampleRate / minHz) + 1);
    }

    unsigned int bufferSize;
    float sampleRate;
    std::vector<float> cmnd;
    float threshold = 0.15f;
    float minHz = 80.0f, maxHz = 1200.0f;
    int tauMin = 2, tauMax = 2;
};
