# Phase 4 and 5 report: live match against the reference

What was built
- The host transport (play, stop, loop, locate) is read in processBlock and travels with the audio into the pitch worker, so every pitch point has an exact project time. A new "pass" starts on play, loop jump or locate.
- Blocks turn Blue (not reached), then Green / Amber / Red. A block earns Green when green time is at least half of the whole block, Amber when time within the amber threshold is at least half. Earned colours are never lost. A passed block with nothing earned is Red. Unvoiced frames count as Red.
- Accuracy is green time over time under blocks (time-weighted, whole percent).
- Piano roll: the live pitch line is drawn over the blocks (brighter when the detector is confident, broken where there is no pitch). A red playhead; with Follow on the roll scrolls past it. Wheel or drag scrolls freely and turns Follow off; the Follow checkbox turns it back on; Follow also re-engages when playback starts.
- Big readout: ON only while inside the green threshold, OFF otherwise, "--" when stopped. Under it: "35 cents LOW - sing higher" or "HIGH - sing lower", and accuracy.
- Settings saved with the project: key, scale (shades rows), octave shift (-2..+2, moves the reference), reference start offset, green 5-50 cents (default 25), amber 50-200 (default 100), A4 430-450 (default 440), trace timing (+/-50 ms).

Automated checks (all pass on Linux and Windows CI)
- 19 colour/accuracy unit tests (BlockTests): Green/Amber/Red, 40% vs 60% majority, never lose an earned colour, time-weighted accuracy, A4 442, 5-cent slider, octave shift, reset for loops, reference offset.
- Host scenario with a fake transport (HostTests): C4/E4/G4 blocks at 1-4 s; sung exact, 60 cents sharp, wrong note gives Green, Amber, Red; live cents reads +60; trace sits at the sung pitch; loop jump forgets later blocks and re-measures; stop leaves colours alone; octave shift resets and re-targets; audio still bit-identical, latency still 0; window paints in about 11 ms (about 90 fps).

Checklist (strict document)
- [x] Colours follow the thresholds and majority rule
- [x] Accuracy time-weighted
- [x] ON counts green only
- [x] Trace drawn in project time with latency slider
- [x] Playhead stationary, scroll back, Follow button
- [x] Key/scale shading, octave shift
- [x] Loop/locate resets results
- [x] Audio untouched, 0 latency
- [ ] Verified by ear and eye in Cubase on a real vocal (user test)
- [ ] Sample-to-screen delay measured on Windows (needs a loopback test in Cubase)
