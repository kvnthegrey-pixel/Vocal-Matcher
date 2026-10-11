// Phase 3 unit tests: post-processing rules, block editing, MIDI, saved state,
// and the pYIN audio analysis on synthetic singing. Exit code 0 only if all pass.
#include "../Source/ReferenceJob.h"
#include "../Source/MidiReference.h"
#include "../Source/Matcher.h"
#include "../Source/SungNotes.h"
#include "../Source/ToneSequence.h"
#include "../Source/EqMatch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <random>

using namespace va;

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("  %-4s %s\n", ok ? "ok" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

// ---- frame track helpers -------------------------------------------------
constexpr double hop = 0.010;

FrameTrack makeTrack (std::initializer_list<std::pair<double, double>> segments)
{
    // each segment: {midi (<=0 unvoiced), duration in seconds}
    FrameTrack t;
    t.hopSec = hop;
    t.firstCentreSec = 0.005;
    for (auto& s : segments)
        for (int i = 0; i < (int) std::lround (s.second / hop); ++i)
        {
            t.midi.push_back ((float) s.first);
            t.clarity.push_back (s.first > 0 ? 0.95f : 0.0f);
        }
    return t;
}

// ---- synthetic singing -----------------------------------------------------
struct Note { double midi; double seconds; };   // midi <= 0: rest

std::vector<float> sing (const std::vector<Note>& notes, double sr, double vibratoCents = 20.0, double noiseAmp = 0.01,
                         double detuneCents = 0.0, double lag = 0.0, unsigned seed = 1)
{
    double total = 0.0;
    for (auto& n : notes) total += n.seconds;
    std::vector<float> out ((size_t) (total * sr) + 1, 0.0f);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);

    double t0 = 0.0, phase = 0.0;
    for (auto& n : notes)
    {
        const size_t a = (size_t) (t0 * sr), b = (size_t) ((t0 + n.seconds) * sr);
        if (n.midi > 0)
            for (size_t i = a; i < b && i < out.size(); ++i)
            {
                const double t = (double) i / sr, local = t - t0;
                const double vib = vibratoCents * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t + lag);
                const double hz = 440.0 * std::pow (2.0, (n.midi - 69.0 + (vib + detuneCents) / 100.0) / 12.0);
                phase += 2.0 * juce::MathConstants<double>::pi * hz / sr;
                double env = std::min (1.0, local / 0.02) * std::min (1.0, (n.seconds - local) / 0.03);
                double s = 0.0;
                for (int h = 1; h <= 6; ++h) s += std::sin (h * phase) / h;
                out[i] = (float) (0.35 * env * s);
            }
        t0 += n.seconds;
    }
    for (auto& s : out) s += (float) noiseAmp * d (rng);
    return out;
}

juce::File writeWav (const std::vector<float>& x, double sr, const juce::String& name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), sr, 1, 16, {}, 0));
    stream.release();
    juce::AudioBuffer<float> buf (1, (int) x.size());
    std::copy (x.begin(), x.end(), buf.getWritePointer (0));
    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    return f;
}

std::vector<float> reverb (const std::vector<float>& x, double sr, double seconds, double wet)
{
    std::mt19937 rng (99);
    std::normal_distribution<float> g (0.0f, 1.0f);
    const size_t len = (size_t) (seconds * sr);
    std::vector<float> ir (len);
    for (size_t i = 0; i < len; ++i) ir[i] = g (rng) * std::exp (-6.9f * (float) i / (float) len);
    std::vector<float> y (x.size(), 0.0f);
    // Sparse convolution (every 3rd tap) is enough to smear the signal.
    for (size_t i = 0; i < x.size(); i += 2)
    {
        const float v = x[i];
        if (std::abs (v) < 1.0e-4f) continue;
        for (size_t k = 0; k < len && i + k < y.size(); k += 3)
            y[i + k] += v * ir[k] * 0.02f * (float) wet;
    }
    for (size_t i = 0; i < y.size(); ++i) y[i] += (float) (1.0 - wet) * x[i];
    return y;
}

AudioAnalysisResult analyse (const std::vector<float>& x, double sr, const juce::String& name)
{
    auto f = writeWav (x, sr, name);
    auto r = analyseAudioFile (f, SegmenterRules(), PyinSettings(), [] (float) { return true; });
    f.deleteFile();
    return r;
}
}

