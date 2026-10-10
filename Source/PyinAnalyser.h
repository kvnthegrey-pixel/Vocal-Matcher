#pragma once

#include "NoteSegmenter.h"
#include <functional>

namespace va
{
struct PyinSettings
{
    double minHz = 80.0, maxHz = 1200.0;
    int    frameSize = 2048;        // samples per analysis frame
    int    hopSize   = 512;         // samples between frames
    int    centsPerBin = 10;        // pitch resolution of the HMM
    int    maxJumpBins = 30;        // largest pitch move between two frames inside a note (300 cents)
    double switchProb = 0.01;       // voiced <-> unvoiced switch probability per frame
    double betaA = 2.0, betaB = 18.0;   // prior over the YIN threshold (mean 0.1)
};

struct PyinResult
{
    FrameTrack track;
    double meanClarity = 0.0;       // mean of (1 - CMND) over voiced frames
    double voicedFraction = 0.0;    // voiced share of the loud frames (rests are not counted)
    double jumpRate = 0.0;          // share of voiced neighbours more than 7 semitones apart
};

/** Offline probabilistic YIN (pYIN, Mauch and Dixon 2014): many YIN thresholds
    give weighted pitch candidates for each frame, and a Viterbi search over a
    voiced/unvoiced hidden Markov model picks the best path. Meant for a
    background thread. Not used on the live path. */
class PyinAnalyser
{
public:
    explicit PyinAnalyser (double sampleRate, PyinSettings s = {});

    /** progress gets 0..1 and returns false to cancel. Returns false if cancelled. */
    bool analyse (const std::vector<float>& mono, const std::function<bool (float)>& progress, PyinResult& out) const;

private:
    double sr;
    PyinSettings cfg;
};
} // namespace va
