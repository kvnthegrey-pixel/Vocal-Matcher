// Headless host test (strict document, section 7): runs the real processor and
// editor without Cubase. Loads a reference, edits it, saves and restores the
// project state, checks the audio path is untouched, and writes screenshots of
// the window if VA_SNAPSHOT_DIR is set. Exit code 0 only if all checks pass.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/MidiReference.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <random>

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
}


// A host that says where the transport is.
struct FakeHost : juce::AudioPlayHead
{
    int64_t sample = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setTimeInSamples (sample);
        p.setIsPlaying (playing);
        return p;
    }
};

// Plays a stretch of the project timeline through the plugin: block by block,
// with the transport position set from the project time. freqAt gives the sung
// frequency at a project time (0 = silence).
template <typename FreqFn>
void playSpan (VocalAssistorProcessor& p, FakeHost& host, int64_t& pos, double toSec, FreqFn freqAt, double& phase)
{
    const double sr = 48000.0;
    const int bs = 512;
    juce::MidiBuffer midi;
    const int64_t end = (int64_t) std::llround (toSec * sr);
    while (pos + bs <= end)
    {
        juce::AudioBuffer<float> buf (2, bs);
        for (int i = 0; i < bs; ++i)
        {
            const double f = freqAt ((double) (pos + i) / sr);
            float s = 0.0f;
            if (f > 0.0) { phase += 2.0 * juce::MathConstants<double>::pi * f / sr; s = 0.4f * (float) std::sin (phase); }
            buf.setSample (0, i, s); buf.setSample (1, i, s);
        }
        host.sample = pos;
        p.processBlock (buf, midi);
        pos += bs;
        pump (3);
    }
}