int main()
{
    SegmenterRules rules;

    // ===================================================================
    std::printf ("\n== Post-processing rules (strict document, section 2) ==\n");
    {
        auto blocks = segment (makeTrack ({ { 60, 0.5 } }), rules);
        check (blocks.size() == 1 && blocks[0].midiMilli == 60000, "one clean note stays one note");
        check (std::abs (blocks[0].startSec() - 0.0) < 0.001 && std::abs (blocks[0].endSec() - 0.5) < 0.001, "note edges are the frame edges");
    }
    {   // minimum duration 80 ms
        auto b60  = segment (makeTrack ({ { 60, 0.06 }, { 0, 0.2 }, { 64, 0.5 } }), rules);
        check (b60.size() == 1 && b60[0].midiMilli == 64000, "60 ms note is dropped (minimum 80 ms)");
        auto b100 = segment (makeTrack ({ { 60, 0.10 }, { 0, 0.2 }, { 64, 0.5 } }), rules);
        check (b100.size() == 2, "100 ms note is kept");
    }
    {   // deviation: 40 cents
        auto drift = segment (makeTrack ({ { 60.0, 0.2 }, { 60.3, 0.1 }, { 60.0, 0.2 }, { 59.7, 0.1 }, { 60.0, 0.2 } }), rules);
        check (drift.size() == 1, "wander of 30 cents stays one note");
        auto jump = segment (makeTrack ({ { 60.0, 0.3 }, { 60.6, 0.3 } }), rules);   // 60 cents up, no gap
        check (jump.size() == 2, "a 60 cent move with no gap forces a split");
        check (jump.size() == 2 && jump[0].midiMilli == 60000 && jump[1].midiMilli == 61000, "...and the two parts get their own pitches (60, 61)");
        auto blip = segment (makeTrack ({ { 60.0, 0.3 }, { 62.0, 0.02 }, { 60.0, 0.3 } }), rules);
        check (blip.size() == 1, "a 20 ms glitch does not split the note");
    }
    {   // unvoiced gaps: more than 60 ms ends the note
        auto g70 = segment (makeTrack ({ { 60, 0.3 }, { 0, 0.07 }, { 60, 0.3 } }), rules);
        check (g70.size() == 2, "70 ms gap ends the note");
        auto g50 = segment (makeTrack ({ { 60, 0.3 }, { 0, 0.05 }, { 60, 0.3 } }), rules);
        check (g50.size() == 1, "50 ms gap does not end the note");
    }
    {   // merge rule, tested on notes directly
        auto mk = [] (double s0, double e0, double m0, double s1, double e1, double m1)
        {
            return std::vector<RawNote> { { (int64_t) (s0 * 1e6), (int64_t) (e0 * 1e6), m0 }, { (int64_t) (s1 * 1e6), (int64_t) (e1 * 1e6), m1 } };
        };
        check (mergeAdjacent (mk (0, 0.5, 60.0, 0.53, 1.0, 60.2), rules).size() == 1, "30 ms gap, 20 cents apart: merged");
        check (mergeAdjacent (mk (0, 0.5, 60.0, 0.53, 1.0, 60.5), rules).size() == 2, "30 ms gap, 50 cents apart: not merged");
        check (mergeAdjacent (mk (0, 0.5, 60.0, 0.545, 1.0, 60.1), rules).size() == 2, "45 ms gap, 10 cents apart: not merged");
        check (mergeAdjacent (mk (0, 0.5, 60.0, 0.53, 1.0, 60.3), rules).size() == 1, "exactly 30 cents apart: merged");
    }
    {   // snapping
        rules.snapToSemitone = false;
        auto raw = segment (makeTrack ({ { 60.2, 0.3 } }), rules);
        check (raw.size() == 1 && raw[0].midiMilli == 60200, "snapping off keeps the measured pitch (60.2)");
        rules.snapToSemitone = true;
        auto snapped = segment (makeTrack ({ { 60.2, 0.3 } }), rules);
        check (snapped.size() == 1 && snapped[0].midiMilli == 60000, "snapping on rounds to the semitone");
    }

    // ===================================================================
    std::printf ("\n== Block editing: select, split, merge, delete, pitch-shift, lock ==\n");
    {
        std::vector<NoteBlock> v;
        for (int i = 0; i < 4; ++i) { NoteBlock b; b.startUs = i * 1000000; b.endUs = i * 1000000 + 800000; b.midiMilli = (60 + i) * 1000; v.push_back (b); }
        BlockList l;
        l.replaceAll (v);
        check (l.blocks().size() == 4 && l.blocks()[0].id == 1 && l.blocks()[3].id == 4, "blocks get ids 1..4");
        check (l.indexAt (1500000) == 1 && l.indexAt (900000) == -1, "indexAt finds the block under a time");

        check (l.split (1, 400000), "split block 1 at 0.4 s");
        check (l.blocks().size() == 5 && l.blocks()[0].endUs == 400000 && l.blocks()[1].startUs == 400000 && l.blocks()[1].endUs == 800000, "...gives two touching parts");
        check (! l.split (2, 790000), "split leaving a part under 20 ms is refused");
        check (! l.split (99, 100), "split of an unknown block is refused");

        check (l.merge ({ 1 }) && l.blocks().size() == 4 && l.blocks()[0].endUs == 800000, "merge of one block joins it to the next");
        check (l.merge ({ 2, 3 }) && l.blocks().size() == 3, "merge of two selected blocks joins them");
        check (l.blocks()[1].startUs == 1000000 && l.blocks()[1].endUs == 2800000, "...and covers both, including the gap");

        check (l.shift ({ 1 }, 2) && l.find (1)->midiMilli == 62000, "pitch-shift +2 semitones");
        check (l.shift ({ 1 }, -12) && l.find (1)->midiMilli == 50000, "pitch-shift down an octave");
        check (! l.shift ({ 1 }, 0), "shift by 0 changes nothing");

        check (l.remove ({ 1 }) && l.blocks().size() == 2, "delete");

        l.setLocked (true);
        const auto count = l.blocks().size();
        check (! l.split (2, 2000000) && ! l.merge ({ 2 }) && ! l.remove ({ 2 }) && ! l.shift ({ 2 }, 1) && l.blocks().size() == count,
              "a locked reference refuses every edit");
        l.setLocked (false);
        check (l.remove ({ 2 }), "unlocking allows edits again");
    }
    {
        ReferenceModel m;
        std::vector<NoteBlock> v;
        for (int i = 0; i < 3; ++i) { NoteBlock b; b.startUs = i * 1000000; b.endUs = i * 1000000 + 500000; b.midiMilli = 60000; v.push_back (b); }
        m.setReference (v, AnalysisInfo());
        m.remove ({ 2 });
        m.shift ({ 1 }, 5);
        check (m.blocksCopy().blocks().size() == 2 && m.canUndo(), "model: two edits done");
        m.undo(); m.undo();
        auto b = m.blocksCopy().blocks();
        check (b.size() == 3 && b[0].midiMilli == 60000, "model: undo twice restores the original");
        check (! m.undo(), "model: nothing left to undo");
    }

    // ===================================================================
    std::printf ("\n== Saved project state: exact restore, no re-analysis ==\n");
    {
        std::vector<NoteBlock> v;
        std::mt19937 rng (5);
        for (int i = 0; i < 200; ++i)
        {
            NoteBlock b;
            b.startUs = (int64_t) i * 1234567 + (int64_t) (rng() % 1000);
            b.endUs = b.startUs + 80000 + (int64_t) (rng() % 900000);
            b.midiMilli = 48000 + (int) (rng() % 36000);
            v.push_back (b);
        }
        ReferenceModel a;
        AnalysisInfo info;
        info.kind = SourceKind::Audio;
        info.sourceName = "lead vocal.flac";
        info.quality = Quality::Amber;
        info.meanClarity = 0.8123456789;
        info.voicedFraction = 0.61;
        info.jumpRate = 0.012;
        info.rules.maxDeviationCents = 35.0;
        info.rules.snapToSemitone = false;
        info.pyin.hopSize = 256;
        a.setReference (v, info);
        a.shift ({ 3 }, 2);
        a.setLocked (true);

        // Through real XML, as Cubase would store it.
        auto xml = a.toValueTree().createXml();
        ReferenceModel b2;
        b2.fromValueTree (juce::ValueTree::fromXml (*xml));

        auto x = a.blocksCopy(), y = b2.blocksCopy();
        bool same = x.blocks().size() == y.blocks().size() && x.isLocked() == y.isLocked();
        for (size_t i = 0; same && i < x.blocks().size(); ++i)
        {
            auto &p = x.blocks()[i], &q = y.blocks()[i];
            same = p.id == q.id && p.startUs == q.startUs && p.endUs == q.endUs && p.midiMilli == q.midiMilli;
        }
        check (same, "200 blocks, ids and lock state survive a save and reload exactly");
        auto i2 = b2.infoCopy();
        check (i2.kind == SourceKind::Audio && i2.sourceName == "lead vocal.flac" && i2.quality == Quality::Amber
                   && std::abs (i2.meanClarity - 0.8123456789) < 1e-9 && i2.rules.maxDeviationCents == 35.0
                   && ! i2.rules.snapToSemitone && i2.pyin.hopSize == 256,
               "analysis parameters, source name and quality survive too");
    }

    // ===================================================================
    std::printf ("\n== MIDI import and export ==\n");
    {
        std::vector<NoteBlock> v;
        const int64_t starts[] = { 0, 600000, 1500000, 2750000 };
        const int pitches[] = { 60, 62, 64, 59 };
        for (int i = 0; i < 4; ++i) { NoteBlock b; b.startUs = starts[i]; b.endUs = starts[i] + 500000 + i * 37000; b.midiMilli = pitches[i] * 1000; v.push_back (b); }

        auto file = MidiReference::exportBlocks (v);
        juce::MemoryOutputStream mos;
        file.writeTo (mos);
        juce::MemoryInputStream mis (mos.getData(), mos.getDataSize(), false);
        juce::MidiFile back;
        check (back.readFrom (mis), "exported file reads back as a valid MIDI file");

        auto tracks = MidiReference::listTracks (back);
        check (tracks.size() == 1 && tracks[0].noteCount == 4 && tracks[0].lowestNote == 59 && tracks[0].highestNote == 64, "track list: one track, 4 notes, range 59 to 64");

        auto got = MidiReference::importTrack (back, tracks[0].index, rules);
        bool ok = got.size() == 4;
        int64_t worst = 0;
        for (size_t i = 0; ok && i < 4; ++i)
        {
            worst = std::max<int64_t> (worst, std::max<int64_t> (std::llabs (got[i].startUs - v[i].startUs), std::llabs (got[i].endUs - v[i].endUs)));
            ok = got[i].midiMilli == v[i].midiMilli;
        }
        check (ok && worst <= 1100, "round trip keeps pitches exactly and times within one tick (" + juce::String ((int) worst) + " us)");
    }
    {   // overlap, short note, drum channel, two tracks
        juce::MidiMessageSequence lead, other;
        lead.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 90), 0);
        lead.addEvent (juce::MidiMessage::noteOff (1, 60), 960);       // 1.0 s at 120 bpm / 480 ppq
        lead.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 90), 480);   // starts at 0.5 s, overlaps the first
        lead.addEvent (juce::MidiMessage::noteOff (1, 62), 1440);
        lead.addEvent (juce::MidiMessage::noteOn (1, 65, (juce::uint8) 90), 2000);  // 40 ms long: dropped
        lead.addEvent (juce::MidiMessage::noteOff (1, 65), 2038);
        lead.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 90), 100);  // drum channel
        lead.addEvent (juce::MidiMessage::noteOff (10, 36), 200);
        other.addEvent (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 90), 0);
        other.addEvent (juce::MidiMessage::noteOff (1, 40), 100);
        lead.updateMatchedPairs(); other.updateMatchedPairs();
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (480);
        mf.addTrack (lead); mf.addTrack (other);
        auto tracks = MidiReference::listTracks (mf);
        check (tracks.size() == 2 && tracks[0].noteCount == 3, "drum channel notes are not counted; two note tracks are offered");
        auto got = MidiReference::importTrack (mf, 0, rules);
        check (got.size() == 2 && got[0].endUs == got[1].startUs && std::abs ((double) got[0].endUs - 500000.0) < 1.0, "overlap: the later note cuts the earlier one at 0.5 s");
        check (got.size() == 2 && got[1].midiMilli == 62000, "the 40 ms note is dropped");
    }

    // ===================================================================
    std::printf ("\n== pYIN audio analysis on synthetic singing (experimental path) ==\n");
    for (double sr : { 44100.0, 48000.0 })
    {
        const std::vector<Note> melody = { { 60, 0.50 }, { 64, 0.40 }, { 0, 0.15 }, { 67, 0.60 }, { 55, 0.45 }, { 0, 0.20 }, { 72, 0.50 }, { 48, 0.50 } };
        auto r = analyse (sing (melody, sr), sr, "va_melody.wav");
        char buf[160];
        std::snprintf (buf, sizeof buf, "%.0f Hz: analysis ran (quality %s, clarity %.2f)", sr, qualityName (r.info.quality), r.info.meanClarity);
        check (r.ok, buf);
        const int expectPitch[] = { 60, 64, 67, 55, 72, 48 };
        const double expectStart[] = { 0.0, 0.50, 1.05, 1.65, 2.30, 2.80 };
        const double expectEnd[]   = { 0.50, 0.90, 1.65, 2.10, 2.80, 3.30 };
        check (r.blocks.size() == 6, "6 notes found (got " + juce::String ((int) r.blocks.size()) + ")");
        bool pitchOk = r.blocks.size() == 6;
        double worstEdge = 0.0;
        for (size_t i = 0; pitchOk && i < 6; ++i)
        {
            pitchOk = r.blocks[i].midiMilli == expectPitch[i] * 1000;
            worstEdge = std::max ({ worstEdge, std::abs (r.blocks[i].startSec() - expectStart[i]), std::abs (r.blocks[i].endSec() - expectEnd[i]) });
        }
        check (pitchOk, "every note has the right semitone, with 20 cents of vibrato");
        check (r.blocks.size() == 6 && worstEdge < 0.060, "note edges within 60 ms (worst " + juce::String (worstEdge * 1000.0, 0) + " ms)");
        check (r.info.quality == Quality::Green, "clean monophonic singing is rated Green");
    }
    {
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> d (-0.5f, 0.5f);
        std::vector<float> noise (48000 * 3);
        for (auto& s : noise) s = d (rng);
        auto r = analyse (noise, 48000.0, "va_noise.wav");
        check (r.ok && r.blocks.empty(), "white noise gives no blocks (got " + juce::String ((int) r.blocks.size()) + ")");
        check (r.info.quality == Quality::Red, "...and is rated Red");
        auto s = analyse (std::vector<float> (48000 * 2, 0.0f), 48000.0, "va_silence.wav");
        check (s.ok && s.blocks.empty(), "silence gives no blocks");
    }
    {
        const double sr = 48000.0;
        const std::vector<Note> melody = { { 60, 0.5 }, { 64, 0.4 }, { 67, 0.6 }, { 55, 0.45 }, { 72, 0.5 } };
        // Two voices a third of a semitone apart with their own vibrato, plus a long reverb tail.
        auto a = sing (melody, sr, 25.0, 0.01, 0.0, 0.0, 1);
        auto b = sing (melody, sr, 35.0, 0.0, 38.0, 2.0, 2);
        std::vector<float> doubled (a.size());
        for (size_t i = 0; i < a.size(); ++i) doubled[i] = 0.6f * a[i] + 0.6f * b[i];
        auto wet = reverb (doubled, sr, 1.5, 0.8);
        auto r = analyse (wet, sr, "va_doubled.wav");
        std::printf ("  info doubled and reverberant: quality %s (clarity %.3f, voiced %.2f, coverage %.3f), %d blocks\n",
                     qualityName (r.info.quality), r.info.meanClarity, r.info.voicedFraction, r.info.coverage, (int) r.blocks.size());
        check (r.ok && r.info.quality != Quality::Green, "doubled and reverberant singing is not rated Green");
    }

    // ===================================================================
    if (auto path = juce::SystemStats::getEnvironmentVariable ("VA_TEST_AUDIO", {}); path.isNotEmpty())
    {
        std::printf ("\n== Real file: %s ==\n", path.toRawUTF8());
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        PyinSettings ps;
        auto env = [] (const char* n, double d) { auto v = juce::SystemStats::getEnvironmentVariable (n, {}); return v.isEmpty() ? d : v.getDoubleValue(); };
        ps.betaB = env ("VA_BETAB", ps.betaB);
        ps.frameSize = (int) env ("VA_FRAME", ps.frameSize);
        ps.hopSize = (int) env ("VA_HOP", ps.hopSize);
        ps.switchProb = env ("VA_SWITCH", ps.switchProb);
        if (auto dump = juce::SystemStats::getEnvironmentVariable ("VA_DUMP", {}); dump.isNotEmpty())
        {
            std::vector<float> mono; double sr = 0; juce::String err;
            decodeToMono (juce::File (path), mono, sr, err, [] (float) { return true; });
            PyinResult pr;
            PyinAnalyser (sr, ps).analyse (mono, [] (float) { return true; }, pr);
            juce::String csv;
            for (size_t i = 0; i < pr.track.midi.size(); ++i)
                csv << juce::String (pr.track.firstCentreSec + (double) i * pr.track.hopSec, 4) << "," << juce::String (pr.track.midi[i], 3) << "," << juce::String (pr.track.clarity[i], 3) << "\n";
            juce::File (dump).replaceWithText (csv);
        }
        auto r = analyseAudioFile (juce::File (path), SegmenterRules(), ps, [] (float) { return true; });
        std::printf ("  info %s in %.1f s: %d blocks, quality %s (clarity %.3f, voiced %.2f, coverage %.3f)\n",
                     r.ok ? "analysed" : r.error.toRawUTF8(), (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, (int) r.blocks.size(),
                     qualityName (r.info.quality), r.info.meanClarity, r.info.voicedFraction, r.info.coverage);
        for (size_t i = 0; i < std::min<size_t> (r.blocks.size(), 40); ++i)
            std::printf ("       %7.3f - %7.3f  midi %.0f\n", r.blocks[i].startSec(), r.blocks[i].endSec(), r.blocks[i].midi());
    }

    std::printf ("\n== Phase 4: colour state machine ==\n");
    {
        auto blk = [] (int id, double s, double e, int midi) { NoteBlock b; b.id = id; b.startUs = (int64_t) (s * 1e6); b.endUs = (int64_t) (e * 1e6); b.midiMilli = midi * 1000; return b; };
        auto hzOf = [] (double midi, double cents = 0.0, double a4 = 440.0) { return a4 * std::pow (2.0, (midi - 69.0 + cents / 100.0) / 12.0); };
        const double fr = 0.005;
        // feed frames from t0 to t1 at a pitch offset (cents), voiced
        auto feed = [&] (Matcher& m, double t0, double t1, double midi, double cents, bool voiced = true)
        { for (double t = t0; t < t1 - 1e-9; t += fr) m.onPoint (t, fr, voiced, voiced ? hzOf (midi, cents) : 0.0); };

        { Matcher m; m.setBlocks ({ blk (1, 1, 2, 60) });
          check (m.stateOf (1) == BlockState::Blue, "Blue before the playhead reaches the block");
          feed (m, 1.0, 2.0, 60, 10); m.onPlayhead (3.0);
          check (m.stateOf (1) == BlockState::Green, "10 cents off for the whole block: Green"); }
        { Matcher m; m.setBlocks ({ blk (1, 1, 2, 60) }); feed (m, 1.0, 2.0, 60, 60); m.onPlayhead (3.0);
          check (m.stateOf (1) == BlockState::Amber, "60 cents off: Amber"); }
        { Matcher m; m.setBlocks ({ blk (1, 1, 2, 60) }); feed (m, 1.0, 2.0, 60, 150); m.onPlayhead (3.0);
          check (m.stateOf (1) == BlockState::Red, "150 cents off: Red"); }
        { Matcher m; m.setBlocks ({ blk (1, 1, 2, 60) }); feed (m, 1.0, 2.0, 60, 0, false); m.onPlayhead (3.0);
          check (m.stateOf (1) == BlockState::Red, "no pitch detected: Red"); }
        { Matcher m; m.setBlocks ({ blk (1, 1, 2, 60) }); feed (m, 1.0, 1.4, 60, 0); feed (m, 1.4, 2.0, 60, 150); m.onPlayhead (3.0);
          check (m.stateOf (1) == BlockState::Red, "green for only 40% of the block: not Green (Amber needs 50% too): Red"); }
        { Matcher m; m.setBlocks ({ blk (1, 1, 2, 60) }); feed (m, 1.0, 1.6, 60, 0); feed (m, 1.6, 2.0, 60, 150); m.onPlayhead (3.0);
          check (m.stateOf (1) == BlockState::Green, "green for 60% of the block: Green"); }
        { Matcher m; m.setBlocks ({ blk (1, 1, 2, 60) }); feed (m, 1.0, 1.6, 60, 0); feed (m, 1.6, 2.0, 60, 150);
          check (m.stateOf (1) == BlockState::Green, "...already Green while still inside the block");
          m.onPlayhead (3.0);
          check (m.stateOf (1) == BlockState::Green, "...and an earned colour is never lost"); }
        { Matcher m; m.setBlocks ({ blk (1, 1, 2, 60) }); feed (m, 1.0, 1.3, 60, 0);
          check (m.stateOf (1) == BlockState::Green, "while active with nothing earned: the instant colour (Green)");
          feed (m, 1.3, 1.4, 60, 150);
          check (m.stateOf (1) == BlockState::Red, "...then Red when the singer goes off"); }
        { Matcher m; m.setBlocks ({ blk (1, 0, 1, 60), blk (2, 1, 4, 62) });
          feed (m, 0.0, 1.0, 60, 0); feed (m, 1.0, 4.0, 62, 150);
          check (m.accuracyPercent() == 25, "accuracy is time-weighted: 1 s green of 4 s is 25%, not 50%"); }
        { MatchSettings s; s.a4 = 442.0;
          Matcher n; n.setSettings (s); n.setBlocks ({ blk (1, 0, 1, 69) });
          for (double t = 0; t < 1.0; t += fr) n.onPoint (t, fr, true, 442.0); n.onPlayhead (2.0);
          check (n.stateOf (1) == BlockState::Green, "A4 = 442: singing 442 Hz on the A block is Green"); }
        { Matcher m; MatchSettings s; s.greenCents = 5.0; m.setSettings (s); m.setBlocks ({ blk (1, 0, 1, 60) });
          feed (m, 0.0, 1.0, 60, 10); m.onPlayhead (2.0);
          check (m.stateOf (1) == BlockState::Amber, "green slider at 5 cents: 10 cents off is only Amber"); }
        { Matcher m; MatchSettings s; s.octaveShift = 1; m.setSettings (s); m.setBlocks ({ blk (1, 0, 1, 60) });
          for (double t = 0; t < 1.0; t += fr) m.onPoint (t, fr, true, 523.2511); m.onPlayhead (2.0);
          check (m.stateOf (1) == BlockState::Green, "octave +1: 523.25 Hz matches the C4 block"); }
        { Matcher m; m.setBlocks ({ blk (1, 0, 1, 60), blk (2, 1, 2, 60) });
          feed (m, 0.0, 1.0, 60, 0); feed (m, 1.0, 2.0, 60, 150); m.onPlayhead (3.0);
          m.resetFrom (1.0);
          check (m.stateOf (1) == BlockState::Green && m.stateOf (2) == BlockState::Blue, "resetFrom(1 s): earlier block keeps its result, later one is cleared");
          check (m.accuracyPercent() == 100, "accuracy is recomputed after a reset"); }
        { Matcher m; m.setBlocks ({ blk (1, 1, 2, 60) }); MatchSettings s; s.refOffsetSec = 10.0; m.setSettings (s);
          feed (m, 11.0, 12.0, 60, 0); m.onPlayhead (13.0);
          check (m.stateOf (1) == BlockState::Green, "reference offset 10 s: the block is measured at 11 to 12 s"); }
    }

    std::printf ("\n== Your notes against the reference ==\n");
    {
        TraceStore tr;
        auto add = [&] (double t0, double t1, double midi) { for (double t = t0; t < t1; t += 0.005) tr.add ({ t, (float) midi, 1.0f, true }); };
        auto gap = [&] (double t0, double t1) { for (double t = t0; t < t1; t += 0.005) tr.add ({ t, 0.0f, 0.0f, false }); };
        add (1.0, 1.5, 60.3);  gap (1.5, 1.6);            // note 1, 100 ms of silence
        add (1.6, 2.0, 62.0);  add (2.0, 2.4, 64.0);      // note 2, then a jump with no gap: note 3
        gap (2.4, 2.5); add (2.5, 2.53, 67.0);            // a 30 ms blip: dropped
        auto notes = segmentSung (tr, 0.0, 10.0);
        check (notes.size() == 3, "three sung notes found (the 30 ms blip is dropped)");
        check (notes.size() == 3 && std::abs (notes[0].midi - 60.3) < 0.01 && std::abs (notes[0].startSec - 1.0) < 0.01, "note 1 keeps its true pitch (60.3, not snapped) and start");
        check (notes.size() == 3 && std::abs (notes[1].midi - 62.0) < 0.01 && std::abs (notes[2].midi - 64.0) < 0.01, "a jump of 2 semitones with no gap splits the note");
        check (segmentSung (tr, 1.7, 1.9).size() == 1, "only the range asked for is segmented");

        ToneSequence seq;
        seq.start (440.0, 330.0, 0.5, 48000.0);
        std::vector<float> out ((size_t) seq.totalSamples() + 100, 0.0f);
        seq.render (out.data(), (int) out.size());
        auto zc = [&] (int64_t a, int64_t b) { int c = 0; for (int64_t i = a + 1; i < b; ++i) if (out[(size_t) i - 1] < 0 && out[(size_t) i] >= 0) ++c; return c / ((b - a) / 48000.0); };
        check (std::abs (zc (4800, 19200) - 440.0) < 12.0, "first tone is the reference pitch (440 Hz)");
        float gapPeak = 0; for (int64_t i = 24500; i < 33000; ++i) gapPeak = std::max (gapPeak, std::abs (out[(size_t) i]));
        check (gapPeak == 0.0f, "silence between the two tones");
        check (std::abs (zc (24000 + 9600 + 4800, 24000 + 9600 + 19200) - 330.0) < 12.0, "second tone is your pitch (330 Hz)");
        check (! seq.isActive(), "sequence ends by itself");
    }

    std::printf ("\n== Tone matching EQ ==\n");
    {
        const double sr = 48000.0;
        std::mt19937 rng (5);
        std::normal_distribution<float> nd (0.0f, 0.1f);
        auto noise = [&] (size_t n) { std::vector<float> v (n); for (auto& s : v) s = nd (rng); return v; };
        auto spectrumOf = [&] (const std::vector<float>& x, BandDb& out)
        { BandSpectrum s (sr); s.add (x.data(), (int) x.size()); return s.result (out); };

        // a sine lands in its own band
        {
            std::vector<float> sine (96000);
            for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.3f * std::sin (2.0f * 3.14159265f * 1000.0f * (float) i / 48000.0f);
            BandDb s; const bool ok = spectrumOf (sine, s);
            int peak = (int) (std::max_element (s.begin(), s.end()) - s.begin());
            check (ok && std::abs (bandCentre (peak) - 1000.0) < 80.0, "a 1 kHz tone shows up in the 1 kHz band");
        }
        check (! [&] { BandSpectrum s (sr); std::vector<float> q (24000, 0.0f); s.add (q.data(), (int) q.size()); BandDb d; return s.result (d); }(), "silence is not measured");

        auto ref = noise (384000);                         // white: bright
        auto mine = noise (384000);                        // dark: one-pole low-pass near 2 kHz
        { float z = 0.0f; const float a = 1.0f - std::exp (-2.0f * 3.14159265f * 2000.0f / 48000.0f); for (auto& s : mine) { z += a * (s - z); s = z * 3.0f; } }
        BandDb rs, ms;
        check (spectrumOf (ref, rs) && spectrumOf (mine, ms), "both voices measured");
        auto corr = computeCorrection (rs, ms);
        float maxAbs = 0.0f; for (auto v : corr) maxAbs = std::max (maxAbs, std::abs (v));
        check (maxAbs <= 12.001f, "the correction never goes past +/-12 dB");
        check (corr[(size_t) 19] > 3.0f && corr[(size_t) 19] > corr[(size_t) 9], "the dark voice is told to boost the highs more than the mids");

        auto errorOf = [&] (const BandDb& a, const BandDb& b)
        {
            double meanDiff = 0, e = 0; int n = 0;
            for (int i = 0; i < numBands; ++i) if (bandCentre (i) >= 100 && bandCentre (i) <= 8000) { meanDiff += a[(size_t) i] - b[(size_t) i]; ++n; }
            meanDiff /= n;
            for (int i = 0; i < numBands; ++i) if (bandCentre (i) >= 100 && bandCentre (i) <= 8000) { const double d = a[(size_t) i] - b[(size_t) i] - meanDiff; e += d * d; }
            return std::sqrt (e / n);
        };
        const double before = errorOf (ms, rs);

        EqFilterBank bank;
        bank.prepare (sr, 1);
        const auto g = eqGainsFor (corr, sr);
        {   // the combined response follows the curve at the filter centres
            double worst = 0.0;
            for (size_t j = 0; j < (size_t) numEqBands; ++j) worst = std::max (worst, std::abs (eqResponseDb (g, eqCentres[j], sr) - curveAt (corr, eqCentres[j])));
            std::printf ("       worst fit error %.2f dB\n", worst);
            check (worst < 1.0, "the 12 filters together follow the correction curve within 1 dB at their centres");
        }
        bank.setTargets (g);
        juce::AudioBuffer<float> z (1, 512); z.clear();
        for (int i = 0; i < 80; ++i) bank.process (z);       // let the gains settle
        std::vector<float> fixedMine = mine;
        bool changed = false;
        for (size_t i = 0; i + 512 <= fixedMine.size(); i += 512)
        {
            juce::AudioBuffer<float> b (1, 512);
            std::copy (fixedMine.begin() + (std::ptrdiff_t) i, fixedMine.begin() + (std::ptrdiff_t) i + 512, b.getWritePointer (0));
            changed |= bank.process (b);
            std::copy (b.getReadPointer (0), b.getReadPointer (0) + 512, fixedMine.begin() + (std::ptrdiff_t) i);
        }
        BandDb fs; spectrumOf (fixedMine, fs);
        const double after = errorOf (fs, rs);
        for (int i = 0; i < numBands; ++i) std::printf ("       %6.0f Hz  before %6.2f  corr %6.2f  after %6.2f\n", bandCentre (i), ms[(size_t) i] - rs[(size_t) i], corr[(size_t) i], fs[(size_t) i] - rs[(size_t) i]);
        std::printf ("       tone error before %.2f dB, after %.2f dB\n", before, after);
        check (changed && after < before * 0.5, "after the EQ the tonal error is less than half what it was");
        bool finite = true; for (float s : fixedMine) if (! std::isfinite (s)) { finite = false; break; }
        check (finite, "the EQ output stays finite");

        EqFilterBank off; off.prepare (sr, 1);
        juce::AudioBuffer<float> keep (1, 512); for (int i = 0; i < 512; ++i) keep.setSample (0, i, nd (rng));
        juce::AudioBuffer<float> copy; copy.makeCopyOf (keep);
        const bool did = off.process (keep);
        check (! did && std::memcmp (keep.getReadPointer (0), copy.getReadPointer (0), 512 * sizeof (float)) == 0, "with every gain at 0 the audio is not touched at all");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL BLOCK TESTS PASSED" : "BLOCK TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
