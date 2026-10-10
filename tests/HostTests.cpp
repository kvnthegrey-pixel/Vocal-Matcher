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
        va::PitchPoint pts[512];
        const int n = proc->engine.popPoints (pts, 512);
        check (n > 0 && pts[n - 1].voiced && std::abs (va::hzToCents (pts[n - 1].hz, 261.63)) < 5.0, "C4 sine reads within 5 cents");
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (proc->createEditor());
        pump (100);
        snapshot (*editor, "4_with_live_pitch.png");
    }

    proc->releaseResources();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL HOST TESTS PASSED" : "HOST TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
