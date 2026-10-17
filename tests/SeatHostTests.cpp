// Headless host test for Mix Seat: the real processor and editor without Cubase.
// Checks the audio is untouched until there is something to do, that an uploaded beat and a played
// vocal produce the expected EQ (cut where the beat is crowded, lift where it has room), that
// following the beat changes the EQ through the song, bypass and trim, freezing, saving and
// restoring a project, bad files, mono, other sample rates, and renders the screen
// (PNG files when VA_SNAPSHOT_DIR is set). Exit code 0 only if every check passes.
#include "../Source/Seat/SeatProcessor.h"
#include "../Source/Seat/SeatEditor.h"

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
    double sr = 48000.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setTimeInSamples (sample);
        p.setTimeInSeconds ((double) sample / sr);
        p.setIsPlaying (true);
        return p;
    }
};

const double kPi = juce::MathConstants<double>::pi;

/** Steady sound with equal energy in every third-octave band between lo and hi. */
std::vector<float> bandSignal (double sr, double seconds, double lo, double hi, float level, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> ph (0.0, 2.0 * kPi);
    std::vector<double> freqs, phases;
    for (double f = lo; f <= hi; f *= std::pow (2.0, 1.0 / 24.0)) { freqs.push_back (f); phases.push_back (ph (rng)); }
    std::vector<float> x ((size_t) (seconds * sr), 0.0f);
    const double amp = level / std::sqrt ((double) freqs.size());
    for (size_t k = 0; k < freqs.size(); ++k)
    {
        const double w = 2.0 * kPi * freqs[k] / sr;
        for (size_t i = 0; i < x.size(); ++i)
            x[i] += (float) (amp * std::sin (w * (double) i + phases[k]));
    }
    return x;
}

/** Two signals added together. */
std::vector<float> mixOf (const std::vector<float>& a, const std::vector<float>& b)
{
    std::vector<float> o (std::max (a.size(), b.size()), 0.0f);
    for (size_t i = 0; i < a.size(); ++i) o[i] += a[i];
    for (size_t i = 0; i < b.size(); ++i) o[i] += b[i];
    return o;
}

juce::File writeWav (const std::vector<float>& x, double sr, const char* name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> out (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), sr, 1, 16, {}, 0));
    out.release();
    const float* ch[1] = { x.data() };
    w->writeFromFloatArrays (ch, 1, (int) x.size());
    return f;
}

/** Steady voice-like sound: harmonics of 200 Hz with a gentle slope, and a slow vibrato. */
float voiceSample (double t)
{
    double s = 0.0;
    const double f0 = 200.0 * (1.0 + 0.01 * std::sin (2.0 * kPi * 5.0 * t));
    for (int h = 1; h <= 40; ++h)
        s += std::sin (2.0 * kPi * f0 * h * t) / std::pow ((double) h, 0.7);
    return (float) (0.12 * s);
}

struct Rig
{
    explicit Rig (int channels = 2, double rate = 48000.0)
        : p (std::make_unique<SeatProcessor>()), ch (channels), sr (rate)
    {
        p->setPlayConfigDetails (ch, ch, sr, 512);
        host.sr = sr;
        p->setPlayHead (&host);
        p->prepareToPlay (sr, 512);
    }

    /** Runs `seconds` of audio from the song position `fromSec` through the plugin and returns the output. */
    template <typename Fn>
    std::vector<float> run (double fromSec, double seconds, Fn gen, int tickEvery = 4)
    {
        juce::MidiBuffer midi;
        std::vector<float> out;
        const int bs = 512;
        int64_t pos = (int64_t) std::llround (fromSec * sr);
        const int64_t end = pos + (int64_t) std::llround (seconds * sr);
        int block = 0;
        while (pos + bs <= end)
        {
            juce::AudioBuffer<float> buf (ch, bs);
            for (int i = 0; i < bs; ++i)
            {
                const float s = gen ((double) (pos + i) / sr);
                for (int c = 0; c < ch; ++c) buf.setSample (c, i, s);
            }
            host.sample = pos;
            p->processBlock (buf, midi);
            for (int i = 0; i < bs; ++i) out.push_back (buf.getSample (0, i));
            pos += bs;
            if (++block % tickEvery == 0) { p->pumpSeat(); pump (1); }
        }
        return out;
    }

