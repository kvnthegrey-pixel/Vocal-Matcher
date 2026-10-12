#pragma once

#include "Compressor.h"
#include "PitchCorrector.h"
#include "PlateReverb.h"

namespace vc
{
struct ChainParams
{
    // input
    float inGainDb = 0.0f, lowCutHz = 80.0f;
    // autotune
    bool tuneOn = true; int key = 0, scale = 1; float tuneSpeedMs = 20.0f, tuneAmount = 1.0f;
    // compressor
    float compThreshDb = -22.0f, compRatio = 3.5f, compAttackMs = 8.0f, compReleaseMs = 120.0f, compMakeupDb = 4.0f, punch = 0.25f;
    // de-esser
    float deessFreq = 6500.0f, deessThreshDb = -34.0f, deessDepthDb = 5.0f;
    // colour
    float warmthDb = 1.5f, drive = 0.12f, airDb = 4.0f, thicken = 0.18f;
    // reverb
    PlateReverb::Params reverb;
    // output
    float outGainDb = 0.0f;
};

/** The whole vocal chain, in the order a mix engineer would put it:
    gain and low-cut, pitch correction, compression (plus a parallel "punch"
    compressor), de-essing, colour (warmth, drive, air), thickening, reverb,
    output gain and a safety ceiling. Fixed latency: PitchCorrector::kLatency. */
class ChainEngine
{
public:
    static constexpr int kLatencySamples = PitchCorrector::kLatency;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        tune.prepare (sr, 2);
        comp.prepare (sr, 2);
        squash.prepare (sr, 2);
        deess.prepare (sr);
        reverb.prepare (sr);
        for (auto& f : lowCut) { f.reset(); f.highPass (sr, 80.0, 0.707); }
        for (auto& f : warmth) f.reset();
        for (auto& f : air) f.reset();
        for (auto& f : dcBlock) { f.reset(); f.highPass (sr, 12.0, 0.707); }
        for (int v = 0; v < 2; ++v) thickLine[v].init ((int) (0.06 * sr));
        thickPhase[0] = 0.0; thickPhase[1] = 0.5;
        inSm.prepare (sr, 20.0f); outSm.prepare (sr, 20.0f); punchSm.prepare (sr, 30.0f); thickSm.prepare (sr, 30.0f); driveSm.prepare (sr, 30.0f);
        inSm.snap (1.0f); outSm.snap (1.0f); punchSm.snap (0.0f); thickSm.snap (0.0f); driveSm.snap (0.0f);
        lastLow = lastWarm = lastAir = -999.0f;
        parallel.setSize (2, std::max (1, maxBlock));
        peakIn = peakOut = 0.0f;
    }

    /** buffer must have two channels (a mono input is duplicated by the caller). */
    void process (juce::AudioBuffer<float>& b, const ChainParams& p)
    {
        const int n = b.getNumSamples();
        if (parallel.getNumSamples() < n) parallel.setSize (2, n, false, false, true);

        // 1. input gain + low cut
        if (p.lowCutHz != lastLow) { for (auto& f : lowCut) f.highPass (sr, p.lowCutHz, 0.707); lastLow = p.lowCutHz; }
        inSm.target = dbToGain (p.inGainDb);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = inSm.next();
            for (int c = 0; c < 2; ++c)
            {
                float* d = b.getWritePointer (c);
                d[i] = lowCut[(size_t) c].process (d[i] * g);
                pk = std::max (pk, std::abs (d[i]));
            }
        }
        peakIn = pk;

        // 2. pitch correction (always in the path so the latency never changes)
        PitchCorrector::Params tp;
        tp.enabled = p.tuneOn; tp.keyRoot = p.key; tp.scale = p.scale; tp.speedMs = p.tuneSpeedMs; tp.amount = p.tuneAmount;
        tune.process (b, tp);

