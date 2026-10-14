#pragma once

// Performance Assist: the take history. Plain C++ (no JUCE).
//
// Every pass over the reference becomes a take: its score, accuracy, how each
// note went (for the heatmap) and a score per phrase (for picking the best
// phrases across takes to comp together).

#include "ScoreKeeper.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace va
{
struct Take
{
    int number = 0;
    int score = 0;
    int accuracy = 0;
    int bestStreak = 0;
    std::vector<BlockStat> blocks;     // only the notes graded in this take
    std::vector<float> phraseScore;    // percent, -1 = not (fully) sung in this take
};

struct Tendency
{
    bool   hasBias = false;
    double overall = 0.0;              // average cents, + sharp
    bool   hasHigh = false, hasLow = false;
    double high = 0.0, low = 0.0;      // notes above / below the take's median pitch
    int    octaveSlips = 0;            // notes with a third or more of their time an octave away
    int    worstPhrase = -1;           // 0-based, -1 when no phrase scored
    float  worstScore = 0.0f;
};

inline Tendency tendencyOf (const Take& t)
{
    Tendency out;
    if (t.blocks.empty())
        return out;

    std::vector<double> pitches;
    for (const auto& b : t.blocks) pitches.push_back (b.targetMidi);
    std::sort (pitches.begin(), pitches.end());
    const double median = pitches[pitches.size() / 2];

    double sum = 0.0, w = 0.0, hs = 0.0, hw = 0.0, ls = 0.0, lw = 0.0;
    for (const auto& b : t.blocks)
    {
        if (b.octaveFrac >= 0.33f) ++out.octaveSlips;
        if (! b.hasBias) continue;
        const double d = std::max (0.02, b.endSec - b.startSec);
        sum += b.biasCents * d; w += d;
        if (b.targetMidi > median) { hs += b.biasCents * d; hw += d; }
        else                       { ls += b.biasCents * d; lw += d; }
    }
    if (w > 0.0) { out.hasBias = true; out.overall = sum / w; }
    if (hw > 0.0) { out.hasHigh = true; out.high = hs / hw; }
    if (lw > 0.0) { out.hasLow = true; out.low = ls / lw; }

    for (size_t p = 0; p < t.phraseScore.size(); ++p)
        if (t.phraseScore[p] >= 0.0f && (out.worstPhrase < 0 || t.phraseScore[p] < out.worstScore))
        {
            out.worstPhrase = (int) p;
            out.worstScore = t.phraseScore[p];
        }
    return out;
}

inline std::string describeBias (double cents)
{
    const int c = (int) std::lround (std::abs (cents));
    if (c < 8) return "centred";
    return std::to_string (c) + " cents " + (cents < 0 ? "flat" : "sharp");
}

/** One or two plain sentences about a take. */
inline std::string tendencyText (const Take& t)
{
    const auto d = tendencyOf (t);
    if (! d.hasBias && d.octaveSlips == 0)
        return "Not enough pitched singing in this take to tell.";

    std::string s;
    if (d.hasBias)
    {
        s = "Overall " + describeBias (d.overall) + ".";
        if (d.hasHigh && d.hasLow && std::abs (d.high - d.low) >= 12.0)
            s += " High notes " + describeBias (d.high) + ", low notes " + describeBias (d.low) + ".";
    }
    if (d.octaveSlips > 0)
        s += std::string (s.empty() ? "" : " ") + std::to_string (d.octaveSlips) + (d.octaveSlips == 1 ? " note was" : " notes were") + " sung in the wrong octave.";
    if (d.worstPhrase >= 0 && d.worstScore < 80.0f)
        s += " Weakest: phrase " + std::to_string (d.worstPhrase + 1) + " (" + std::to_string ((int) std::lround (d.worstScore)) + "%).";
    return s;
}

class TakeStore
{
public:
    static constexpr size_t maxTakes = 40;

    /** Adds a take from a score snapshot. Returns false when nothing was graded. */
    bool add (const ScoreSnapshot& snap)
    {
        if (snap.countedBlocks <= 0)
            return false;
        Take t;
        t.number = ++counter;
        t.score = snap.score;
        t.accuracy = snap.accuracy;
        t.bestStreak = snap.bestStreak;
        t.phraseScore = snap.phraseScore;
        for (const auto& b : snap.blocks)
            if (b.counted)
                t.blocks.push_back (b);
        list.push_back (std::move (t));
        trim();
        return true;
    }

    const std::vector<Take>& takes() const { return list; }
    const Take* find (int number) const
    {
        for (const auto& t : list)
            if (t.number == number)
                return &t;
        return nullptr;
    }
    void clear() { list.clear(); }
    int  nextNumber() const { return counter + 1; }

    /** Highest-scoring take (by score), or null. */
    const Take* best() const
    {
        const Take* b = nullptr;
        for (const auto& t : list)
            if (! b || t.score > b->score)
                b = &t;
        return b;
    }

    struct Comp
    {
        std::vector<int>   takeNumber;     // per phrase, 0 = nobody sang it
        std::vector<float> score;          // per phrase, percent
        double projected = 0.0;            // duration-weighted accuracy of the comp, percent
        int    phrasesCovered = 0;
    };

    /** The best take for every phrase. phraseSeconds weights the projected score. */
    Comp comp (const std::vector<double>& phraseSeconds) const
    {
        Comp c;
        size_t np = phraseSeconds.size();
        for (const auto& t : list) np = std::max (np, t.phraseScore.size());
        c.takeNumber.assign (np, 0);
        c.score.assign (np, -1.0f);
        for (const auto& t : list)
            for (size_t p = 0; p < t.phraseScore.size(); ++p)
                if (t.phraseScore[p] > c.score[p])
                {
                    c.score[p] = t.phraseScore[p];
                    c.takeNumber[p] = t.number;
                }
        double total = 0.0, weighted = 0.0;
        for (size_t p = 0; p < np; ++p)
        {
            if (c.takeNumber[p] == 0) continue;
            ++c.phrasesCovered;
            const double d = p < phraseSeconds.size() ? std::max (0.1, phraseSeconds[p]) : 1.0;
            total += d;
            weighted += d * c.score[p];
        }
        c.projected = total > 0.0 ? weighted / total : 0.0;
        return c;
    }

    /** Plain text plan to paste into notes or a Cubase comping lane name. */
    std::string compText (const std::vector<double>& phraseSeconds) const
    {
        const auto c = comp (phraseSeconds);
        std::ostringstream o;
        for (size_t p = 0; p < c.takeNumber.size(); ++p)
        {
            o << "Phrase " << (p + 1) << ": ";
            if (c.takeNumber[p] == 0) o << "not sung yet";
            else o << "take " << c.takeNumber[p] << " (" << (int) std::lround (c.score[p]) << "%)";
            o << "\n";
        }
        o << "Projected accuracy of the comp: " << (int) std::lround (c.projected) << "%\n";
        return o.str();
    }

    // ---- saved state (one compact text block) ------------------------------------
    std::string toText() const
    {
        std::ostringstream o;
        o.precision (4);
        o << "V1\n" << counter << "\n";
        for (const auto& t : list)
        {
            o << "T|" << t.number << '|' << t.score << '|' << t.accuracy << '|' << t.bestStreak << '|' << t.phraseScore.size() << '|' << t.blocks.size() << "\n";
            o << "P";
            for (float f : t.phraseScore) o << '|' << f;
            o << "\n";
            for (const auto& b : t.blocks)
                o << "B|" << b.id << '|' << b.startSec << '|' << b.endSec << '|' << b.targetMidi << '|' << (int) b.grade << '|' << b.quality
                  << '|' << b.biasCents << '|' << (b.hasBias ? 1 : 0) << '|' << b.octaveFrac << '|' << b.octaveDir << '|' << b.phrase << "\n";
        }
        return o.str();
    }

    bool fromText (const std::string& text)
    {
        std::istringstream in (text);
        std::string line;
        if (! std::getline (in, line) || line != "V1") return false;
        if (! std::getline (in, line)) return false;
        std::vector<Take> loaded;
        int loadedCounter = std::atoi (line.c_str());

        auto split = [] (const std::string& s)
        {
            std::vector<std::string> parts;
            std::string cur;
            for (char ch : s)
            {
                if (ch == '|') { parts.push_back (cur); cur.clear(); }
                else cur.push_back (ch);
            }
            parts.push_back (cur);
            return parts;
        };

        Take cur;
        bool have = false;
        size_t expectBlocks = 0;
        while (std::getline (in, line))
        {
            const auto f = split (line);
            if (f.empty()) continue;
            if (f[0] == "T" && f.size() >= 7)
            {
                if (have) loaded.push_back (cur);
                cur = Take();
                have = true;
                cur.number = std::atoi (f[1].c_str());
                cur.score = std::atoi (f[2].c_str());
                cur.accuracy = std::atoi (f[3].c_str());
                cur.bestStreak = std::atoi (f[4].c_str());
                expectBlocks = (size_t) std::atoi (f[6].c_str());
            }
            else if (f[0] == "P" && have)
            {
                for (size_t i = 1; i < f.size(); ++i) cur.phraseScore.push_back ((float) std::atof (f[i].c_str()));
            }
            else if (f[0] == "B" && have && f.size() >= 12)
            {
                BlockStat b;
                b.id = std::atoi (f[1].c_str());
                b.startSec = std::atof (f[2].c_str());
                b.endSec = std::atof (f[3].c_str());
                b.targetMidi = std::atof (f[4].c_str());
                b.grade = (Grade) std::clamp (std::atoi (f[5].c_str()), 0, 3);
                b.quality = (float) std::atof (f[6].c_str());
                b.biasCents = (float) std::atof (f[7].c_str());
                b.hasBias = f[8] == "1";
                b.octaveFrac = (float) std::atof (f[9].c_str());
                b.octaveDir = std::atoi (f[10].c_str());
                b.phrase = std::atoi (f[11].c_str());
                b.counted = true;
                cur.blocks.push_back (b);
            }
        }
        if (have) loaded.push_back (cur);
        (void) expectBlocks;

        list = std::move (loaded);
        counter = std::max (loadedCounter, list.empty() ? 0 : list.back().number);
        return true;
    }

private:
    /** Keeps the history bounded: drops the oldest take that is not the best for any phrase. */
    void trim()
    {
        while (list.size() > maxTakes)
        {
            const auto c = comp ({});
            size_t victim = 0;
            for (size_t i = 0; i < list.size(); ++i)
            {
                const bool keeper = std::find (c.takeNumber.begin(), c.takeNumber.end(), list[i].number) != c.takeNumber.end();
                if (! keeper) { victim = i; break; }
            }
            list.erase (list.begin() + (std::ptrdiff_t) victim);
        }
    }

    std::vector<Take> list;
    int counter = 0;
};
} // namespace va
