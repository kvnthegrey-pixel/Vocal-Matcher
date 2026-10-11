#include "EqMatch.h"
#include "ReferenceModel.h"

namespace va
{
bool analyseToneFile (const juce::File& file, BandDb& out, juce::String& error, const std::atomic<bool>& cancel)
{
    std::vector<float> mono;
    double sr = 0.0;
    if (! decodeToMono (file, mono, sr, error, [&cancel] (float) { return ! cancel.load(); }))
        return false;
    BandSpectrum spec (sr);
    const int chunk = 1 << 15;
    for (size_t i = 0; i < mono.size(); i += (size_t) chunk)
    {
        if (cancel.load()) { error = "cancelled"; return false; }
        spec.add (mono.data() + i, (int) std::min<size_t> ((size_t) chunk, mono.size() - i));
    }
    if (! spec.result (out))
    {
        error = "That file is too quiet or too short to measure the tone (it needs at least a second of singing).";
        return false;
    }
    return true;
}
} // namespace va
