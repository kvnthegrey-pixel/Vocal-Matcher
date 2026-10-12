#pragma once

#include "../Chain/Dsp.h"
#include "../Chain/Compressor.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>

namespace vs
{
using vc::dbToGain;

struct StackParams
{
    float inGainDb = 0.0f, outGainDb = 0.0f;
    float voices = 4.0f, stack = 0.6f, spread = 0.8f, detuneCents = 9.0f, tight = 0.5f, brightDb = 0.0f;
    float grit = 0.35f, bite = 0.4f, hard = 0.4f;
    float presenceDb = 3.0f, mudDb = 3.0f, upfront = 0.5f, room = 0.15f;
};

/** Stacked, hard, upfront vocal texture. Zero latency.
    Lead: low-cut, mud cut, presence, compression (tighter as "upfront" rises), a hard parallel
    clipper and a high-band exciter ("bite").
    Stack: up to six delayed, slowly detuned, differently panned copies of the lead, each
    saturated, summed and glued by their own compressor. */
class StackEngine
{
public:
    static constexpr int kMaxVoices = 6;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            hp[c].reset(); hp[c].highPass (sr, 90.0, 0.707);
            mud[c].reset(); pres[c].reset(); biteHp[c].reset(); biteHp[c].highPass (sr, 3000.0, 0.707);
            hardHp[c].reset(); hardHp[c].highPass (sr, 160.0, 0.707);
            hardLp[c].reset(); hardLp[c].lowPass (sr, 11000.0, 0.707);
            roomLp[c].reset(); roomLp[c].lowPass (sr, 5500.0, 0.707);
            bright[c].reset();
            dc[c].reset(); dc[c].highPass (sr, 15.0, 0.707);
        }
        for (int v = 0; v < kMaxVoices; ++v)
        {
            line[v].init ((int) (0.12 * sr));
            vHp[v].reset(); vHp[v].highPass (sr, 220.0, 0.707);
            vLp[v].reset(); vLp[v].lowPass (sr, 8500.0, 0.707);
            phase[v] = 0.17 * v;
            weight[v].prepare (sr, 60.0f); weight[v].snap (0.0f);
        }
        for (int t = 0; t < 3; ++t) roomLine[t].init ((int) (0.05 * sr));
        comp.prepare (sr, 2); stackComp.prepare (sr, 2);
        inSm.prepare (sr, 20.0f); outSm.prepare (sr, 20.0f); stackSm.prepare (sr, 40.0f);
        hardSm.prepare (sr, 30.0f); biteSm.prepare (sr, 30.0f); roomSm.prepare (sr, 40.0f);
        inSm.snap (1.0f); outSm.snap (1.0f); stackSm.snap (0.0f); hardSm.snap (0.0f); biteSm.snap (0.0f); roomSm.snap (0.0f);
        lastMud = lastPres = lastBright = -999.0f;
        stackBuf.setSize (2, std::max (1, maxBlock));
        peakIn = peakOut = 0.0f;
    }

    void process (juce::AudioBuffer<float>& b, const StackParams& p)
    {
        const int n = b.getNumSamples();
        if (stackBuf.getNumSamples() < n) stackBuf.setSize (2, n, false, false, true);

        const float presTot = p.presenceDb + 3.0f * p.upfront;
        if (p.mudDb != lastMud)    { for (auto& f : mud)  f.peaking (sr, 320.0, 1.0, -p.mudDb); lastMud = p.mudDb; }
        if (presTot != lastPres)   { for (auto& f : pres) f.peaking (sr, 3600.0, 0.9, presTot); lastPres = presTot; }
        if (p.brightDb != lastBright) { for (auto& f : bright) f.highShelf (sr, 5000.0, p.brightDb); lastBright = p.brightDb; }

        // 1. input gain and lead tone
        inSm.target = dbToGain (p.inGainDb);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = inSm.next();
            for (int c = 0; c < 2; ++c)
            {
                float* d = b.getWritePointer (c);
                const float x = d[i] * g;
                pk = std::max (pk, std::abs (x));
                d[i] = pres[c].process (mud[c].process (hp[c].process (x)));
            }
        }
        peakIn = pk;

        // 2. lead compression, firmer as upfront rises
        vc::Compressor::Params cp;
        cp.thresholdDb = -20.0f - 10.0f * p.upfront; cp.ratio = 3.0f + 3.0f * p.upfront;
        cp.attackMs = 6.0f - 3.0f * p.upfront; cp.releaseMs = 110.0f; cp.kneeDb = 6.0f; cp.makeupDb = 2.0f + 5.0f * p.upfront;
        comp.process (b, cp);

        // 3. hard + bite (parallel), room, written back into the lead
        hardSm.target = p.hard; biteSm.target = p.bite; roomSm.target = p.room; stackSm.target = p.stack;
        const float hk = 2.0f + 18.0f * p.hard;
        for (int i = 0; i < n; ++i)
        {
            const float hs = hardSm.next(), bs = biteSm.next(), rs = roomSm.next();
            float pre[2];
            for (int c = 0; c < 2; ++c)
            {
                float* d = b.getWritePointer (c);
                const float x = d[i];
                float y = x;
                if (hs > 0.001f)
                {
                    const float h = hardLp[c].process (std::tanh (hk * hardHp[c].process (x)) / std::sqrt (hk));
                    y += 0.45f * hs * h;
                }
                if (bs > 0.001f)
                {
                    const float hi = biteHp[c].process (x);
                    // asymmetric clipper: even and odd harmonics, which is the "rasp"
                    const float s = std::tanh (6.0f * hi + 0.4f) - std::tanh (0.4f);
                    y += 0.35f * bs * dc[c].process (s);
                }
                pre[c] = y;
                if (rs > 0.001f)
                {
                    const float t0 = roomLine[0].readFrac ((c == 0 ? 0.0091f : 0.0113f) * (float) sr);
                    const float t1 = roomLine[1].readFrac ((c == 0 ? 0.0143f : 0.0171f) * (float) sr);
                    const float t2 = roomLine[2].readFrac ((c == 0 ? 0.0217f : 0.0194f) * (float) sr);
                    y += rs * 0.4f * roomLp[c].process (0.6f * t0 + 0.45f * t1 + 0.35f * t2);
                }
                d[i] = y;
            }
            // the room reads the lead before the room was added, so it never feeds back
            for (int t = 0; t < 3; ++t) roomLine[t].write (0.5f * (pre[0] + pre[1]));
        }

        // 4. the stack
        const float tscale = 0.35f + 1.15f * p.tight;     // tight = early and close, loose = late and loose
        static constexpr float base[kMaxVoices]  = { 0.0140f, 0.0210f, 0.0290f, 0.0360f, 0.0430f, 0.0520f };
        static constexpr float rate[kMaxVoices]  = { 0.31f, 0.43f, 0.57f, 0.71f, 0.83f, 0.37f };
        static constexpr float pan[kMaxVoices]   = { -1.0f, 1.0f, -0.6f, 0.6f, -0.3f, 0.3f };
        const float depthSec = (p.detuneCents / 1731.0f);   // delay-modulation depth that gives +-cents at 1 Hz-ish, scaled per voice below
        for (int v = 0; v < kMaxVoices; ++v) weight[v].target = juce::jlimit (0.0f, 1.0f, p.voices - (float) v);
        const float gk = 1.0f + 4.0f * p.grit;
        float* sl = stackBuf.getWritePointer (0);
        float* sr_ = stackBuf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (b.getReadPointer (0)[i] + b.getReadPointer (1)[i]);
            float L = 0.0f, R = 0.0f, act = 0.0f;
            for (int v = 0; v < kMaxVoices; ++v)
            {
                line[v].write (m);
                const float w = weight[v].next();
                phase[v] += rate[v] / sr; if (phase[v] >= 1.0) phase[v] -= 1.0;
                if (w < 0.0005f) continue;
                const float depth = depthSec / (2.0f * (float) vc::kPi * rate[v]);
                const float d = (base[v] * tscale + depth * (float) std::sin (2.0 * vc::kPi * phase[v])) * (float) sr;
                float x = line[v].readFrac (std::max (2.0f, d));
                x = vLp[v].process (vHp[v].process (x));
                x = std::tanh (gk * x) / std::sqrt (gk);
                const float a = (pan[v] * p.spread + 1.0f) * 0.78539816f;
                L += w * x * std::cos (a); R += w * x * std::sin (a);
                act += w;
            }
            const float norm = 1.0f / std::sqrt (std::max (1.0f, act));
            sl[i] = bright[0].process (L * norm); sr_[i] = bright[1].process (R * norm);
        }
        vc::Compressor::Params sp;
        sp.thresholdDb = -26.0f; sp.ratio = 5.0f; sp.attackMs = 3.0f; sp.releaseMs = 90.0f; sp.kneeDb = 6.0f; sp.makeupDb = 4.0f;
        stackComp.process (stackBuf, sp);

        // 5. mix and output
        outSm.target = dbToGain (p.outGainDb);
        float po = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float s = stackSm.next() * 0.9f, g = outSm.next();
            for (int c = 0; c < 2; ++c)
            {
                float* d = b.getWritePointer (c);
                const float y = vc::softCeiling ((d[i] + s * stackBuf.getReadPointer (c)[i]) * g);
                d[i] = y;
                po = std::max (po, std::abs (y));
            }
        }
        peakOut = po;
    }

    float leadReductionDb() const  { return comp.getGainReductionDb(); }
    float stackReductionDb() const { return stackComp.getGainReductionDb(); }
    float inputPeak() const  { return peakIn.load(); }
    float outputPeak() const { return peakOut.load(); }

private:
    double sr = 48000.0;
    std::array<vc::Biquad, 2> hp, mud, pres, biteHp, hardHp, hardLp, roomLp, bright, dc;
    std::array<vc::DelayLine, kMaxVoices> line;
    std::array<vc::Biquad, kMaxVoices> vHp, vLp;
    std::array<double, kMaxVoices> phase {};
    std::array<vc::Smooth, kMaxVoices> weight;
    std::array<vc::DelayLine, 3> roomLine;
    vc::Compressor comp, stackComp;
    vc::Smooth inSm, outSm, stackSm, hardSm, biteSm, roomSm;
    float lastMud = -999.0f, lastPres = -999.0f, lastBright = -999.0f;
    juce::AudioBuffer<float> stackBuf;
    std::atomic<float> peakIn { 0.0f }, peakOut { 0.0f };
};
} // namespace vs