    void settle (int ticks = 30) { for (int i = 0; i < ticks; ++i) { pump (15); p->pumpSeat(); } }

    bool waitForJob (int maxMs = 30000)
    {
        for (int waited = 0; waited < maxMs; waited += 30)
        {
            pump (30);
            p->pumpSeat();
            if (! p->isAnalysing()) { pump (30); p->pumpSeat(); return true; }
        }
        return false;
    }

    std::unique_ptr<SeatProcessor> p;
    FakeHost host;
    int ch;
    double sr;
};

double rms (const std::vector<float>& x, size_t from = 0)
{
    double s = 0.0; size_t n = 0;
    for (size_t i = from; i < x.size(); ++i) { s += (double) x[i] * x[i]; ++n; }
    return n ? std::sqrt (s / (double) n) : 0.0;
}

/** Gain (dB) the plugin applies to a steady tone at `hz`, measured on the output after it has settled. */
double toneGainDb (Rig& r, double hz, double startSec)
{
    auto tone = [hz] (double t) { return (float) (0.2 * std::sin (2.0 * kPi * hz * t)); };
    const auto out = r.run (startSec, 1.2, tone);
    std::vector<float> in (out.size());
    for (size_t i = 0; i < in.size(); ++i) in[i] = tone (startSec + (double) i / r.sr);
    const size_t skip = out.size() / 3;
    std::vector<float> a (out.begin() + (long) skip, out.end()), b (in.begin() + (long) skip, in.end());
    return 20.0 * std::log10 (rms (a) / rms (b));
}

