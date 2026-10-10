# Phase 2 report: pitch engine

Status: **built and tested on Linux and on Windows (CI run 38034839479 passed, including the pitch tests). Your listening test in Cubase is the remaining gate.**

## What was done

1. **Library fixed in the vendored copy** (every change is listed in `third_party/NOTICE.txt`).
   The spike showed 110 Hz reading 10.5 cents sharp. The cause: the library used the raw
   autocorrelation where the McLeod method needs the normalised square difference function.
   It now computes the real NSDF, still using the library's own FFT autocorrelation. The
   library also allocated memory on every call, hid its peak height, hard-coded its limits,
   and the YIN part read past the end of its buffer and dereferenced an uninitialised pointer.
   All of that is fixed.
2. **PitchTracker** (`Source/PitchTracker.h`): mono input, fixed 2048-sample window, 256-sample
   hop, McLeod by default with YIN switchable, 80 to 1200 Hz (anything outside is discarded),
   voicing gate, 3-frame median on frequency before any conversion to cents, silence floor
   at about -80 dBFS. Allocates only when constructed.
3. **PitchEngine** (`Source/PitchEngine.h`): the audio thread copies a mono mix into a lock-free
   FIFO and nothing else. A worker thread runs the tracker. Points go to a second FIFO that
   the window reads.
4. **Settings** (saved with the project): pitch method, voicing gate for each method (default
   0.8 McLeod, 0.15 YIN), live-trace latency compensation -50 to +50 ms.
5. **Test window**: shows the live note, cents, Hz and confidence, so you can check the engine
   in Cubase. The piano roll is Phase 5.

## Checklist against the strict document

| Item | State | Evidence |
| --- | --- | --- |
| Existing pitch library, not a hand-written detector | Done | `adamski/pitch_detector`, patched copy, changes listed in NOTICE.txt |
| McLeod default, YIN switchable | Done | `method` parameter, both tested |
| Fixed 2048 window, 256 hop, no resizing or allocation on the audio thread | Done | constants in `PitchTracker`; audio thread only fills a preallocated FIFO |
| 3-frame median on frequency before cents | Done | `PitchTracker::analyseFrame`; a gap in voicing resets it |
| Voicing gate, user control, defaults 0.8 / 0.15 | Done | test "voicing gate is adjustable" |
| Range 80 to 1200 Hz, out of range discarded | Done | tests with 60 Hz and 1500 Hz sines |
| Latency compensation slider -50..+50 ms, shifts only the live trace | Done | `shiftedCentreSample` and its tests; the audio path never sees it. Nothing draws the trace until Phase 5 |
| Sines 110 / 220 / 440 / 880 Hz within 5 cents | Done | worst error 0.00 to 0.01 cents (McLeod), 0.00 to 0.11 (YIN). Also 123.47, 329.63, 1000 Hz and three 8-harmonic tones |
| Silence and white noise give zero pitch points | Done | 0 voiced points for silence and for noise at three levels, both methods |
| Measured processing delay documented | Done | see below |
| Windows CI builds and runs the tests | Done | run 38034839479: plugin build, tests, installer all passed. The test log itself could not be read from this session, so the Windows delay figures are not recorded |
| Works in Cubase on a real voice | **Open, needs you** | see Gate |

## Measured delay

Measured with the threaded engine fed 512-sample blocks at real time (48 kHz) on the Linux
build machine:

| | First voiced point after a note starts | Analysis cost per frame (hop is 5.33 ms) |
| --- | --- | --- |
| McLeod | 21.1 ms | 0.05 ms |
| YIN | 32.0 ms | 1.1 ms |

The window refreshes at 30 Hz, so the screen adds at most 33 ms: worst case about 54 ms
(McLeod) and 65 ms (YIN), inside the 80 ms target. This excludes your audio interface's own
buffer, which Cubase handles. Figures on your Windows machine may differ and are worth
re-checking in the listening test. The plugin reports 0 samples of latency to Cubase and
never delays your audio.

## Things worth knowing

- The window and hop are fixed in samples, not milliseconds. At 44.1 kHz they are 46 ms and 5.8 ms.
- A tone above 1200 Hz is discarded, not shown an octave low.
- The median filter does not smooth across a gap, so a note that restarts is not dragged
  toward the previous one.
- White noise and clean speech-like consonants are rejected by the gate. Breathy vocals may
  need the gate lowered, which is why it is a control.

## Found in your first Cubase screenshot

The Phase 2 window opened, but the pitch method dropdown was blank. The dropdown was linked to its setting before its items were added, so nothing showed as selected (McLeod was still the active default). Fixed by adding the items first.

## Gate

Install the new build (same steps as before), put it on the vocal track, and check:

1. Sing or hold a note: the window shows a steady note name, with cents moving as you drift.
2. Silence and breaths: it says "no pitch".
3. Switch between McLeod and YIN: both track you.
4. Your audio sounds exactly as before.

Tell me if any of those fail. Phase 3 does not start until Phase 2 is ticked off.
