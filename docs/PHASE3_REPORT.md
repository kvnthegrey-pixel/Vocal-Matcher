# Phase 3 report: reference vocal to note blocks

Status: **built, and all tests pass on Linux. Windows CI result and your test in Cubase are still to come.**

## What was done

1. **First-open prompt.** With no reference, the window shows "Add your reference vocal" and a
   load button. MIDI is offered first; audio (WAV, FLAC, AIFF, OGG) is marked experimental.
2. **MIDI import (primary path).** One MIDI track becomes the melody. If the file has several
   tracks with notes, you pick the lead vocal from a list. The drum channel is ignored.
   Overlapping notes are resolved by letting the later note cut the earlier one. Notes under
   80 ms are dropped.
3. **Audio analysis (experimental).** Runs on its own background thread with a progress bar and a
   Cancel button, and carries on if you close the window. It is probabilistic YIN (pYIN): many
   YIN thresholds give weighted pitch candidates per frame, then a Viterbi search over a
   voiced/unvoiced model picks the best path. It is not frame merging.
4. **The four post-processing rules**, applied automatically before you see the blocks:
   - minimum note 80 ms
   - a note that moves more than 40 cents from its centre is split
   - an unvoiced gap over 60 ms ends the note
   - adjacent notes within 30 cents and under 40 ms apart are merged
5. **Block editor.** Click a block to select it (Shift adds), then **split** at the cursor,
   **merge**, **delete** or **pitch-shift** by a semitone. **Undo** steps back through edits.
   Wheel pans, Ctrl+wheel zooms, **Fit all** resets the view. **Lock reference** freezes the blocks
   (every edit is refused until you unlock). **Export MIDI** writes the blocks back out.
6. **Saved state.** Blocks, ids, lock state, source name, quality and every analysis setting are
   stored in the Cubase project. Reloading restores the exact numbers with no re-analysis.
7. **Analysis Quality indicator.** Green for MIDI and clean monophonic audio, Amber for
   processed but usable, Red for heavy reverb or doubles.

## Checklist against the strict document (section 2)

| Item | State | Evidence |
| --- | --- | --- |
| MIDI import is the primary path | Done | prompt and file chooser list MIDI first; `MidiReference` |
| WAV analysis marked "experimental" in the UI | Done | prompt text, status line, quality line |
| WAV analysis only on a background thread | Done | `ReferenceJob`; host test checks `isAnalysing()` while it runs |
| pYIN or equivalent, no simple frame merging | Done | `PyinAnalyser` (see note below) |
| Minimum note 80 ms | Done | tests: 60 ms dropped, 100 ms kept |
| 40 cent deviation forces a split | Done | tests: 30 cent wander stays one note, 60 cent move splits |
| Unvoiced gap over 60 ms ends the note | Done | tests: 70 ms ends it, 50 ms does not |
| Merge notes within 30 cents and under 40 ms | Done | four boundary tests |
| Select, split, merge, delete, pitch-shift before locking | Done | `BlockList` and `BlockEditor`; tests for each, and for the lock refusing edits |
| Not "analyse and forget" | Done | edits, undo, lock, export |
| Parameters and blocks stored in the project state | Done | 200 blocks round-trip through XML; a fresh plugin instance restores them without analysing |
| Analysis Quality indicator, three colours | Done | see the numbers below |
| Works in Cubase (file dialog, window, save and reopen) | **Open, needs you** | see Gate |
| Windows CI builds and runs the tests | **Open** | runs on push |

## Decisions I made where the document was silent

- **Gaps up to 60 ms are bridged**, not just "gaps over 60 ms end the note": a short dropout does
  not split a held note.
- **A new pitch must persist 3 frames** (about 35 ms) before it forces a split, so a one-frame
  glitch is dropped instead of cutting the note in two. The 40 cent rule is measured against the
  median pitch of the note so far.
- **Block pitch is rounded to the nearest semitone** for audio (a stored setting, on by default),
  so a slightly flat stem does not teach you to sing flat. MIDI pitches are kept as they are.
- **The split, gap and merge rules are not applied to MIDI**, which is exact; only the 80 ms
  minimum and overlap handling are. Merging repeated MIDI notes would destroy real repeats.
- **pYIN is written here, not taken from a library.** The strict document's "existing library
  only" rule is stated for the live pitch path (Phase 2); for this step it allows pYIN "or
  equivalent". No suitable library builds cleanly under JUCE and CMake (the usual C++ one is a
  GPL Vamp plugin), so this follows the published method (Mauch and Dixon, 2014) using YIN's
  difference function. I checked it against librosa's pYIN on your stem: where both report a
  pitch, the median difference is 0.07 of a semitone. Tell me if you want it replaced.

## Real stem

Your `suno.flac` (3 min 20 s, stereo, almost mono) analyses in about 2 to 5 seconds on the
build machine and gives about 465 blocks, rated **Amber** (clarity 0.94, 74% of loud frames voiced,
83% of voiced frames inside blocks). Expect many short blocks and a few stray high notes on
a Suno vocal. That is what the editor is for. For comparison, a clean synthetic melody scores
Green and white noise scores Red with no blocks. The Amber/Red cut-offs are first guesses and
need your real stems to tune.

## Tests

| Program | What it covers | Result |
| --- | --- | --- |
| `PitchTests` | Phase 2 pitch engine | all pass |
| `BlockTests` | the four rules and their boundaries, editing, lock, undo, saved state, MIDI round trip, overlaps and drums, pYIN on synthetic singing at 44.1 and 48 kHz (right semitone for all 6 notes with vibrato, edges within 60 ms), noise and silence give no blocks, doubled and reverberant singing is not Green | all pass |
| `HostTests` | the real processor and editor without Cubase: 10 s of random audio comes out bit-identical, 0 latency, MIDI and audio reference, edit, lock, save, restore into a fresh instance, live pitch beside it | all pass |

## Not verified in this phase

- The file chooser, the MIDI track menu and the progress bar inside Cubase on Windows (only
  checked headlessly).
- Loading a real vocal MIDI file (I only had your FLAC stem and my own generated MIDI).
- Windows build and test run (result to be added).

## Gate

Install the new build, then in Cubase:

1. Open the plugin: it asks for a reference vocal.
2. Load your Suno stem (or a MIDI file) and watch the progress bar. Blocks appear.
3. Zoom in (Ctrl+wheel), click blocks, try split, merge, delete, pitch -1/+1 and undo.
4. Lock the reference. Check edits stop. Unlock.
5. Save the Cubase project, close it, reopen it, open the plugin: your edited blocks are back
   without any analysing.
6. Tell me the Analysis Quality colour you got and whether the blocks look like the melody.