bool allFinite (const std::vector<float>& x)
{
    for (float v : x) if (! std::isfinite (v)) return false;
    return true;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double sr = 48000.0;

    // beat A: 12 s, low-mids (150-500 Hz) 12 dB louder than everything else
    const auto beatA = mixOf (bandSignal (44100.0, 12.0, 150.0, 500.0, 0.5f, 11),
                              bandSignal (44100.0, 12.0, 700.0, 9000.0, 0.125f, 12));
    // beat B: 16 s, low-mids crowded for the first 8 s, presence (2.2-5 kHz) crowded for the last 8 s
    std::vector<float> beatB;
    {
        auto first = mixOf (bandSignal (44100.0, 8.0, 150.0, 500.0, 0.5f, 21), bandSignal (44100.0, 8.0, 700.0, 9000.0, 0.125f, 22));
        auto second = mixOf (bandSignal (44100.0, 8.0, 2200.0, 5000.0, 0.5f, 23), bandSignal (44100.0, 8.0, 150.0, 2000.0, 0.14f, 24));
        beatB.insert (beatB.end(), first.begin(), first.end());
        beatB.insert (beatB.end(), second.begin(), second.end());
    }
    const auto fileA = writeWav (beatA, 44100.0, "ms_host_beatA.wav");
    const auto fileB = writeWav (beatB, 44100.0, "ms_host_beatB.wav");

    std::printf ("\n== Plugin in a headless host ==\n");
    Rig rig;
    auto& p = *rig.p;
    check (p.getTotalNumInputChannels() == 2 && p.getTotalNumOutputChannels() == 2, "stereo insert");
    check (p.getName() == "Mix Seat", "plugin name is Mix Seat");
    check (p.getLatencySamples() == 0, "plugin reports 0 samples of latency");
    {
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> d (-1.0f, 1.0f);
        bool exact = true;
        juce::MidiBuffer midi;
        for (int b = 0; b < 100 && exact; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512), ref (2, 512);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (c, i, d (rng));
            ref.makeCopyOf (buf);
            rig.host.sample = (int64_t) b * 512;
            p.processBlock (buf, midi);
            if (b % 4 == 0) p.pumpSeat();
            for (int c = 0; c < 2 && exact; ++c)
                exact = std::memcmp (buf.getReadPointer (c), ref.getReadPointer (c), sizeof (float) * 512) == 0;
        }
        check (exact, "random audio comes out bit-identical before a beat or vocal is loaded");
        check (! p.plan().valid, "no plan yet");
        p.resetVocal();                                            // the noise above must not count as a vocal
    }

    std::printf ("\n== First open: the empty screen renders ==\n");
    {
        auto ed = std::unique_ptr<juce::AudioProcessorEditor> (p.createEditor());
        pump (150);
        snapshot (*ed, "ms_1_first_open.png");
        auto* se = dynamic_cast<SeatEditor*> (ed.get());
        check (se != nullptr && se->statusText().containsIgnoreCase ("upload your beat"), "status tells you to upload a beat");
    }

    std::printf ("\n== Upload a beat ==\n");
    {
        check (p.loadBeat (fileA), "analysis starts");
        check (! p.loadBeat (fileB) || true, "(a second request while busy is allowed to be refused)");
        check (rig.waitForJob(), "analysis finishes");
        check (p.takeError().isEmpty(), "no error");
        check (p.beatProfile().valid && p.beatProfile().name == "ms_host_beatA.wav", "the beat is loaded and named");
        check (std::abs (p.beatProfile().durationSec - 12.0) < 0.1, "12 s long");
        check (! p.plan().valid, "still no plan: the vocal has not been heard yet");
    }

    std::printf ("\n== Play the vocal through the plugin ==\n");
    {
        const auto out = rig.run (0.0, 2.0, voiceSample);
        check (! p.vocalProfile().valid, "2 s of singing is not enough to trust");
        check (std::abs (rms (out) - 0.0) > 0.0, "audio is flowing");
        const auto out2 = rig.run (2.0, 8.0, voiceSample);
        rig.settle();
        check (p.vocalProfile().valid && p.vocalProfile().seconds >= 3.0, "after several seconds the vocal is learned (" + juce::String (p.vocalProfile().seconds, 1) + " s)");
        check (p.plan().valid, "a plan exists");
        check (allFinite (out) && allFinite (out2), "output is always finite");
    }

    std::printf ("\n== The EQ does what it should ==\n");
    {
        rig.settle();
        const double g300 = toneGainDb (rig, 300.0, 1.0);
        const double g3k = toneGainDb (rig, 3000.0, 1.0);
        const double g1k = toneGainDb (rig, 1000.0, 1.0);
        std::printf ("       300 Hz %+.2f dB, 1 kHz %+.2f dB, 3 kHz %+.2f dB\n", g300, g1k, g3k);
        check (g300 < -1.0, "300 Hz (the beat is crowded there) is cut by more than 1 dB");
        check (g1k > 0.5, "1 kHz (the beat leaves room) is lifted");
        check (g3k > -0.3, "3 kHz (the beat leaves room) is not cut");
        check (g300 >= -4.1, "the cut stays within the 4 dB limit");
        check (g3k <= 2.5, "the lift stays within 60% of the limit");

        // a dry vocal and the EQ'd vocal: overall level stays close
        const auto dry = [] (double t) { return voiceSample (t); };
        const auto wet = rig.run (1.0, 2.0, dry);
        std::vector<float> ref (wet.size());
        for (size_t i = 0; i < ref.size(); ++i) ref[i] = dry (1.0 + (double) i / sr);
        const double dDb = 20.0 * std::log10 (rms (wet) / rms (ref));
        check (std::abs (dDb) < 2.5, "the vocal's overall level stays close to where it was (" + juce::String (dDb, 2) + " dB)");
    }

    std::printf ("\n== Screens with a beat and a vocal ==\n");
    {
        auto ed = std::unique_ptr<juce::AudioProcessorEditor> (p.createEditor());
        pump (250);
        snapshot (*ed, "ms_2_active_static.png");
        auto* se = dynamic_cast<SeatEditor*> (ed.get());
        check (se != nullptr && se->statusText().startsWith ("Active"), "status says active");
        check (se != nullptr && se->statusText().contains ("whole-song"), "status says it is using the whole-song average");
        const auto moves = ms::explain (p.plan().curveAt (0.0, false));
        check (! moves.empty() && moves[0].db < 0.0f, "the biggest move is a cut, and it is explained");
    }

    std::printf ("\n== Bypass and trim ==\n");
    {
        p.apvts.getParameter ("bypass")->setValueNotifyingHost (1.0f);
        rig.settle (40);
        const auto sig = [] (double t) { return voiceSample (t); };
        auto wet = rig.run (0.0, 1.0, sig);
        std::vector<float> ref (wet.size());
        for (size_t i = 0; i < ref.size(); ++i) ref[i] = sig ((double) i / sr);
        // the filters ramp to flat, so compare after they have settled
        bool exact = true;
        for (size_t i = wet.size() / 2; i < wet.size(); ++i) if (wet[i] != ref[i]) { exact = false; break; }
        check (exact, "bypassed: the vocal is bit-identical to what went in");
        check (p.appliedGains()[3] == 0.0f, "bypassed: every EQ gain is 0");

        p.apvts.getParameter ("trim")->setValueNotifyingHost (p.apvts.getParameter ("trim")->convertTo0to1 (6.0f));
        rig.run (0.0, 0.5, sig);                                   // let the trim ramp
        wet = rig.run (0.0, 1.0, sig);
        const double ratio = rms (wet) / rms (ref);
        check (std::abs (ratio - 1.995) < 0.03, "trim +6 dB doubles the level (x" + juce::String (ratio, 3) + ")");
        p.apvts.getParameter ("trim")->setValueNotifyingHost (p.apvts.getParameter ("trim")->convertTo0to1 (0.0f));
        rig.run (0.0, 0.5, sig);
        p.apvts.getParameter ("bypass")->setValueNotifyingHost (0.0f);
        rig.settle (40);
        check (std::abs (toneGainDb (rig, 300.0, 1.0)) > 1.0, "un-bypassed: the EQ is back");
    }

    std::printf ("\n== Freezing the vocal profile ==\n");
    {
        const double before = p.vocalProfile().seconds;
        const auto before300 = p.vocalProfile().average[8];
        p.apvts.getParameter ("freeze")->setValueNotifyingHost (1.0f);
        rig.run (0.0, 4.0, [] (double t) { return (float) (0.3 * std::sin (2.0 * kPi * 5000.0 * t)); });
        rig.settle();
        check (std::abs (p.vocalProfile().seconds - before) < 0.01 && p.vocalProfile().average[8] == before300, "frozen: other sounds do not change the learned vocal");
        p.apvts.getParameter ("freeze")->setValueNotifyingHost (0.0f);
    }

    std::printf ("\n== Following the beat through the song ==\n");
    {
        Rig r2;
        check (r2.p->loadBeat (fileB), "analysis of beat B starts");
        check (r2.waitForJob(), "...and finishes");
        r2.run (0.0, 10.0, voiceSample);
        r2.settle();
        r2.p->apvts.getParameter ("follow")->setValueNotifyingHost (1.0f);
        r2.settle (20);
        check (r2.p->plan().valid && r2.p->plan().hasWindows, "follow mode builds a plan per window");
        const double early300 = toneGainDb (r2, 300.0, 2.0);
        const double early3k = toneGainDb (r2, 3000.0, 2.0);
        const double late300 = toneGainDb (r2, 300.0, 12.0);
        const double late3k = toneGainDb (r2, 3000.0, 12.0);
        std::printf ("       early (2 s): 300 Hz %+.2f, 3 kHz %+.2f   late (12 s): 300 Hz %+.2f, 3 kHz %+.2f\n", early300, early3k, late300, late3k);
        check (early300 < -1.0 && early300 < late300 - 1.0, "early part: the vocal is cut at 300 Hz (the beat is crowded there), less so later");
        check (late3k < -1.0 && late3k < early3k - 1.0, "late part: the vocal is cut at 3 kHz instead");

        r2.p->apvts.getParameter ("beatOffset")->setValueNotifyingHost (r2.p->apvts.getParameter ("beatOffset")->convertTo0to1 (10.0f));
        r2.settle (20);
        const double shifted300 = toneGainDb (r2, 300.0, 12.0);       // 12 s in the project = 2 s into the beat
        check (shifted300 < -1.0, "'beat starts at 10 s' moves the beat later: project 12 s is now 2 s into the beat");
        {
            auto ed = std::unique_ptr<juce::AudioProcessorEditor> (r2.p->createEditor());
            pump (250);
            snapshot (*ed, "ms_3_following.png");
            auto* se = dynamic_cast<SeatEditor*> (ed.get());
            check (se != nullptr && se->statusText().contains ("following the beat"), "status says it is following the beat");
        }
    }

    std::printf ("\n== Saving and reopening the project ==\n");
    {
        juce::MemoryBlock state;
        p.getStateInformation (state);
        check (state.getSize() > 500, "state is saved (" + juce::String ((int) state.getSize()) + " bytes)");

        Rig r3;
        r3.p->setStateInformation (state.getData(), (int) state.getSize());
        r3.settle (10);
        check (r3.p->beatProfile().valid && r3.p->beatProfile().name == "ms_host_beatA.wav", "the beat comes back without the file");
        check (r3.p->vocalProfile().valid && std::abs (r3.p->vocalProfile().seconds - p.vocalProfile().seconds) < 0.1, "the learned vocal comes back");
        check (r3.p->plan().valid, "the plan is rebuilt");
        float worst = 0.0f;
        for (size_t i = 0; i < p.plan().staticGains.size(); ++i)
            worst = std::max (worst, std::abs (p.plan().staticGains[i] - r3.p->plan().staticGains[i]));
        check (worst < 0.05f, "the same EQ comes back (largest gain difference " + juce::String (worst, 3) + " dB)");
        check (std::abs (p.apvts.getRawParameterValue ("maxDb")->load() - r3.p->apvts.getRawParameterValue ("maxDb")->load()) < 1.0e-4f, "knob settings come back");

        const char junk[] = "not a state";
        r3.p->setStateInformation (junk, (int) sizeof (junk));
        r3.settle (4);
        check (r3.p->beatProfile().valid, "garbage state is ignored, nothing is lost");
    }

    std::printf ("\n== Bad files ==\n");
    {
        auto bad = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms_host_bad.wav");
        bad.replaceWithText ("this is not audio");
        Rig r4;
        check (r4.p->loadBeat (bad), "a bad file is accepted for analysis");
        check (r4.waitForJob(), "...and finishes");
        const auto err = r4.p->takeError();
        check (err.isNotEmpty(), "...with a message: \"" + err.substring (0, 60) + "\"");
        check (! r4.p->beatProfile().valid, "no beat is set");
        auto ed = std::unique_ptr<juce::AudioProcessorEditor> (r4.p->createEditor());
        pump (100);
        bad.deleteFile();
        const auto out = r4.run (0.0, 0.3, voiceSample);
        check (allFinite (out), "audio still flows");
    }

    std::printf ("\n== Reset and remove ==\n");
    {
        p.resetVocal();
        rig.settle (30);
        check (! p.vocalProfile().valid && ! p.plan().valid, "forgetting the vocal removes the plan");
        const auto sig = [] (double t) { return voiceSample (t); };
        rig.run (0.0, 1.0, sig);
        auto wet = rig.run (0.0, 1.0, sig);
        std::vector<float> ref (wet.size());
        for (size_t i = 0; i < ref.size(); ++i) ref[i] = sig ((double) i / sr);
        bool exact = true;
        for (size_t i = wet.size() / 2; i < wet.size(); ++i) if (wet[i] != ref[i]) { exact = false; break; }
        check (exact, "with no plan the audio is bit-identical again");
        p.clearBeat();
        rig.settle (5);
        check (! p.beatProfile().valid, "removing the beat clears it");
    }

    std::printf ("\n== Mono track and another sample rate ==\n");
    {
        Rig mono (1, 44100.0);
        check (mono.p->getTotalNumInputChannels() == 1, "mono track is accepted");
        check (mono.p->loadBeat (fileA), "beat loads on a mono track");
        check (mono.waitForJob(), "...and finishes");
        const auto out = mono.run (0.0, 8.0, voiceSample);
        mono.settle();
        check (mono.p->plan().valid && allFinite (out), "mono: a plan exists and the output is finite");

        mono.p->setPlayConfigDetails (1, 1, 96000.0, 512);
        mono.p->prepareToPlay (96000.0, 512);
        mono.host.sr = 96000.0;
        mono.sr = 96000.0;
        mono.settle (10);
        check (mono.p->vocalProfile().valid && mono.p->plan().valid, "a sample-rate change keeps the vocal and rebuilds the plan");
        bool fin = true;
        for (float g : mono.p->appliedGains()) if (! std::isfinite (g)) fin = false;
        check (fin, "...with finite gains at 96 kHz");
    }

    fileA.deleteFile();
    fileB.deleteFile();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
