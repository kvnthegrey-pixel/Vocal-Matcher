#pragma once

#include "Dsp.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace vc
{
/** Feed-forward, stereo-linked, soft-knee compressor working in the log domain.
    Peak detector behind an 80 Hz high-pass so low rumble does not pump it. */
class Compressor
{
public:
    struct Params
    {
        float thresholdDb = -20.0f, ratio = 3.0f, attackMs = 10.0f, releaseMs = 120.0f, kneeDb = 6.0f, makeupDb = 0.0f;
    };

    void prepare (double sampleRate, int channels)
    {
        sr = sampleRate;
        for (auto& f : sidechainHp) { f.highPass (sr, 80.0, 0.707); f.reset(); }
        (void) channels;
        grDb = 0.0f;
    }

    void process (juce::AudioBuffer<float>& b, const Params& p)
    {
        const int n = b.getNumSamples(), chans = std::min (b.getNumChannels(), 2);
        const float att = std::exp (-1.0f / (std::max (0.05f, p.attackMs) * 0.001f * (float) sr));
        const float rel = std::exp (-1.0f / (std::max (1.0f, p.releaseMs) * 0.001f * (float) sr));
        const float slope = 1.0f / std::max (1.0f, p.ratio) - 1.0f;
        const float makeup = dbToGain (p.makeupDb);
        float worst = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < chans; ++c)
                peak = std::max (peak, std::abs (sidechainHp[(size_t) c].process (b.getReadPointer (c)[i])));
            const float level = gainToDb (peak);
            const float over = level - p.thresholdDb;

            float target;                                   // wanted gain change in dB, never above 0
            if (2.0f * over < -p.kneeDb)       target = 0.0f;
            else if (2.0f * std::abs (over) <= p.kneeDb)
            {
                const float x = over + p.kneeDb * 0.5f;
                target = slope * x * x / (2.0f * std::max (0.01f, p.kneeDb));
            }
            else                               target = slope * over;

            grDb = target < grDb ? att * grDb + (1.0f - att) * target
                                 : rel * grDb + (1.0f - rel) * target;
            const float g = dbToGain (grDb) * makeup;
            for (int c = 0; c < chans; ++c)
                b.getWritePointer (c)[i] *= g;
            worst = std::min (worst, grDb);
        }
        meter.store (worst);
    }

    /** Largest gain reduction in the last block, in dB (0 or negative). */
    float getGainReductionDb() const { return meter.load(); }
    float currentGrDb() const { return grDb; }

private:
    double sr = 48000.0;
    std::array<Biquad, 2> sidechainHp;
    float grDb = 0.0f;
    std::atomic<float> meter { 0.0f };
};

/** Split-band de-esser: a band-pass listens around the sibilance; when it gets
    loud, a narrow dynamic cut is applied at that frequency. */
class DeEsser
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& f : cut) f.reset();
        env = 0.0f; cutDb = 0.0f; counter = 0;
    }

    void process (juce::AudioBuffer<float>& b, float freq, float thresholdDb, float depthDb)
    {
        const int n = b.getNumSamples(), chans = std::min (b.getNumChannels(), 2);
        if (depthDb < 0.05f) { cutDb = 0.0f; return; }
        if (freq != lastFreq) { listen.bandPass (sr, freq, 1.4); lastFreq = freq; }
        const float att = std::exp (-1.0f / (0.0005f * (float) sr)), rel = std::exp (-1.0f / (0.04f * (float) sr));

        for (int i = 0; i < n; ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < chans; ++c) m += b.getReadPointer (c)[i];
            const float s = std::abs (listen.process (m / (float) chans));
            env = s > env ? att * env + (1.0f - att) * s : rel * env + (1.0f - rel) * s;
            const float over = gainToDb (env) - thresholdDb;
            const float wanted = over > 0.0f ? std::min (depthDb, over * 0.75f) : 0.0f;
            cutDb += 0.02f * (wanted - cutDb);

            if ((counter++ & 15) == 0)
                for (auto& f : cut) f.peaking (sr, freq, 1.6, -cutDb);
            for (int c = 0; c < chans; ++c)
            {
                float* d = b.getWritePointer (c);
                d[i] = cut[(size_t) c].process (d[i]);
            }
        }
    }

    float getCutDb() const { return cutDb; }

private:
    double sr = 48000.0;
    Biquad listen;
    std::array<Biquad, 2> cut;
    float env = 0.0f, cutDb = 0.0f, lastFreq = -1.0f;
    int counter = 0;
};
} // namespace vc
