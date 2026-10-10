#include "ReferenceJob.h"

namespace va
{
AudioAnalysisResult analyseAudioFile (const juce::File& file, const SegmenterRules& rules, const PyinSettings& pyin,
                                      const std::function<bool (float)>& progress)
{
    AudioAnalysisResult r;
    std::vector<float> mono;
    double sr = 0.0;

    // Decoding is the first 10% of the bar, analysis the rest.
    if (! decodeToMono (file, mono, sr, r.error, [&] (float p) { return progress (0.1f * p); }))
    {
        r.cancelled = r.error.isEmpty();
        return r;
    }

    PyinAnalyser analyser (sr, pyin);
    PyinResult res;
    if (! analyser.analyse (mono, [&] (float p) { return progress (0.1f + 0.85f * p); }, res))
    {
        r.cancelled = true;
        return r;
    }

    r.blocks = segment (res.track, rules);
    r.info.kind = SourceKind::Audio;
    r.info.sourceName = file.getFileName();
    r.info.meanClarity = res.meanClarity;
    r.info.voicedFraction = res.voicedFraction;
    r.info.jumpRate = res.jumpRate;
    r.info.coverage = noteCoverage (res.track, r.blocks);
    r.info.quality = assessAudioQuality (res.meanClarity, res.voicedFraction, r.info.coverage);
    r.info.rules = rules;
    r.info.pyin = pyin;
    r.ok = true;
    progress (1.0f);
    return r;
}
} // namespace va
