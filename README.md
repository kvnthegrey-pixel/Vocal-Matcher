# Vocal Matcher (Vocal Assistor)

Vocal Assistor is a personal VST3 plugin for **Cubase Pro 12 on Windows**. You insert it on your vocal track, load a reference vocal (a Suno stem), and it turns the stem into note blocks on a scrolling piano roll. While you record, each block turns green, amber or red as you sing, so you can see exactly which notes to hit.

Personal use only. No pitch correction, no lyrics, no harmonies, no change to your audio, no VST2, no macOS.

## Status

| Phase | State |
| --- | --- |
| 1. Repository and build | Done. Loads in Cubase Pro 12. Release tag not yet published |
| 2. Pitch engine | Done. Confirmed reading a note in Cubase |
| 3. Reference vocal to note blocks | Built, tests pass on Linux and Windows CI; awaiting your test in Cubase |
| 4. Matching and colours | Not started |
| 5. Piano roll display | Not started |
| 6. Cubase sync and saved state | Not started |
| 7. User testing | Not started |

See `docs/PHASE3_REPORT.md` for the current checklist and findings.

## Performance Assist (fourth plugin)

A separate coaching plugin for singing against the Suno vocal. Load the reference, press play in Cubase and sing:

- **Your voice is drawn as a wave** that rides at the height of the note you are singing, gets thicker when you are louder, and flows into the note blocks. It is green inside Perfect, amber inside Good, red when off, and an orange banner says when you are an octave high or low.
- **Every note is graded** Perfect (within 25 cents), Good (within 50 cents) or Miss, with a score, a streak multiplier (up to x4) and accuracy. The start of each note is forgiven (a scoop up to pitch), vibrato is averaged out, and a 50 ms consonant gap is not a mistake. All of these are settings.
- **Practise** one phrase at a time (it shows the times to type into Cubase's cycle locators; the plugin cannot set the cycle itself), or switch to **Ghost** mode so only the first note of each phrase is shown and the rest appear once graded.
- **Takes**: every pass (stop, or a loop jump) is kept with a score, a heatmap of where each note landed (flat, centred, sharp, octave slip), a plain-language read of your tendencies, and a **comp plan** that names the best take for each phrase.
- **Edit reference** fixes wrong blocks from the Suno stem (split, merge, delete, shift, lock).

The audio is an exact pass-through and the plugin reports no latency. See `docs/PERFORMANCE_ASSIST.md`.

## Install

See `docs/INSTALL.txt`.

## Build

Windows builds run in GitHub Actions on every push and on every tag that starts with `v`. A tag publishes a zipped `.vst3` folder and an Inno Setup installer to the Releases page.

To build locally you need CMake 3.22 or newer and a C++17 compiler:

```
cmake -S . -B build
cmake --build build --config Release --target VocalAssistor_VST3
```

## Third-party code

See `third_party/NOTICE.txt`.
