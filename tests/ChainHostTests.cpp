// Headless host test for Vocal Chain: layouts, latency, presets, state, audio sanity, editor snapshot.
#include "../Source/Chain/ChainProcessor.h"
#include "../Source/Chain/ChainEditor.h"
#include <cstdio>
#include <cmath>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("  %-4s %s\n", ok ? "ok" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}
void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

void snapshot (juce::Component& c, const juce::String& name)
{
    auto dir = juce::SystemStats::getEnvironmentVariable ("VA_SNAPSHOT_DIR", {});
    if (dir.isEmpty()) return;
    auto img = c.createComponentSnapshot (c.getLocalBounds());
    juce::PNGImageFormat png;
    auto f = juce::File (dir).getChildFile (name);
    f.deleteFile();
    juce::FileOutputStream out (f);
    png.writeImageToStream (img, out);
}

// runs a 220 Hz voice-like tone through the plugin; returns peak of the last second and whether anything was non-finite
struct Run { float peak = 0; bool finite = true; };
Run runTone (VocalChainProcessor& p, int channelsIn, int channelsOut, double seconds)
{
    const double sr = 48000.0; const int bs = 480;
    Run r; juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (std::max (channelsIn, channelsOut), bs);
    const int total = (int) (seconds * sr / bs);
    for (int b = 0; b < total; ++b)
    {
        buf.clear();
        for (int c = 0; c < channelsIn; ++c)
            for (int i = 0; i < bs; ++i)
            {
                const double t = (double) (b * bs + i) / sr;
                buf.setSample (c, i, (float) (0.25 * std::sin (2 * juce::MathConstants<double>::pi * 220.0 * t)
                                             + 0.1 * std::sin (2 * juce::MathConstants<double>::pi * 440.0 * t)));
            }
        p.processBlock (buf, midi);
        if (b > total - 100)
            for (int c = 0; c < channelsOut; ++c)
                for (int i = 0; i < bs; ++i)
                {
                    const float v = buf.getSample (c, i);
                    if (! std::isfinite (v)) r.finite = false;
                    r.peak = std::max (r.peak, std::abs (v));
                }
    }
    return r;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf ("\n== Vocal Chain in a headless host ==\n");

    auto proc = std::make_unique<VocalChainProcessor>();
    check (juce::String (proc->getName()) == "Vocal Chain", "plugin name is Vocal Chain");

    using CS = juce::AudioChannelSet;
    auto layout = [&] (CS in, CS out) { juce::AudioProcessor::BusesLayout l; l.inputBuses.add (in); l.outputBuses.add (out); return proc->isBusesLayoutSupported (l); };
    check (layout (CS::stereo(), CS::stereo()), "stereo -> stereo supported");
    check (layout (CS::mono(), CS::mono()), "mono -> mono supported");
    check (layout (CS::mono(), CS::stereo()), "mono -> stereo supported");
    check (! layout (CS::stereo(), CS::mono()), "stereo -> mono refused");

    proc->setPlayConfigDetails (2, 2, 48000.0, 480);
    proc->prepareToPlay (48000.0, 480);
    check (proc->getLatencySamples() == 2048, "reports 2048 samples latency");

    {
        auto r = runTone (*proc, 2, 2, 3.0);
        check (r.finite, "stereo output is finite");
        check (r.peak > 0.02f && r.peak < 1.0f, "stereo output has signal and stays under full scale (" + juce::String (r.peak, 3) + ")");
    }

    for (int i = 0; i < (int) vc::presets().size(); ++i)
    {
        proc->applyPreset (i);
        auto r = runTone (*proc, 2, 2, 2.0);
        check (r.finite && r.peak < 1.0f, juce::String ("preset ok: ") + vc::presets()[(size_t) i].name + " peak " + juce::String (r.peak, 3));
    }
    proc->applyPreset (2);
    check (std::abs (proc->apvts.getRawParameterValue ("revDecay")->load() - 5.5f) < 0.1f, "Dreamy Shimmer sets 5.5 s decay");
    proc->applyPreset (5);
    check (proc->apvts.getRawParameterValue ("tuneOn")->load() < 0.5f, "Natural preset turns tuning off");

    {   // state round trip
        proc->applyPreset (2);
        juce::MemoryBlock mb; proc->getStateInformation (mb);
        auto other = std::make_unique<VocalChainProcessor>();
        other->setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (other->apvts.getRawParameterValue ("revMix")->load() - 36.0f) < 0.6f, "saved project restores reverb mix");
    }

    {   // mono -> mono
        auto m = std::make_unique<VocalChainProcessor>();
        juce::AudioProcessor::BusesLayout l; l.inputBuses.add (CS::mono()); l.outputBuses.add (CS::mono());
        check (m->setBusesLayout (l), "mono layout accepted");
        m->prepareToPlay (44100.0, 256);
        auto r = runTone (*m, 1, 1, 2.0);
        check (r.finite && r.peak > 0.02f && r.peak < 1.0f, "mono -> mono works");
    }
    {   // mono -> stereo
        auto m = std::make_unique<VocalChainProcessor>();
        juce::AudioProcessor::BusesLayout l; l.inputBuses.add (CS::mono()); l.outputBuses.add (CS::stereo());
        check (m->setBusesLayout (l), "mono->stereo layout accepted");
        m->prepareToPlay (44100.0, 256);
        auto r = runTone (*m, 1, 2, 2.0);
        check (r.finite && r.peak > 0.02f && r.peak < 1.0f, "mono -> stereo works");
    }

    {   // odd block sizes: 1 sample, 7, 1000 and 4096 (host may exceed prepared size)
        proc->applyPreset (0);
        juce::MidiBuffer midi; bool ok = true;
        for (int bs : { 1, 7, 64, 1000, 4096, 17 })
        {
            juce::AudioBuffer<float> buf (2, bs);
            for (int rep = 0; rep < 5; ++rep)
            {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) buf.setSample (c, i, 0.2f * std::sin ((float) (i + rep) * 0.05f));
                proc->processBlock (buf, midi);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) if (! std::isfinite (buf.getSample (c, i))) ok = false;
            }
        }
        check (ok, "odd block sizes stay finite");
    }

    {
        proc->applyPreset (0);
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (proc->createEditor());
        check (editor != nullptr && editor->getWidth() == 1180, "editor opens at 1180 wide");
        runTone (*proc, 2, 2, 1.0);
        pump (250);
        snapshot (*editor, "chain_1_sweet_pop.png");
        proc->applyPreset (2);
        pump (150);
        snapshot (*editor, "chain_2_dreamy.png");
    }

    proc->releaseResources();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL CHAIN HOST TESTS PASSED" : "CHAIN HOST TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
