#pragma once

#include "ChainEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace vc
{
struct FloatDef
{
    const char* id;
    const char* label;
    float lo, hi, def, step;
    float centre;            // value shown at the middle of the knob (0 = linear)
    const char* unit;
    int section;
};

enum Section { SecInput, SecTune, SecComp, SecDeess, SecColour, SecReverb, SecOutput };

inline const std::vector<FloatDef>& floatDefs()
{
    static const std::vector<FloatDef> d = {
        { "inGain",      "Input",       -12.0f,  12.0f,   0.0f, 0.1f,   0.0f, " dB", SecInput },
        { "lowCut",      "Low cut",      20.0f, 300.0f,  80.0f, 1.0f,  80.0f, " Hz", SecInput },
        { "tuneSpeed",   "Retune speed",  0.0f, 400.0f,  20.0f, 1.0f,  60.0f, " ms", SecTune },
        { "tuneAmount",  "Amount",        0.0f, 100.0f, 100.0f, 1.0f,   0.0f, " %",  SecTune },
        { "compThresh",  "Threshold",   -50.0f,   0.0f, -22.0f, 0.5f,   0.0f, " dB", SecComp },
        { "compRatio",   "Ratio",         1.0f,  20.0f,   3.5f, 0.1f,   4.0f, ":1",  SecComp },
        { "compAttack",  "Attack",        0.1f, 100.0f,   8.0f, 0.1f,  10.0f, " ms", SecComp },
        { "compRelease", "Release",      10.0f,1000.0f, 120.0f, 1.0f, 150.0f, " ms", SecComp },
        { "compMakeup",  "Makeup",        0.0f,  18.0f,   4.0f, 0.1f,   0.0f, " dB", SecComp },
        { "punch",       "Punch",         0.0f, 100.0f,  25.0f, 1.0f,   0.0f, " %",  SecComp },
        { "deessFreq",   "Frequency",  3000.0f,10000.0f,6500.0f, 50.0f,  0.0f, " Hz", SecDeess },
        { "deessThresh", "Threshold",   -50.0f, -10.0f, -34.0f, 0.5f,   0.0f, " dB", SecDeess },
        { "deessDepth",  "Depth",         0.0f,  12.0f,   5.0f, 0.1f,   0.0f, " dB", SecDeess },
        { "warmth",      "Warmth",        0.0f,   6.0f,   1.5f, 0.1f,   0.0f, " dB", SecColour },
        { "drive",       "Drive",         0.0f, 100.0f,  12.0f, 1.0f,   0.0f, " %",  SecColour },
        { "air",         "Air",           0.0f,  10.0f,   4.0f, 0.1f,   0.0f, " dB", SecColour },
        { "thicken",     "Thicken",       0.0f, 100.0f,  18.0f, 1.0f,   0.0f, " %",  SecColour },
        { "revDecay",    "Decay",         0.3f,  10.0f,   2.2f, 0.05f,  2.0f, " s",  SecReverb },
        { "revPre",      "Pre-delay",     0.0f, 200.0f,  28.0f, 1.0f,  40.0f, " ms", SecReverb },
        { "revDamp",     "Damping",    1500.0f,16000.0f,6500.0f, 50.0f,5000.0f," Hz", SecReverb },
        { "revLow",      "Low cut",      20.0f, 600.0f, 220.0f, 1.0f, 150.0f, " Hz", SecReverb },
        { "revTone",     "Tone",         -8.0f,   4.0f,  -2.0f, 0.1f,   0.0f, " dB", SecReverb },
        { "revMod",      "Movement",      0.0f, 100.0f,  40.0f, 1.0f,   0.0f, " %",  SecReverb },
        { "revWidth",    "Width",         0.0f, 100.0f, 100.0f, 1.0f,   0.0f, " %",  SecReverb },
        { "revShimmer",  "Shimmer",       0.0f, 100.0f,   0.0f, 1.0f,   0.0f, " %",  SecReverb },
        { "revDuck",     "Duck",          0.0f, 100.0f,  35.0f, 1.0f,   0.0f, " %",  SecReverb },
        { "revMix",      "Reverb mix",    0.0f, 100.0f,  24.0f, 1.0f,   0.0f, " %",  SecReverb },
        { "outGain",     "Output",      -12.0f,  12.0f,   0.0f, 0.1f,   0.0f, " dB", SecOutput },
    };
    return d;
}

inline juce::StringArray keyNames()   { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
inline juce::StringArray scaleNames() { return { "Chromatic", "Major", "Minor", "Major pentatonic", "Minor pentatonic" }; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createChainLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "tuneOn", 1 }, "Auto-tune on", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keyNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scaleNames(), 1));
    for (auto& f : floatDefs())
    {
        NormalisableRange<float> r (f.lo, f.hi, f.step);
        if (f.centre > f.lo && f.centre < f.hi) r.setSkewForCentre (f.centre);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { f.id, 1 }, f.label, r, f.def));
    }
    return layout;
}

struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; bool tuneOn; int scale; };

