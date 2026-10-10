// Phase 1 spike: confirms the vendored pitch_detector module compiles under
// CMake and gives a first accuracy reading on pure sine waves at the four
// pitches named in the strict document. The pass/fail gate for these tests is
// Phase 2; here the only hard failure is a sine that is off by more than 5
// cents, because that would mean the library cannot meet the requirement.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <pitch_detector/pitch_detector.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int windowSize = 2048;

double centsBetween (double measuredHz, double trueHz)
{
    return 1200.0 * std::log2 (measuredHz / trueHz);
}

std::vector<float> makeSine (double hz)
{
    std::vector<float> buf ((size_t) windowSize);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / sampleRate));
    return buf;
}
}

int main()
{
    int failures = 0;

    adamski::PitchMPM mpm ((int) sampleRate, (size_t) windowSize);

    std::printf ("McLeod (MPM), window %d samples at %.0f Hz\n", windowSize, sampleRate);

    for (double hz : { 110.0, 220.0, 440.0, 880.0 })
    {
        auto buf = makeSine (hz);
        const float measured = mpm.getPitch (buf.data());

        if (measured <= 0.0f)
        {
            std::printf ("  sine %6.1f Hz -> no pitch reported   FAIL\n", hz);
            ++failures;
            continue;
        }

        const double cents = centsBetween (measured, hz);
        const bool ok = std::abs (cents) <= 5.0;
        std::printf ("  sine %6.1f Hz -> %8.3f Hz  (%+6.2f cents)   %s\n", hz, measured, cents, ok ? "ok" : "FAIL");
        if (! ok)
            ++failures;
    }

    // Informational only: Phase 2 adds the voicing gate that has to make
    // silence and noise report no pitch.
    {
        std::vector<float> silence ((size_t) windowSize, 0.0f);
        std::printf ("  silence        -> %f (information only)\n", (double) mpm.getPitch (silence.data()));

        std::mt19937 rng (12345);
        std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
        std::vector<float> noise ((size_t) windowSize);
        for (auto& s : noise)
            s = dist (rng);
        std::printf ("  white noise    -> %f (information only)\n", (double) mpm.getPitch (noise.data()));
    }

    std::printf (failures == 0 ? "Spike result: sine accuracy within 5 cents.\n"
                               : "Spike result: %d sine check(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
