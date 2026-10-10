#include "Matcher.h"
#include <cmath>

namespace va
{
void Matcher::setBlocks (const std::vector<NoteBlock>& blocks)
{
    recs.clear();
    for (auto& b : blocks)
    {
        Rec r;
        r.id = b.id;
        r.startSec = b.startSec();
        r.endSec = b.endSec();
        r.pitch = b.midi();
        recs.push_back (r);
    }
    liveMatch = {};
    recompute();
}

void Matcher::setSettings (const MatchSettings& s)
{
    const bool moved = s.octaveShift != cfg.octaveShift || s.refOffsetSec != cfg.refOffsetSec || s.a4 != cfg.a4;
    cfg = s;
    if (moved)
        resetAll();          // results were measured against the old reference position or pitch
}

void Matcher::resetAll()
{
    for (auto& r : recs)
    {
        r.activeSec = r.greenSec = r.amberSec = 0.0;
        r.reached = r.passed = false;
        r.earned = r.instant = BlockState::Blue;
    }
    liveMatch = {};
    recompute();
}

void Matcher::resetFrom (double projectSec)
{
    for (auto& r : recs)
        if (projectEnd (r) > projectSec)
        {
            r.activeSec = r.greenSec = r.amberSec = 0.0;
            r.reached = r.passed = false;
            r.earned = r.instant = BlockState::Blue;
        }
    liveMatch = {};
    recompute();
}

void Matcher::recompute()
{
    totalActive = totalGreen = 0.0;
    for (auto& r : recs)
    {
        totalActive += r.activeSec;
        totalGreen += r.greenSec;
    }
}

void Matcher::onPoint (double t, double frameSec, bool voiced, double hz)
{
    liveMatch = {};
    for (auto& r : recs)
    {
        if (t < projectStart (r) || t >= projectEnd (r)) continue;

        r.reached = true;
        liveMatch.hasBlock = true;
        liveMatch.voiced = voiced;

        BlockState inst = BlockState::Red;
        if (voiced && hz > 0.0)
        {
            const double nominal = cfg.a4 * std::pow (2.0, (r.pitch + 12.0 * cfg.octaveShift - 69.0) / 12.0);
            const double cents = 1200.0 * std::log2 (hz / nominal);
            liveMatch.cents = cents;
            const double ac = std::abs (cents);
            if (ac <= cfg.greenCents) inst = BlockState::Green;
            else if (ac <= cfg.amberCents) inst = BlockState::Amber;
        }
        liveMatch.instant = inst;
        r.instant = inst;

        r.activeSec += frameSec;
        totalActive += frameSec;
        if (inst == BlockState::Green) { r.greenSec += frameSec; totalGreen += frameSec; }
        if (inst != BlockState::Red) r.amberSec += frameSec;

        const double dur = r.endSec - r.startSec;
        if (r.greenSec >= 0.5 * dur)
            r.earned = BlockState::Green;
        else if (r.earned != BlockState::Green && r.amberSec >= 0.5 * dur)
            r.earned = BlockState::Amber;
        break;
    }
}

void Matcher::onPlayhead (double t)
{
    for (auto& r : recs)
        if (r.reached && ! r.passed && t >= projectEnd (r) + 0.12)    // 0.12 s lets the last frames arrive
            r.passed = true;
}

BlockState Matcher::stateOf (int blockId) const
{
    for (auto& r : recs)
    {
        if (r.id != blockId) continue;
        if (! r.reached) return BlockState::Blue;
        if (r.earned == BlockState::Green || r.earned == BlockState::Amber) return r.earned;
        return r.passed ? BlockState::Red : r.instant;
    }
    return BlockState::Blue;
}

int Matcher::accuracyPercent() const
{
    if (totalActive <= 0.0) return 0;
    return (int) std::floor (100.0 * totalGreen / totalActive + 0.5);
}
} // namespace va
