#pragma once

#include "StackEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace vs
{
struct FloatDef { const char* id; const char* label; float lo, hi, def, step; const char* unit; int section; };
enum Section { SecInput, SecStack, SecTexture, SecUpfront, SecOutput };

inline const std::vector<FloatDef>& floatDefs()
{
    static const std::vector<FloatDef> d = {
        { "inGain",   "Input",      -12.0f, 12.0f,  0.0f, 0.1f, " dB",    SecInput },
        { "voices",   "Voices",       1.0f,  6.0f,  4.0f, 1.0f, "",       SecStack },
        { "stack",    "Stack level",  0.0f,100.0f, 60.0f, 1.0f, " %",     SecStack },
        { "spread",   "Spread",       0.0f,100.0f, 80.0f, 1.0f, " %",     SecStack },
        { "detune",   "Detune",       0.0f, 30.0f,  9.0f, 0.5f, " cents", SecStack },
        { "tight",    "Looseness",    0.0f,100.0f, 50.0f, 1.0f, " %",     SecStack },
        { "bright",   "Stack bright",-8.0f,  8.0f,  0.0f, 0.1f, " dB",    SecStack },
        { "grit",     "Grit",         0.0f,100.0f, 35.0f, 1.0f, " %",     SecTexture },
        { "bite",     "Bite",         0.0f,100.0f, 40.0f, 1.0f, " %",     SecTexture },
        { "hard",     "Hard",         0.0f,100.0f, 40.0f, 1.0f, " %",     SecTexture },
        { "presence", "Presence",    -4.0f, 10.0f,  3.0f, 0.1f, " dB",    SecUpfront },
        { "mud",      "Mud cut",      0.0f, 10.0f,  3.0f, 0.1f, " dB",    SecUpfront },
        { "upfront",  "Upfront",      0.0f,100.0f, 50.0f, 1.0f, " %",     SecUpfront },
        { "room",     "Room",         0.0f,100.0f, 15.0f, 1.0f, " %",     SecUpfront },
        { "outGain",  "Output",     -12.0f, 12.0f,  0.0f, 0.1f, " dB",    SecOutput },
    };
    return d;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& f : floatDefs())
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { f.id, 1 }, f.label, NormalisableRange<float> (f.lo, f.hi, f.step), f.def));
    return layout;
}

struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        { "Stacked & Hard (default)", {} },
        { "Wall of Voices",   { { "voices", 6 }, { "stack", 85 }, { "spread", 100 }, { "detune", 12 }, { "tight", 60 }, { "bright", 1.5f }, { "grit", 40 }, { "bite", 35 }, { "hard", 35 }, { "upfront", 55 } } },
        { "Hard & Upfront",   { { "voices", 3 }, { "stack", 40 }, { "spread", 55 }, { "detune", 6 }, { "tight", 20 }, { "grit", 50 }, { "bite", 65 }, { "hard", 70 }, { "presence", 5 }, { "mud", 4.5f }, { "upfront", 85 }, { "room", 8 } } },
        { "Gritty Lead",      { { "voices", 2 }, { "stack", 30 }, { "spread", 40 }, { "detune", 5 }, { "tight", 15 }, { "grit", 80 }, { "bite", 80 }, { "hard", 55 }, { "presence", 4 }, { "upfront", 70 } } },
        { "Tight Double",     { { "voices", 2 }, { "stack", 55 }, { "spread", 90 }, { "detune", 4 }, { "tight", 5 }, { "grit", 10 }, { "bite", 20 }, { "hard", 20 }, { "upfront", 40 }, { "room", 5 } } },
        { "Subtle Stack",     { { "voices", 3 }, { "stack", 35 }, { "spread", 70 }, { "detune", 7 }, { "tight", 40 }, { "grit", 10 }, { "bite", 15 }, { "hard", 15 }, { "presence", 2 }, { "upfront", 30 }, { "room", 10 } } },
    };
    return p;
}

inline StackParams readParams (juce::AudioProcessorValueTreeState& s)
{
    auto v = [&s] (const char* id) { return s.getRawParameterValue (id)->load(); };
    StackParams p;
    p.inGainDb = v ("inGain"); p.outGainDb = v ("outGain");
    p.voices = v ("voices"); p.stack = v ("stack") * 0.01f; p.spread = v ("spread") * 0.01f;
    p.detuneCents = v ("detune"); p.tight = v ("tight") * 0.01f; p.brightDb = v ("bright");
    p.grit = v ("grit") * 0.01f; p.bite = v ("bite") * 0.01f; p.hard = v ("hard") * 0.01f;
    p.presenceDb = v ("presence"); p.mudDb = v ("mud"); p.upfront = v ("upfront") * 0.01f; p.room = v ("room") * 0.01f;
    return p;
}
} // namespace vs
