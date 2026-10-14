// Headless host test for Performance Assist: the real processor and editor without Cubase.
// Checks the audio is untouched, grades a sung pass against a reference, files takes at the
// end of a pass, at a loop jump and at stop, saves and restores a project, and renders the
// screens (to PNG files when VA_SNAPSHOT_DIR is set). Exit code 0 only if all checks pass.
#include "../Source/Assist/AssistProcessor.h"
#include "../Source/Assist/AssistEditor.h"
#include "../Source/MidiReference.h"

#include <cstdio>
#include <cstring>
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

template <typename FreqFn>
void playSpan (AssistProcessor& p, FakeHost& host, int64_t& pos, double toSec, FreqFn freqAt, double& phase)
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

void settle (AssistProcessor& p, int ticks = 12)
{
    for (int i = 0; i < ticks; ++i)
    {
        pump (40);
        p.pumpPerformance();
    }
}

const double c4 = 261.6256, e4 = 329.6276, g4 = 391.9954;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    std::printf ("\n== Plugin in a headless host ==\n");
    auto p = std::make_unique<AssistProcessor>();
    p->setPlayConfigDetails (2, 2, 48000.0, 512);
    FakeHost host;
    p->setPlayHead (&host);
    p->prepareToPlay (48000.0, 512);

    check (p->getTotalNumInputChannels() == 2 && p->getTotalNumOutputChannels() == 2, "stereo insert");
    check (p->getName() == "Performance Assist", "plugin name is Performance Assist");
    {
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> d (-1.0f, 1.0f);
        bool exact = true;
        juce::MidiBuffer midi;
        host.playing = true;
        for (int b = 0; b < 400 && exact; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512), ref (2, 512);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (c, i, d (rng));
            ref.makeCopyOf (buf);
            host.sample = (int64_t) b * 512;
            p->processBlock (buf, midi);
            for (int c = 0; c < 2 && exact; ++c)
                exact = std::memcmp (buf.getReadPointer (c), ref.getReadPointer (c), sizeof (float) * 512) == 0;
        }
        check (exact, "random audio comes out bit-identical while playing");
        check (p->getLatencySamples() == 0, "plugin reports 0 samples of latency");
        settle (*p, 4);
    }

    std::printf ("\n== First open: empty screens render ==\n");
    {
        auto ed = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
        pump (100);
        check (! p->reference.hasBlocks(), "no reference yet");
        snapshot (*ed, "pa_1_first_open.png");
    }

    std::printf ("\n== Reference: C4, E4 in one phrase, G4 in the next ==\n");
    {
        std::vector<va::NoteBlock> v;
        auto add = [&] (double s, double e, int midi) { va::NoteBlock b; b.startUs = (int64_t) (s * 1e6); b.endUs = (int64_t) (e * 1e6); b.midiMilli = midi * 1000; v.push_back (b); };
        add (1.0, 2.0, 60); add (2.2, 3.2, 64); add (5.0, 6.0, 67);
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pa_host_ref.mid");
        f.deleteFile();
        { juce::FileOutputStream out (f); va::MidiReference::exportBlocks (v).writeTo (out); }
        check (p->loadReferenceMidi (f, 0).isEmpty(), "3 reference blocks imported");
        f.deleteFile();
        pump (200);
        check (p->reference.blocksCopy().blocks().size() == 3, "blocks are in the model");
        check (p->score().phraseCount() == 2, "the score sees two phrases");
    }
    const auto ids = [&] { std::vector<int> r; const auto list = p->reference.blocksCopy(); for (auto& b : list.blocks()) r.push_back (b.id); return r; }();

    std::printf ("\n== Pass 1: right, 60 cents sharp, right ==\n");
    double phase = 0.0;
    int64_t pos = 0;
    auto pass1 = [&] (double t) -> double
    {
        if (t >= 1.0 && t < 2.0) return c4;
        if (t >= 2.2 && t < 3.2) return e4 * std::pow (2.0, 60.0 / 1200.0);
        if (t >= 5.0 && t < 6.0) return g4;
        return 0.0;
    };
    host.playing = true;
    playSpan (*p, host, pos, 2.7, pass1, phase);
    settle (*p, 3);
    {
        const auto& live = p->score().liveNote();
        check (live.hasBlock && live.voiced && live.cents > 40.0 && live.cents < 80.0, "mid-note: reads about 60 cents sharp");
        std::printf ("  info live cents %.1f\n", live.cents);
        auto ed = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
        pump (120);
        snapshot (*ed, "pa_2a_mid_performance.png");
    }
    playSpan (*p, host, pos, 6.6, pass1, phase);
    settle (*p, 3);
    std::printf ("  info grades %d %d %d, ids %d %d %d, takes %d, counted %d, score %d\n",
                 (int) p->score().gradeOf (ids[0]), (int) p->score().gradeOf (ids[1]), (int) p->score().gradeOf (ids[2]),
                 ids[0], ids[1], ids[2], (int) p->takeStore().takes().size(), p->score().countedCount(), p->score().score());
    check (p->score().gradeOf (ids[0]) == va::Grade::Perfect, "C4 sung right is Perfect");
    check (p->score().gradeOf (ids[1]) == va::Grade::Miss, "E4 sung 60 cents sharp is a Miss");
    check (p->score().gradeOf (ids[2]) == va::Grade::Perfect, "G4 sung right is Perfect");
    check (p->score().score() == 100 + 0 + 100, "score is 200 (the miss reset the streak)");
    check (p->takeStore().takes().empty(), "no take yet: the pass is still playing");

    // the voice wave
    {
        const auto& env = p->envelope();
        float mn = 0, mx = 0;
        check (! env.points().empty(), "the voice envelope has slices");
        check (env.range (1.4, 1.6, mn, mx) && mx > 0.3f && mx < 0.5f && mn < -0.3f, "at 1.5 s the wave peaks near +-0.4 (the sine we sang)");
        check (env.range (4.0, 4.5, mn, mx) && std::max (std::abs (mn), std::abs (mx)) < 0.01f, "between notes the wave is flat (silence)");
        const auto& tr = p->trace().points();
        const size_t i = p->trace().lowerBound (1.5);
        check (i < tr.size() && tr[i].voiced && std::abs (tr[i].midi - 60.0f) < 0.1f, "the pitch line at 1.5 s is C4");
    }

    {
        auto ed = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
        pump (150);
        snapshot (*ed, "pa_2_perform.png");
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i) { auto img = ed->createComponentSnapshot (ed->getLocalBounds()); }
        const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0;
        std::printf ("  info window paint %.1f ms per frame\n", ms);
        check (ms < 33.0, "a frame of the performance screen paints in under 33 ms");
    }

    std::printf ("\n== Stop files the take ==\n");
    host.playing = false;
    playSpan (*p, host, pos, 7.2, [] (double) { return 0.0; }, phase);
    settle (*p, 14);
    check (p->takeStore().takes().size() == 1, "stopping the transport files take 1");
    if (! p->takeStore().takes().empty())
    {
        const auto& t = p->takeStore().takes()[0];
        check (t.score == 200 && t.blocks.size() == 3, "take 1 holds the score and the three notes");
        check (t.phraseScore.size() == 2 && t.phraseScore[0] > 30.0f && t.phraseScore[0] < 70.0f && t.phraseScore[1] > 90.0f,
               "phrase 1 scored about half (one right, one missed), phrase 2 scored high");
        std::printf ("  info %s\n", va::tendencyText (t).c_str());
    }

    std::printf ("\n== A loop jump files the pass before it, and the stop files the last one ==\n");
    auto good = [&] (double t) -> double
    {
        if (t >= 1.0 && t < 2.0) return c4;
        if (t >= 2.2 && t < 3.2) return e4;
        if (t >= 5.0 && t < 6.0) return g4;
        return 0.0;
    };
    host.playing = true;
    pos = 43200;                                     // 0.9 s
    playSpan (*p, host, pos, 3.2, good, phase);      // the cycle ends exactly where E4 ends
    settle (*p, 2);
    pos = 43200;                                     // the loop jumps back
    playSpan (*p, host, pos, 3.2, good, phase);
    settle (*p, 3);
    check (p->takeStore().takes().size() == 2, "the jump filed the first loop pass as take 2 (its last note ended at the cycle end)");
    host.playing = false;
    playSpan (*p, host, pos, 3.8, [] (double) { return 0.0; }, phase);
    settle (*p, 14);
    check (p->takeStore().takes().size() == 3, "stopping filed the second loop pass as take 3");
    if (p->takeStore().takes().size() == 3)
    {
        const auto& t2 = p->takeStore().takes()[1];
        check (t2.blocks.size() == 2 && t2.phraseScore.size() == 2 && t2.phraseScore[0] > 90.0f && t2.phraseScore[1] < 0.0f,
               "a loop of phrase 1 scores phrase 1 only (phrase 2 not sung)");
        const auto comp = p->takeStore().comp (p->phraseSeconds());
        check (comp.takeNumber.size() == 2 && comp.takeNumber[0] != 1 && comp.takeNumber[1] == 1,
               "comp plan: phrase 1 from a later (better) take, phrase 2 from take 1");
        std::printf ("%s", p->takeStore().compText (p->phraseSeconds()).c_str());
    }

    std::printf ("\n== Ghost mode and focus ==\n");
    {
        p->apvts.getParameter ("mode")->setValueNotifyingHost (1.0f);
        if (auto* focus = dynamic_cast<juce::AudioParameterInt*> (p->apvts.getParameter ("focus")))
            *focus = 2;
        pump (100); p->pumpPerformance();
        check (p->ghostMode(), "mode switches to Ghost");
        check (p->score().settings().focusPhrase == 1, "focus 2 means the second phrase (index 1)");
        auto ed = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
        pump (150);
        snapshot (*ed, "pa_3_ghost_focus.png");
        p->apvts.getParameter ("mode")->setValueNotifyingHost (0.0f);
        if (auto* focus = dynamic_cast<juce::AudioParameterInt*> (p->apvts.getParameter ("focus")))
            *focus = 0;
        pump (100); p->pumpPerformance();
        check (p->score().settings().focusPhrase == -1 && ! p->ghostMode(), "back to all phrases, Play mode");
    }

    std::printf ("\n== Takes screen and Settings screen render ==\n");
    {
        auto ed = std::unique_ptr<AssistEditor> (dynamic_cast<AssistEditor*> (p->createEditor()));
        check (ed != nullptr, "editor is an AssistEditor");
        pump (150);
        ed->showTab (AssistEditor::Tab::Takes);
        pump (150);
        snapshot (*ed, "pa_4_takes.png");
        ed->showTab (AssistEditor::Tab::Edit);
        pump (150);
        snapshot (*ed, "pa_5_edit.png");
        ed->showTab (AssistEditor::Tab::Settings);
        pump (150);
        snapshot (*ed, "pa_6_settings.png");
    }

    std::printf ("\n== Saved project: reference, takes and settings come back ==\n");
    {
        p->apvts.getParameter ("octave")->setValueNotifyingHost (p->apvts.getParameter ("octave")->convertTo0to1 (1.0f));
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        check (saved.getSize() > 0, "state is written");

        auto q = std::make_unique<AssistProcessor>();
        q->setPlayConfigDetails (2, 2, 48000.0, 512);
        q->setStateInformation (saved.getData(), (int) saved.getSize());
        pump (200); q->pumpPerformance();
        check (q->reference.blocksCopy().blocks().size() == 3, "the reference is restored with no re-analysis");
        check (q->takeStore().takes().size() == 3, "all three takes are restored");
        check (q->score().phraseCount() == 2, "phrases are rebuilt");
        check (std::lround (q->apvts.getRawParameterValue ("octave")->load()) == 1, "settings are restored");
        check (q->takeStore().comp (q->phraseSeconds()).takeNumber == p->takeStore().comp (p->phraseSeconds()).takeNumber,
               "the comp plan is the same after a reload");

        juce::MemoryBlock junk;
        junk.append ("not a project", 13);
        q->setStateInformation (junk.getData(), (int) junk.getSize());
        check (q->takeStore().takes().size() == 3, "corrupt state is ignored, takes untouched");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    p.reset();
    return failures == 0 ? 0 : 1;
}
