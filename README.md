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
