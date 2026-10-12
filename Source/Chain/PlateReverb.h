#pragma once

#include "Dsp.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace vc
{
/** A lush plate reverb after Dattorro's 1997 design: a pre-delay, four input
    diffusers and a figure-eight tank of two cross-fed branches, each with a
    slowly modulated all-pass (the shimmer in the tail), a damping low-pass and
    a decay gain. On top of that for vocals: a low-cut and tone control on the
    wet signal, a ducker that tucks the reverb under the singing and lets it
    bloom in the gaps, and an optional octave-up shimmer fed into the tail.
    Mono in (stereo is summed), stereo out (wet only). */
class PlateReverb
{
public:
    struct Params
    {
        float decaySec = 2.2f, preDelayMs = 28.0f, dampingHz = 6500.0f, lowCutHz = 220.0f, toneDb = -2.0f;
        float modulation = 0.4f, width = 1.0f, shimmer = 0.0f, duck = 0.35f, mix = 0.24f;
    };

    static constexpr double kBaseRate = 29761.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        k = sr / kBaseRate;
        auto S = [this] (int n) { return std::max (1, (int) std::lround ((double) n * k)); };

        preDelay.init ((int) (0.26 * sr));
        diffuser[0].init (S (142), 0.75f); diffuser[1].init (S (107), 0.75f);
        diffuser[2].init (S (379), 0.625f); diffuser[3].init (S (277), 0.625f);

        static const int apm[2] = { 672, 908 }, d1l[2] = { 4453, 4217 }, ap2l[2] = { 1800, 2656 }, d2l[2] = { 3720, 3163 };
        for (int b = 0; b < 2; ++b)
        {
            apMod[b].init (S (apm[b]), 0.7f);
            apMod[b].d.init (S (apm[b]) + (int) (40 * k) + 64);
            d1[b].init (S (d1l[b]));  d1Len[b] = S (d1l[b]);
            ap2[b].init (S (ap2l[b]), 0.5f);
            d2[b].init (S (d2l[b]));  d2Len[b] = S (d2l[b]);
            damp[b] = 0.0f;
            apmLen[b] = (float) S (apm[b]);
        }
        loopSec = ((double) (672 + 4453 + 1800 + 3720 + 908 + 4217 + 2656 + 3163)) / kBaseRate;

        inLp = 0.0f;
        lfoPhase[0] = 0.0; lfoPhase[1] = 0.5;
        sh.init ((int) (0.12 * sr));
        shPhase = 0.0f; shPrev = 0.0f;
        for (auto& f : wetHp) f.reset();
        for (auto& f : wetShelf) f.reset();
        duckEnv = 0.0f; duckGain = 1.0f;
        lastLow = lastTone = -1.0f;
        fb[0] = fb[1] = 0.0f;
        mixSmooth.prepare (sr, 30.0f); mixSmooth.snap (0.0f);
    }

    /** Adds the reverb to the buffer (dry stays as it is). `key` is the signal that makes the reverb duck. */
    void process (juce::AudioBuffer<float>& buf, const Params& p)
    {
        const int n = buf.getNumSamples(), nc = buf.getNumChannels();
        if (nc == 0) return;

        if (p.lowCutHz != lastLow) { for (auto& f : wetHp) f.highPass (sr, p.lowCutHz, 0.707); lastLow = p.lowCutHz; }
        if (p.toneDb != lastTone)  { for (auto& f : wetShelf) f.highShelf (sr, 5000.0, p.toneDb); lastTone = p.toneDb; }

        const float decay = std::min (0.985f, std::pow (10.0f, -1.5f * (float) loopSec / std::max (0.1f, p.decaySec)));
        const float dampK = 1.0f - std::exp (-2.0f * (float) kPi * p.dampingHz / (float) sr);
        const float inK = 1.0f - std::exp (-2.0f * (float) kPi * 12000.0f / (float) sr);
        const float modSamples = p.modulation * 10.0f * (float) k;
        const int preSamples = std::max (1, (int) (p.preDelayMs * 0.001f * (float) sr));
        const float duckAtt = std::exp (-1.0f / (0.008f * (float) sr)), duckRel = std::exp (-1.0f / (0.3f * (float) sr));
        const float shAmt = p.shimmer;
        mixSmooth.target = p.mix;
        const float widthAmt = p.width;

        for (int i = 0; i < n; ++i)
        {
            float in = 0.0f;
            for (int c = 0; c < nc; ++c) in += buf.getReadPointer (c)[i];
            in /= (float) nc;

            // dry level for the ducker
            const float a = std::abs (in);
            duckEnv = a > duckEnv ? duckAtt * duckEnv + (1.0f - duckAtt) * a : duckRel * duckEnv + (1.0f - duckRel) * a;
            const float lvlDb = gainToDb (duckEnv);
            const float u = std::clamp ((lvlDb + 40.0f) / 22.0f, 0.0f, 1.0f);           // -40 dB .. -18 dB
            const float wantDuck = 1.0f - p.duck * u * u * (3.0f - 2.0f * u);
            duckGain += 0.003f * (wantDuck - duckGain);

            // pre-delay, bandwidth, diffusion
            const float pd = preDelay.read (preSamples);
            preDelay.write (in);
            inLp += inK * (pd - inLp);
            float x = inLp;
            for (auto& d : diffuser) x = d.process (x);

            // shimmer: the octave-up of what the tail is doing, fed back into the tail
            const float shOut = shAmt > 0.001f ? shimmerTick (0.5f * (shPrev)) : 0.0f;

            // figure-eight tank
            float yb[2];
            for (int b = 0; b < 2; ++b)
            {
                const double ph = lfoPhase[b];
                const float lfo = (float) std::sin (2.0 * kPi * ph);
                float s = (b == 0 ? x + fb[1] : x + fb[0]) + 0.35f * shAmt * shOut;
                s = apMod[b].processMod (s, apmLen[b] + modSamples * lfo);
                d1[b].write (s);
                const float s2 = d1[b].read (d1Len[b]);
                damp[b] += dampK * (s2 - damp[b]);
                float s3 = damp[b] * decay;
                s3 = ap2[b].process (s3);
                const float outv = d2[b].read (d2Len[b]);
                d2[b].write (s3);
                yb[b] = outv;
            }
            fb[0] = yb[0]; fb[1] = yb[1];
            lfoPhase[0] += 0.8 / sr; lfoPhase[1] += 1.07 / sr;
            if (lfoPhase[0] >= 1.0) lfoPhase[0] -= 1.0;
            if (lfoPhase[1] >= 1.0) lfoPhase[1] -= 1.0;

            // taps from the tank (positions in base-rate samples, scaled)
            auto T = [this] (int t) { return std::max (1, (int) std::lround ((double) t * k)); };
            float wl = d1[1].read (T (266)) + d1[1].read (T (2974)) - ap2[1].d.read (T (1913)) + d2[1].read (T (1996))
                     - d1[0].read (T (1990)) - ap2[0].d.read (T (187)) - d2[0].read (T (1066));
            float wr = d1[0].read (T (353)) + d1[0].read (T (3627)) - ap2[0].d.read (T (1228)) + d2[0].read (T (2673))
                     - d1[1].read (T (2111)) - ap2[1].d.read (T (335)) - d2[1].read (T (121));
            wl *= 0.5f; wr *= 0.5f;
            shPrev = 0.5f * (wl + wr);

            // wet shaping
            wl = wetShelf[0].process (wetHp[0].process (wl));
            wr = wetShelf[1].process (wetHp[1].process (wr));
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * widthAmt;
            wl = mid + side; wr = mid - side;
            if (shAmt > 0.001f) { wl += 0.5f * shAmt * shOut; wr += 0.5f * shAmt * shOut; }

            const float g = mixSmooth.next() * duckGain;
            if (nc >= 2)
            {
                buf.getWritePointer (0)[i] += g * wl;
                buf.getWritePointer (1)[i] += g * wr;
            }
            else
                buf.getWritePointer (0)[i] += g * 0.5f * (wl + wr);
        }
    }

    /** Impulse-response helper for tests. */
    double loopSeconds() const { return loopSec; }

private:
    // two crossfaded delay taps sweeping at (ratio-1)/window: +12 semitones
    float shimmerTick (float x)
    {
        const float W = (float) sh.buf.size() * 0.5f;
        sh.write (x);
        shPhase += 1.0f / W;                                  // ratio 2
        if (shPhase >= 1.0f) shPhase -= 1.0f;
        const float p2 = shPhase + 0.5f >= 1.0f ? shPhase - 0.5f : shPhase + 0.5f;
        const float d1t = std::max (2.0f, (1.0f - shPhase) * W), d2t = std::max (2.0f, (1.0f - p2) * W);
        const float w1 = std::sin ((float) kPi * shPhase), w2 = std::sin ((float) kPi * p2);
        return w1 * sh.readFrac (d1t) + w2 * sh.readFrac (d2t);
    }

    double sr = 48000.0, k = 1.6, loopSec = 0.72;
    DelayLine preDelay, sh;
    std::array<AllPass, 4> diffuser;
    std::array<AllPass, 2> apMod, ap2;
    std::array<DelayLine, 2> d1, d2;
    std::array<int, 2> d1Len {}, d2Len {};
    std::array<float, 2> damp {}, apmLen {}, fb {};
    std::array<double, 2> lfoPhase {};
    std::array<Biquad, 2> wetHp, wetShelf;
    Smooth mixSmooth;
    float inLp = 0.0f, shPhase = 0.0f, shPrev = 0.0f, duckEnv = 0.0f, duckGain = 1.0f, lastLow = -1.0f, lastTone = -1.0f;
};
} // namespace vc