/** Starting points. Every one is just a set of knob positions you can keep tweaking. */
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        { "Sweet Pop (default)", { { "tuneSpeed", 25 }, { "tuneAmount", 85 }, { "compThresh", -22 }, { "compRatio", 3.5f }, { "compAttack", 8 }, { "compRelease", 120 },
                                   { "compMakeup", 4 }, { "punch", 25 }, { "deessThresh", -34 }, { "deessDepth", 5 }, { "warmth", 1.5f }, { "drive", 12 }, { "air", 4 }, { "thicken", 18 },
                                   { "revDecay", 2.2f }, { "revPre", 28 }, { "revDamp", 6500 }, { "revLow", 220 }, { "revTone", -2 }, { "revMod", 40 }, { "revWidth", 100 },
                                   { "revShimmer", 0 }, { "revDuck", 35 }, { "revMix", 24 } }, true, 1 },
        { "Hard Tune Effect",   { { "tuneSpeed", 0 }, { "tuneAmount", 100 }, { "compThresh", -24 }, { "compRatio", 4 }, { "punch", 30 }, { "drive", 18 }, { "air", 5 }, { "thicken", 10 },
                                   { "revDecay", 1.4f }, { "revPre", 15 }, { "revDamp", 7500 }, { "revMod", 30 }, { "revShimmer", 0 }, { "revDuck", 25 }, { "revMix", 16 } }, true, 0 },
        { "Dreamy Shimmer",     { { "tuneSpeed", 40 }, { "tuneAmount", 70 }, { "compThresh", -20 }, { "compRatio", 3 }, { "punch", 15 }, { "warmth", 2 }, { "drive", 8 }, { "air", 6 }, { "thicken", 32 },
                                   { "revDecay", 5.5f }, { "revPre", 45 }, { "revDamp", 5200 }, { "revLow", 260 }, { "revTone", -3 }, { "revMod", 65 }, { "revWidth", 100 },
                                   { "revShimmer", 35 }, { "revDuck", 55 }, { "revMix", 36 } }, true, 1 },
        { "Intimate Close",     { { "tuneSpeed", 80 }, { "tuneAmount", 40 }, { "compThresh", -26 }, { "compRatio", 2.5f }, { "compAttack", 12 }, { "compRelease", 160 }, { "punch", 10 },
                                   { "warmth", 2.5f }, { "drive", 8 }, { "air", 3 }, { "thicken", 6 },
                                   { "revDecay", 0.9f }, { "revPre", 12 }, { "revDamp", 5500 }, { "revMod", 25 }, { "revShimmer", 0 }, { "revDuck", 45 }, { "revMix", 12 } }, true, 1 },
        { "Anthem Wide",        { { "tuneSpeed", 15 }, { "tuneAmount", 90 }, { "compThresh", -24 }, { "compRatio", 4.5f }, { "punch", 40 }, { "warmth", 1.5f }, { "drive", 22 }, { "air", 6 }, { "thicken", 55 },
                                   { "revDecay", 3.2f }, { "revPre", 35 }, { "revDamp", 6000 }, { "revMod", 50 }, { "revWidth", 100 }, { "revShimmer", 12 }, { "revDuck", 40 }, { "revMix", 30 } }, true, 1 },
        { "Natural (no tune)",  { { "compThresh", -20 }, { "compRatio", 2.5f }, { "punch", 0 }, { "drive", 0 }, { "air", 2 }, { "thicken", 0 },
                                   { "revDecay", 1.8f }, { "revPre", 20 }, { "revMod", 35 }, { "revShimmer", 0 }, { "revDuck", 35 }, { "revMix", 15 } }, false, 1 },
    };
    return p;
}

inline ChainParams readParams (juce::AudioProcessorValueTreeState& s)
{
    auto v = [&s] (const char* id) { return s.getRawParameterValue (id)->load(); };
    ChainParams p;
    p.inGainDb = v ("inGain"); p.lowCutHz = v ("lowCut");
    p.tuneOn = v ("tuneOn") >= 0.5f; p.key = (int) std::lround (v ("key")); p.scale = (int) std::lround (v ("scale"));
    p.tuneSpeedMs = v ("tuneSpeed"); p.tuneAmount = v ("tuneAmount") * 0.01f;
    p.compThreshDb = v ("compThresh"); p.compRatio = v ("compRatio"); p.compAttackMs = v ("compAttack"); p.compReleaseMs = v ("compRelease");
    p.compMakeupDb = v ("compMakeup"); p.punch = v ("punch") * 0.01f;
    p.deessFreq = v ("deessFreq"); p.deessThreshDb = v ("deessThresh"); p.deessDepthDb = v ("deessDepth");
    p.warmthDb = v ("warmth"); p.drive = v ("drive") * 0.01f; p.airDb = v ("air"); p.thicken = v ("thicken") * 0.01f;
    p.reverb.decaySec = v ("revDecay"); p.reverb.preDelayMs = v ("revPre"); p.reverb.dampingHz = v ("revDamp"); p.reverb.lowCutHz = v ("revLow");
    p.reverb.toneDb = v ("revTone"); p.reverb.modulation = v ("revMod") * 0.01f; p.reverb.width = v ("revWidth") * 0.01f;
    p.reverb.shimmer = v ("revShimmer") * 0.01f; p.reverb.duck = v ("revDuck") * 0.01f; p.reverb.mix = v ("revMix") * 0.01f;
    p.outGainDb = v ("outGain");
    return p;
}
} // namespace vc
