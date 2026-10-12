// Vocal Stack: DSP checks and a headless host run.
#include "../Source/Stack/StackProcessor.h"
#include "../Source/Stack/StackEditor.h"
#include <cstdio>
#include <cmath>
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

constexpr double kSr = 48000.0;

// a harmonic, voice-like tone with vibrato and slow level changes
std::vector<float> voice (double seconds, float level = 0.3f)
{
    std::vector<float> x ((size_t) (seconds * kSr));
    double ph = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / kSr;
        ph += 2 * vc::kPi * (200.0 * (1.0 + 0.008 * std::sin (2 * vc::kPi * 5.0 * t))) / kSr;
        double s = 0;
        for (int k = 1; k <= 40; ++k) s += std::sin (k * ph) / std::pow ((double) k, 1.3);
        x[i] = level * (float) (s * (0.7 + 0.3 * std::sin (2 * vc::kPi * 1.3 * t)));
    }
    return x;
}

struct Out { std::vector<float> l, r; };
Out run (const std::vector<float>& in, const vs::StackParams& p, int block = 480)
{
    vs::StackEngine e; e.prepare (kSr, block);
    Out o; juce::AudioBuffer<float> b (2, block);
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        for (int i = 0; i < n; ++i) { b.setSample (0, i, in[pos + (size_t) i]); b.setSample (1, i, in[pos + (size_t) i]); }
        juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, n);
        e.process (v, p);
        for (int i = 0; i < n; ++i) { o.l.push_back (b.getSample (0, i)); o.r.push_back (b.getSample (1, i)); }
    }
    return o;
}
double rms (const std::vector<float>& x, size_t from)
{
    double s = 0; for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (x.size() - from));
}
double corr (const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = from; i < a.size(); ++i) { ab += (double) a[i] * b[i]; aa += (double) a[i] * a[i]; bb += (double) b[i] * b[i]; }
    return ab / std::sqrt (aa * bb + 1e-30);
}
// energy above 3 kHz relative to total
double highShare (const std::vector<float>& x)
{
    vc::Biquad hp; hp.highPass (kSr, 3000.0, 0.707);
    double h = 0, t = 0;
    for (size_t i = 4800; i < x.size(); ++i) { const double s = x[i]; const double y = hp.process (x[i]); h += y * y; t += s * s; }
    return h / (t + 1e-30);
}
bool finite (const Out& o) { for (auto v : o.l) if (! std::isfinite (v)) return false; for (auto v : o.r) if (! std::isfinite (v)) return false; return true; }
float peakOf (const Out& o, size_t from = 0) { float m = 0; for (size_t i = from; i < o.l.size(); ++i) m = std::max (m, std::max (std::abs (o.l[i]), std::abs (o.r[i]))); return m; }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto in = voice (4.0);

    std::printf ("\n== Vocal Stack DSP ==\n");
    {
        vs::StackParams off; off.stack = 0; off.room = 0;
        auto a = run (in, off);
        check (std::abs (corr (a.l, a.r, 4800) - 1.0) < 1e-6, "stack off: left and right are identical (a plain centred lead)");

        vs::StackParams on; on.stack = 1.0f; on.voices = 6; on.spread = 1.0f; on.room = 0;
        auto b = run (in, on);
        const double c = corr (b.l, b.r, 4800);
        std::printf ("       left/right correlation with a full stack: %.3f\n", c);
        check (c < 0.93, "full stack widens the vocal (left and right differ)");
        check (rms (b.l, 4800) > rms (a.l, 4800) * 1.02, "the stack adds level and weight");

        vs::StackParams one = on; one.voices = 1; one.stack = 1.0f;
        auto o1 = run (in, one);
        vs::StackParams six = on;
        auto o6 = run (in, six);
        check (corr (o1.l, o1.r, 4800) < 0.999 && corr (o6.l, o6.r, 4800) < corr (o1.l, o1.r, 4800) + 0.2, "voice count changes the stack");

        vs::StackParams loose = on; loose.tight = 1.0f; vs::StackParams tight = on; tight.tight = 0.0f;
        auto lo = run (in, loose), ti = run (in, tight);
        check (std::abs (corr (lo.l, lo.r, 4800) - corr (ti.l, ti.r, 4800)) > 0.005, "looseness changes the timing of the voices");

        vs::StackParams nb = on; nb.bite = 0; nb.hard = 0; nb.grit = 0;
        vs::StackParams bb = on; bb.bite = 1.0f; bb.hard = 1.0f; bb.grit = 1.0f;
        auto n0 = run (in, nb), n1 = run (in, bb);
        std::printf ("       energy above 3 kHz: %.4f without bite/hard/grit, %.4f with\n", highShare (n0.l), highShare (n1.l));
        check (highShare (n1.l) > highShare (n0.l) * 1.3, "bite, hard and grit add high harmonics (texture)");

        // dynamics: 2 s quiet then 2 s loud (12 dB apart); "upfront" should shrink that gap
        auto steps = in;
        for (size_t i = 0; i < steps.size() / 2; ++i) steps[i] *= 0.25f;
        auto gap = [&] (float up)
        {
            vs::StackParams q = on; q.upfront = up; q.hard = 0; q.bite = 0;
            auto o = run (steps, q);
            auto seg = [&] (size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) o.l[i] * o.l[i]; return std::sqrt (s / (double) (b - a)); };
            return 20.0 * std::log10 (seg (3 * 48000 - 24000, 4 * 48000) / seg (48000 - 24000, 2 * 48000 - 24000 + 24000));
        };
        const double g0 = gap (0.0f), g1 = gap (1.0f);
        std::printf ("       loud-to-quiet gap: upfront 0 -> %.1f dB, upfront 100 -> %.1f dB\n", g0, g1);
        check (g1 < g0 - 1.5, "upfront squeezes the dynamics (firmer, more in your face)");

        vs::StackParams ex; ex.voices = 6; ex.stack = 1; ex.spread = 1; ex.detuneCents = 30; ex.tight = 1; ex.brightDb = 8; ex.grit = 1; ex.bite = 1; ex.hard = 1;
        ex.presenceDb = 10; ex.mudDb = 10; ex.upfront = 1; ex.room = 1; ex.inGainDb = 12; ex.outGainDb = 12;
        auto e = run (in, ex);
        check (finite (e), "extreme settings stay finite");
        check (peakOf (e) <= 1.0001f, "safety ceiling holds at extreme settings (peak " + juce::String (peakOf (e), 3) + ")");

        // zero latency: an impulse comes out at the start
        std::vector<float> imp (2000, 0.0f); imp[100] = 0.5f;
        vs::StackParams z; z.stack = 0; z.room = 0;
        auto zi = run (imp, z);
        size_t pk = 0; for (size_t i = 0; i < zi.l.size(); ++i) if (std::abs (zi.l[i]) > std::abs (zi.l[pk])) pk = i;
        check (pk >= 100 && pk <= 104, "no latency: the impulse comes out within 4 samples");

        // block size independence
        auto big = run (in, on, 480), odd = run (in, on, 37);
        double worst = 0; for (size_t i = 0; i < big.l.size(); ++i) worst = std::max (worst, (double) std::abs (big.l[i] - odd.l[i]));
        std::printf ("       480-sample vs 37-sample blocks: worst difference %.2e\n", worst);
        check (worst < 1e-3, "result does not depend on the host's block size");

        std::vector<float> sil (48000, 0.0f);
        auto sr = run (sil, on);
        check (peakOf (sr) < 1e-4, "silence stays silent");
    }

    std::printf ("\n== Vocal Stack in a headless host ==\n");
    auto proc = std::make_unique<VocalStackProcessor>();
    check (juce::String (proc->getName()) == "Vocal Stack", "plugin name is Vocal Stack");
    using CS = juce::AudioChannelSet;
    auto layout = [&] (CS i, CS o) { juce::AudioProcessor::BusesLayout l; l.inputBuses.add (i); l.outputBuses.add (o); return proc->isBusesLayoutSupported (l); };
    check (layout (CS::stereo(), CS::stereo()) && layout (CS::mono(), CS::mono()) && layout (CS::mono(), CS::stereo()) && ! layout (CS::stereo(), CS::mono()), "mono and stereo layouts");
    proc->setPlayConfigDetails (2, 2, 48000.0, 480);
    proc->prepareToPlay (48000.0, 480);
    check (proc->getLatencySamples() == 0, "reports no latency");

    auto hostRun = [&] (VocalStackProcessor& p, int cin, int cout, double secs)
    {
        juce::MidiBuffer m; juce::AudioBuffer<float> b (std::max (cin, cout), 480); float pk = 0; bool ok = true;
        const auto src = voice (secs);
        for (size_t pos = 0; pos + 480 <= src.size(); pos += 480)
        {
            b.clear();
            for (int c = 0; c < cin; ++c) for (int i = 0; i < 480; ++i) b.setSample (c, i, src[pos + (size_t) i]);
            p.processBlock (b, m);
            for (int c = 0; c < cout; ++c) for (int i = 0; i < 480; ++i) { const float v = b.getSample (c, i); if (! std::isfinite (v)) ok = false; pk = std::max (pk, std::abs (v)); }
        }
        return std::make_pair (ok, pk);
    };
    for (int i = 0; i < (int) vs::presets().size(); ++i)
    {
        proc->applyPreset (i);
        auto r = hostRun (*proc, 2, 2, 2.0);
        check (r.first && r.second > 0.05f && r.second <= 1.0001f, juce::String ("preset ok: ") + vs::presets()[(size_t) i].name + " peak " + juce::String (r.second, 3));
    }
    proc->applyPreset (1);
    check (std::abs (proc->apvts.getRawParameterValue ("voices")->load() - 6.0f) < 0.01f, "Wall of Voices sets 6 voices");
    {
        juce::MemoryBlock mb; proc->getStateInformation (mb);
        auto other = std::make_unique<VocalStackProcessor>();
        other->setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (other->apvts.getRawParameterValue ("stack")->load() - 85.0f) < 0.6f, "saved project restores the stack level");
    }
    {
        auto m = std::make_unique<VocalStackProcessor>();
        juce::AudioProcessor::BusesLayout l; l.inputBuses.add (CS::mono()); l.outputBuses.add (CS::mono());
        check (m->setBusesLayout (l), "mono layout accepted");
        m->prepareToPlay (44100.0, 480);
        auto r = hostRun (*m, 1, 1, 2.0);
        check (r.first && r.second > 0.05f && r.second <= 1.0001f, "mono -> mono works");
        auto s = std::make_unique<VocalStackProcessor>();
        juce::AudioProcessor::BusesLayout l2; l2.inputBuses.add (CS::mono()); l2.outputBuses.add (CS::stereo());
        check (s->setBusesLayout (l2), "mono->stereo layout accepted");
        s->prepareToPlay (44100.0, 480);
        auto r2 = hostRun (*s, 1, 2, 2.0);
        check (r2.first && r2.second > 0.05f && r2.second <= 1.0001f, "mono -> stereo works");
    }
    {
        juce::MidiBuffer midi; bool ok = true;
        for (int bs : { 1, 7, 64, 1000, 4096, 17 })
        {
            juce::AudioBuffer<float> buf (2, bs);
            for (int rep = 0; rep < 4; ++rep)
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
        check (editor != nullptr && editor->getWidth() == 1100, "editor opens");
        hostRun (*proc, 2, 2, 1.0);
        pump (250);
        snapshot (*editor, "stack_1.png");
    }
    proc->releaseResources();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL STACK TESTS PASSED" : "STACK TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