void liveMatchScenario()
{
    std::printf("\n== Live match against the reference (fake host transport) ==\n");
    auto p = std::make_unique<VocalAssistorProcessor>();
    p->setPlayConfigDetails (2, 2, 48000.0, 512);
    FakeHost host;
    p->setPlayHead (&host);
    p->prepareToPlay (48000.0, 512);

    std::vector<va::NoteBlock> v;
    const int pitch[] = { 60, 64, 67 };
    for (int i = 0; i < 3; ++i) { va::NoteBlock b; b.startUs = (1 + i) * 1000000; b.endUs = b.startUs + 1000000; b.midiMilli = pitch[i] * 1000; v.push_back (b); }
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("va_host_live.mid");
    f.deleteFile();
    { juce::FileOutputStream out (f); va::MidiReference::exportBlocks (v).writeTo (out); }
    check (p->loadReferenceMidi (f, 0).isEmpty(), "3 reference blocks (C4, E4, G4 at 1 to 4 s)");
    f.deleteFile();
    pump (150);
    auto blocks = p->reference.blocksCopy().blocks();
    check (blocks.size() == 3, "blocks imported");
    auto stateOf = [&] (int i) { return p->matcher().stateOf (blocks[(size_t) i].id); };
    check (stateOf (0) == va::BlockState::Blue && stateOf (1) == va::BlockState::Blue && stateOf (2) == va::BlockState::Blue, "all blocks Blue before playing");

    const double c4 = 261.6256, e4 = 329.6276;
    double phase = 0.0;
    auto pass1 = [&] (double t) -> double
    {
        if (t >= 1.0 && t < 2.0) return c4;                                   // exactly right
        if (t >= 2.0 && t < 3.0) return e4 * std::pow (2.0, 60.0 / 1200.0);   // 60 cents sharp
        if (t >= 3.0 && t < 4.0) return 440.0;                                // wrong note
        return 0.0;
    };

    host.playing = true;
    int64_t pos = 0;
    playSpan (*p, host, pos, 2.6, pass1, phase);
    pump (250); p->pumpPerformance();
    {
        auto& lv = p->matcher().live();
        check (lv.hasBlock && lv.voiced && lv.cents > 40.0 && lv.cents < 80.0, "mid-block 2: reads about 60 cents sharp (HIGH)");
        check (lv.instant == va::BlockState::Amber, "mid-block 2: instant colour Amber");
        std::printf ("  info live cents %.1f\n", lv.cents);
    }
    playSpan (*p, host, pos, 5.0, pass1, phase);
    pump (300); p->pumpPerformance();

    std::printf ("  info states %d %d %d\n", (int) stateOf (0), (int) stateOf (1), (int) stateOf (2));
    {
        auto ed = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
        pump (150);
        snapshot (*ed, "6_sung_vs_reference.png");
    }
    check (stateOf (0) == va::BlockState::Green, "block 1 (exact) is Green");
    check (stateOf (1) == va::BlockState::Amber, "block 2 (60 cents sharp) is Amber");
    check (stateOf (2) == va::BlockState::Red, "block 3 (wrong note) is Red");
    const int acc = p->matcher().accuracyPercent();
    std::printf ("  info accuracy %d%%\n", acc);
    check (acc >= 25 && acc <= 40, "accuracy is about a third (1 of 3 seconds green)");
    check (! p->trace().points().empty(), "the live trace has points");
    {
        // the trace sits where the singing was: midi 60 at 1.5 s
        const auto& pts = p->trace().points();
        size_t i = p->trace().lowerBound (1.5);
        check (i < pts.size() && pts[i].voiced && std::abs (pts[i].midi - 60.0f) < 0.1f, "trace at 1.5 s is C4 (midi 60)");
        i = p->trace().lowerBound (2.5);
        check (i < pts.size() && pts[i].voiced && std::abs (pts[i].midi - 64.6f) < 0.1f, "trace at 2.5 s is E4 + 60 cents (midi 64.6)");
    }

    // Loop: the transport jumps back to 1 s and now everything is sung right.
    auto pass2 = [&] (double t) -> double
    {
        if (t >= 1.0 && t < 2.0) return c4;
        if (t >= 2.0 && t < 3.0) return e4;
        if (t >= 3.0 && t < 4.0) return 391.9954;
        return 0.0;
    };
    pos = 43200;          // the loop jumps the transport back to 0.9 s
    playSpan (*p, host, pos, 1.9, pass2, phase);
    pump (250); p->pumpPerformance();
    check (stateOf (0) == va::BlockState::Green && stateOf (1) == va::BlockState::Blue && stateOf (2) == va::BlockState::Blue,
           "after the loop jump: block 1 re-measured, later blocks forget the old pass (Blue)");
    playSpan (*p, host, pos, 5.0, pass2, phase);
    pump (300); p->pumpPerformance();
    std::printf ("  info states %d %d %d\n", (int) stateOf (0), (int) stateOf (1), (int) stateOf (2));
    check (stateOf (0) == va::BlockState::Green && stateOf (1) == va::BlockState::Green && stateOf (2) == va::BlockState::Green,
           "second pass, all sung right: all Green");

    // Stop the transport: the matcher is left alone, the readout would show "--".
    host.playing = false;
    playSpan (*p, host, pos, 5.5, pass2, phase);
    pump (200);
    check (! p->isPlaying(), "stopped transport is reported as not playing");
    check (stateOf (2) == va::BlockState::Green, "stopping does not change the colours");

    // Settings: octave shift moves the target; a singer one octave up now matches.
    p->apvts.getParameter ("octave")->setValueNotifyingHost (p->apvts.getParameter ("octave")->convertTo0to1 (1.0f));
    pump (100); p->pumpPerformance();
    check (stateOf (0) == va::BlockState::Blue, "changing the octave shift resets the colours");
    host.playing = true;
    auto up = [&] (double t) -> double { return (t >= 1.0 && t < 2.0) ? c4 * 2.0 : 0.0; };
    pos = 43200;
    playSpan (*p, host, pos, 2.2, up, phase);
    pump (250); p->pumpPerformance();
    check (stateOf (0) == va::BlockState::Green, "octave +1: singing C5 matches the C4 block");

    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
    pump (150);
    snapshot (*editor, "5_live_match.png");
    {   // frame time of the whole window
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i) { auto img = editor->createComponentSnapshot (editor->getLocalBounds()); }
        const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0;
        std::printf ("  info window paint %.1f ms per frame (%.0f fps possible)\n", ms, 1000.0 / ms);
        check (ms < 33.0, "a frame paints in under 33 ms (30 fps)");
    }
    editor.reset();
    p->setPlayHead (nullptr);
    p->releaseResources();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    std::printf ("\n== Plugin in a headless host ==\n");
    auto proc = std::make_unique<VocalAssistorProcessor>();
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);

    check (proc->getTotalNumInputChannels() == 2 && proc->getTotalNumOutputChannels() == 2, "stereo insert");

    {   // audio is untouched: random 10 second buffer, bit-exact
        std::mt19937 rng (11);
        std::uniform_real_distribution<float> d (-1.0f, 1.0f);
        const int blocks = (int) (10.0 * 48000.0 / 512.0);
        bool exact = true;
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks && exact; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512), ref (2, 512);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (c, i, d (rng));
            ref.makeCopyOf (buf);
            proc->processBlock (buf, midi);
            for (int c = 0; c < 2 && exact; ++c)
                exact = std::memcmp (buf.getReadPointer (c), ref.getReadPointer (c), sizeof (float) * 512) == 0;
        }
        check (exact, "10 s of random audio comes out bit-identical");
        check (proc->getLatencySamples() == 0, "plugin reports 0 samples of latency");
    }

    std::printf ("\n== First open: no reference, prompt shown ==\n");
    {
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (proc->createEditor());
        pump (100);
        check (! proc->reference.hasBlocks(), "no blocks yet");
        snapshot (*editor, "1_first_open.png");
    }

    std::printf ("\n== MIDI reference ==\n");
    {
        std::vector<va::NoteBlock> v;
        const int pitch[] = { 60, 62, 64, 65, 67, 65, 64, 62, 60 };
        for (int i = 0; i < 9; ++i) { va::NoteBlock b; b.startUs = i * 600000; b.endUs = b.startUs + 500000; b.midiMilli = pitch[i] * 1000; v.push_back (b); }
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("va_host_test.mid");
        f.deleteFile();
        { juce::FileOutputStream out (f); va::MidiReference::exportBlocks (v).writeTo (out); }
        check (proc->loadReferenceMidi (f, 0).isEmpty(), "MIDI file imported");
        check (proc->reference.blocksCopy().blocks().size() == 9 && proc->reference.infoCopy().quality == va::Quality::Green, "9 blocks, quality Green");
        f.deleteFile();
    }

    std::printf ("\n== Audio reference (experimental) in the background ==\n");
    {
        auto path = juce::SystemStats::getEnvironmentVariable ("VA_TEST_AUDIO", {});
        if (path.isEmpty())
            std::printf ("  skip no VA_TEST_AUDIO file given\n");
        else
        {
            check (proc->loadReferenceAudio (juce::File (path)), "analysis started");
            check (proc->isAnalysing(), "runs on its own thread (isAnalysing)");
            int waited = 0;
            while (proc->isAnalysing() && waited < 120000) { pump (50); waited += 50; }
            pump (300);   // let the timer deliver the result
            check (proc->reference.hasBlocks() && proc->reference.infoCopy().kind == va::SourceKind::Audio, "blocks arrived from the stem");
            std::printf ("  info %d blocks, quality %s\n", (int) proc->reference.blocksCopy().blocks().size(), va::qualityName (proc->reference.infoCopy().quality));
        }
    }

    std::printf ("\n== Edit, lock, save, restore ==\n");
    {
        auto before = proc->reference.blocksCopy();
        const int firstId = before.blocks().front().id;
        proc->reference.shift ({ firstId }, 3);
        proc->reference.remove ({ before.blocks()[1].id });
        proc->reference.setLocked (true);
        auto edited = proc->reference.blocksCopy();
        check (edited.blocks().size() + 1 == before.blocks().size() && edited.isLocked(), "edited and locked");

        juce::MemoryBlock state;
        proc->getStateInformation (state);

        auto other = std::make_unique<VocalAssistorProcessor>();
        other->setStateInformation (state.getData(), (int) state.getSize());
        auto restored = other->reference.blocksCopy();
        bool same = restored.blocks().size() == edited.blocks().size() && restored.isLocked();
        for (size_t i = 0; same && i < edited.blocks().size(); ++i)
        {
            auto &a = edited.blocks()[i], &b = restored.blocks()[i];
            same = a.id == b.id && a.startUs == b.startUs && a.endUs == b.endUs && a.midiMilli == b.midiMilli;
        }
        check (same, "a fresh plugin instance restores exactly the saved blocks (no re-analysis)");
        check (other->reference.infoCopy().sourceName == proc->reference.infoCopy().sourceName
                   && other->reference.infoCopy().quality == proc->reference.infoCopy().quality, "source name and quality restored");
        check (! other->isAnalysing(), "restoring does not start an analysis");

        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (proc->createEditor());
        pump (150);
        snapshot (*editor, "2_loaded_locked.png");
        proc->reference.setLocked (false);
        pump (150);
        snapshot (*editor, "3_loaded_unlocked.png");
    }

    std::printf ("\n== Live pitch still works next to the reference ==\n");
    {
        juce::MidiBuffer midi;
        for (int b = 0; b < 200; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512);
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 261.63f * (float) (b * 512 + i) / 48000.0f);
                buf.setSample (0, i, s); buf.setSample (1, i, s);
            }
            proc->processBlock (buf, midi);
            pump (2);
        }
        pump (200);
        va::PitchPoint last;
        check (proc->getLatestPoint (last) && last.voiced && std::abs (va::hzToCents (last.hz, 261.63)) < 5.0, "C4 sine reads within 5 cents");
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (proc->createEditor());
        pump (100);
        snapshot (*editor, "4_with_live_pitch.png");
    }

    proc->releaseResources();
    liveMatchScenario();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL HOST TESTS PASSED" : "HOST TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
