// PATCHED for Vocal Assistor (Phase 2). See third_party/NOTICE.txt.
// Original: adamski/pitch_detector PitchMPM (McLeod Pitch Method, FFT based).
//
// What changed against the original:
//  - The original used the raw autocorrelation where McLeod specifies the
//    normalised square difference function (NSDF). That is the source of the
//    bias seen at low pitches. The NSDF is now computed properly
//    (2 * r(tau) / m'(tau)), using the library's own FFT autocorrelation.
//  - Nothing is allocated after construction. The original built several
//    std::vectors on every call, which must not happen on an audio thread.
//  - The peak height of the chosen peak is returned as a clarity value, so a
//    voicing gate can be built on it.
//  - The hard-coded cutoffs and the 80 Hz limit are settings.
//  - Search range is limited to [minHz, maxHz].

class PitchMPM
{
public:
    PitchMPM (size_t bufferSizeIn) : PitchMPM (44100, bufferSizeIn) {}

    PitchMPM (int sampleRateIn, size_t bufferSizeIn)
        : bufferSize (bufferSizeIn),
          fftSize (2 * bufferSizeIn),
          sampleRate ((float) sampleRateIn),
          real (audiofft::AudioFFT::ComplexSize (2 * bufferSizeIn)),
          imag (audiofft::AudioFFT::ComplexSize (2 * bufferSizeIn)),
          padded (2 * bufferSizeIn, 0.0f),
          acf (2 * bufferSizeIn, 0.0f),
          nsdf (bufferSizeIn, 0.0f)
    {
        fft.init (fftSize);
        peakPositions.reserve (bufferSizeIn);
        updateTauRange();
    }

    /** Finds the pitch of one window of exactly bufferSize samples.
        Returns true and fills hz and clarity (0..1, the NSDF peak height) when
        a peak was found. Does not allocate. */
    bool analyse (const float* audioBuffer, float& hz, float& clarity) noexcept
    {
        hz = 0.0f;
        clarity = 0.0f;
        if (audioBuffer == nullptr)
            return false;

        computeNsdf (audioBuffer);

        // Collect the highest peak of every positive region of the NSDF.
        peakPositions.clear();
        const int last = (int) std::min<size_t> ((size_t) (tauMax + 1), bufferSize - 2);
        int pos = 1;
        // Skip the lobe around lag 0.
        while (pos < last && nsdf[(size_t) pos] > 0.0f)
            ++pos;
        int curMax = 0;
        while (pos < last)
        {
            const float v = nsdf[(size_t) pos];
            if (v <= 0.0f)
            {
                if (curMax > 0) { peakPositions.push_back (curMax); curMax = 0; }
            }
            else if (v > nsdf[(size_t) pos - 1] && v >= nsdf[(size_t) pos + 1])
            {
                if (curMax == 0 || v > nsdf[(size_t) curMax])
                    curMax = pos;
            }
            ++pos;
        }
        if (curMax > 0)
            peakPositions.push_back (curMax);

        float highest = -1.0f;
        for (int tau : peakPositions)
            if (tau >= tauMin)
                highest = std::max (highest, nsdf[(size_t) tau]);

        if (highest < smallCutoff)
            return false;

        const float threshold = cutoff * highest;
        for (int tau : peakPositions)
        {
            if (tau < tauMin || nsdf[(size_t) tau] < smallCutoff || nsdf[(size_t) tau] < threshold)
                continue;

            const float a = nsdf[(size_t) tau - 1], b = nsdf[(size_t) tau], c = nsdf[(size_t) tau + 1];
            const float den = a + c - 2.0f * b;
            float x = (float) tau, y = b;
            if (den != 0.0f)
            {
                const float delta = a - c;
                x += delta / (2.0f * den);
                y = b - delta * delta / (8.0f * den);
            }
            if (x <= 0.0f)
                return false;
            hz = sampleRate / x;
            clarity = std::min (y, 1.0f);
            return hz >= minHz && hz <= maxHz;
        }
        return false;
    }

    /** Original API: frequency in Hz, or -1 when nothing was found. */
    float getPitch (const float* audioBuffer) noexcept
    {
        float hz, clarity;
        return analyse (audioBuffer, hz, clarity) ? hz : -1.0f;
    }

    void setSampleRate (int newSampleRate) { sampleRate = (float) newSampleRate; updateTauRange(); }
    void setRange (float newMinHz, float newMaxHz) { minHz = newMinHz; maxHz = newMaxHz; updateTauRange(); }
    void setCutoff (float c) { cutoff = c; }
    void setSmallCutoff (float c) { smallCutoff = c; }

private:
    void updateTauRange() noexcept
    {
        // Search up to one octave above maxHz so that a tone just out of range is
        // found at its true pitch and then discarded, rather than being reported
        // an octave low.
        tauMin = std::max (2, (int) std::floor (sampleRate / (2.0f * maxHz)) - 1);
        tauMax = std::min ((int) bufferSize - 3, (int) std::ceil (sampleRate / minHz) + 1);
    }

    void computeNsdf (const float* x) noexcept
    {
        const size_t n = bufferSize;
        std::copy (x, x + n, padded.begin());
        std::fill (padded.begin() + (std::ptrdiff_t) n, padded.end(), 0.0f);

        fft.fft (padded.data(), real.data(), imag.data());
        for (size_t i = 0; i < real.size(); ++i)
        {
            real[i] = real[i] * real[i] + imag[i] * imag[i];
            imag[i] = 0.0f;
        }
        fft.ifft (acf.data(), real.data(), imag.data());

        // Work in double for the running denominator.
        double sumSq = 0.0;
        for (size_t i = 0; i < n; ++i)
            sumSq += (double) x[i] * (double) x[i];

        if (sumSq <= 1.0e-12 || acf[0] <= 0.0f)
        {
            std::fill (nsdf.begin(), nsdf.end(), 0.0f);
            return;
        }

        // The FFT wrapper's scaling is not relied on: r(0) must equal sumSq.
        const double scale = sumSq / (double) acf[0];
        double m = 2.0 * sumSq;
        nsdf[0] = 1.0f;
        for (size_t tau = 1; tau < n; ++tau)
        {
            m -= (double) x[tau - 1] * x[tau - 1] + (double) x[n - tau] * x[n - tau];
            nsdf[tau] = (m > 1.0e-12) ? (float) (2.0 * scale * (double) acf[tau] / m) : 0.0f;
        }
    }

    size_t bufferSize;
    size_t fftSize;
    float sampleRate;

    audiofft::AudioFFT fft;
    std::vector<float> real, imag, padded, acf, nsdf;
    std::vector<int> peakPositions;

    float cutoff = 0.93f;       // original CUTOFF
    float smallCutoff = 0.5f;   // original SMALL_CUTOFF
    float minHz = 80.0f;        // original LOWER_PITCH_CUTOFF
    float maxHz = 1200.0f;
    int tauMin = 2, tauMax = 2;
};