        // 3. compression + parallel punch
        Compressor::Params cp;
        cp.thresholdDb = p.compThreshDb; cp.ratio = p.compRatio; cp.attackMs = p.compAttackMs; cp.releaseMs = p.compReleaseMs;
        cp.makeupDb = p.compMakeupDb; cp.kneeDb = 6.0f;
        comp.process (b, cp);
        punchSm.target = p.punch;
        if (p.punch > 0.001f || punchSm.v > 0.001f)
        {
            for (int c = 0; c < 2; ++c) parallel.copyFrom (c, 0, b, c, 0, n);
            Compressor::Params sq;
            sq.thresholdDb = -32.0f; sq.ratio = 10.0f; sq.attackMs = 0.8f; sq.releaseMs = 70.0f; sq.kneeDb = 3.0f; sq.makeupDb = 12.0f;
            juce::AudioBuffer<float> view (parallel.getArrayOfWritePointers(), 2, n);
            squash.process (view, sq);
            for (int i = 0; i < n; ++i)
            {
                const float g = punchSm.next() * 0.5f;
                for (int c = 0; c < 2; ++c) b.getWritePointer (c)[i] += g * parallel.getReadPointer (c)[i];
            }
        }

        // 4. de-esser
        deess.process (b, p.deessFreq, p.deessThreshDb, p.deessDepthDb);

        // 5. colour: warmth, drive, air
        if (p.warmthDb != lastWarm) { for (auto& f : warmth) f.lowShelf (sr, 200.0, p.warmthDb); lastWarm = p.warmthDb; }
        if (p.airDb != lastAir)     { for (auto& f : air) f.highShelf (sr, 11000.0, p.airDb); lastAir = p.airDb; }
        driveSm.target = p.drive;
        for (int i = 0; i < n; ++i)
        {
            const float dr = driveSm.next();
            const float kk = 1.0f + 5.0f * dr * dr;
            const float bias = 0.12f * dr;
            for (int c = 0; c < 2; ++c)
            {
                float* d = b.getWritePointer (c);
                float x = warmth[(size_t) c].process (d[i]);
                if (dr > 0.001f)
                {
                    x = (std::tanh (kk * (x + bias)) - std::tanh (kk * bias)) / kk;
                    x = dcBlock[(size_t) c].process (x);
                }
                d[i] = air[(size_t) c].process (x);
            }
        }

        // 6. thicken: two slowly detuned, delayed copies, one to each side
        thickSm.target = p.thicken;
        for (int i = 0; i < n; ++i)
        {
            const float amt = thickSm.next();
            const float m = 0.5f * (b.getReadPointer (0)[i] + b.getReadPointer (1)[i]);
            float v[2];
            const float base[2] = { 0.017f, 0.023f }, depth[2] = { 0.0015f, 0.0018f }, rate[2] = { 0.35f, 0.47f };
            for (int s = 0; s < 2; ++s)
            {
                const float lfo = (float) std::sin (2.0 * kPi * thickPhase[s]);
                v[s] = thickLine[s].readFrac ((base[s] + depth[s] * lfo * (s == 0 ? 1.0f : -1.0f)) * (float) sr);
                thickLine[s].write (m);
                thickPhase[s] += rate[s] / sr;
                if (thickPhase[s] >= 1.0) thickPhase[s] -= 1.0;
            }
            b.getWritePointer (0)[i] += 0.6f * amt * v[0];
            b.getWritePointer (1)[i] += 0.6f * amt * v[1];
        }

        // 7. reverb (adds wet to the signal)
        reverb.process (b, p.reverb);

        // 8. output gain and safety ceiling
        outSm.target = dbToGain (p.outGainDb);
        float po = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = outSm.next();
            for (int c = 0; c < 2; ++c)
            {
                float* d = b.getWritePointer (c);
                d[i] = softCeiling (d[i] * g);
                po = std::max (po, std::abs (d[i]));
            }
        }
        peakOut = po;
    }

    // meters and displays (any thread)
    float gainReductionDb() const { return comp.getGainReductionDb(); }
    float deessCutDb() const      { return deess.getCutDb(); }
    float inputPeak() const       { return peakIn.load(); }
    float outputPeak() const      { return peakOut.load(); }
    float detectedMidi() const    { return tune.detectedMidi(); }
    float targetMidi() const      { return tune.targetMidi(); }

private:
    double sr = 48000.0;
    PitchCorrector tune;
    Compressor comp, squash;
    DeEsser deess;
    PlateReverb reverb;
    std::array<Biquad, 2> lowCut, warmth, air, dcBlock;
    std::array<DelayLine, 2> thickLine;
    std::array<double, 2> thickPhase {};
    Smooth inSm, outSm, punchSm, thickSm, driveSm;
    juce::AudioBuffer<float> parallel;
    float lastLow = -999.0f, lastWarm = -999.0f, lastAir = -999.0f;
    std::atomic<float> peakIn { 0.0f }, peakOut { 0.0f };
};
} // namespace vc
