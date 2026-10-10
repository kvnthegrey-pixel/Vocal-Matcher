# Phase 1 report: repository and build

Status: **built on Windows in GitHub Actions (run 38032057781 passed); not yet tagged. Cubase scans and lists it; insert and window not yet confirmed.**

## Checklist against the strict document

| Item | State | Evidence |
| --- | --- | --- |
| CMake + JUCE project, not Projucer-only | Done | `CMakeLists.txt`, JUCE 8.0.15 fetched at configure time |
| Normal mono or stereo insert, no custom bus layout | Done in code | `isBusesLayoutSupported` accepts matching mono or stereo only |
| Plugin reports 0 samples latency | Done in code | `setLatencySamples (0)` |
| Audio path leaves samples untouched | Done in code | `processBlock` is empty. The bit-exact unit test is a Phase 6 item |
| VST3 builds | Checked on Linux only | `Vocal Assistor.vst3` produced by `VocalAssistor_VST3` |
| GitHub Actions on windows-latest builds a Release .vst3 | Done for pushes to main. The tag-triggered release step has not run yet | `.github/workflows/build.yml`, run 38032057781 |
| Zipped .vst3 folder and Inno Setup installer | Built in CI and kept as a workflow artifact. Not attached to a release yet | workflow artifact `VocalAssistor-0.1.0-dev-win64` |
| Installer places the plugin in `C:\Program Files\Common Files\VST3` | Built, not yet run on a machine | `installer/VocalAssistor.iss` |
| Plain-text install guide with SmartScreen steps and Cubase scan | Done | `docs/INSTALL.txt` |
| Semantic versioning, first tag `v0.1.0-alpha` | Tag exists locally. Pushing it was refused (HTTP 403) by the session's policy | needs to be created on GitHub |
| Pitch library builds under CMake | Done | `PitchSpike` target compiled and ran |
| Plugin scans in Cubase | Done | You confirmed it listed as Vocal Assistor, vendor Vocal Matcher, category Fx\|Analyzer, VST 3 (installed in the per-user VST3 folder) |
| Plugin loads as an insert and opens its window | **Open, needs you** | insert it on an audio track and open the window |

## Pitch library spike

Library: `adamski/pitch_detector`, vendored under `third_party/` (see `third_party/NOTICE.txt`).
Test: one 2048-sample window at 48 kHz per pitch, McLeod method, pure sine at 0.5 amplitude.

| Input | Reported | Error | Within 5 cents |
| --- | --- | --- | --- |
| Sine 110 Hz | 110.672 Hz | +10.54 cents | **No** |
| Sine 220 Hz | 220.291 Hz | +2.29 cents | Yes |
| Sine 440 Hz | 440.618 Hz | +2.43 cents | Yes |
| Sine 880 Hz | 880.032 Hz | +0.06 cents | Yes |
| Silence | -1 (no pitch) | n/a | Yes |
| White noise | 150.7 Hz | n/a | **No**, reports a pitch |

What this means for Phase 2:

1. **The library as shipped misses the 5-cent requirement at 110 Hz** and is biased by a couple of cents elsewhere. Phase 2 has to fix the normalisation or interpolation in the vendored copy, or replace the algorithm, and say which in the Phase 2 report.
2. **The library gives no confidence value.** It returns a frequency or -1, so the voicing gate (threshold 0.8 for McLeod, 0.15 for YIN) cannot be built on it unchanged. Phase 2 has to expose the peak height from inside the module.
3. **White noise is reported as a pitch.** This is the gap the voicing gate exists to close, and it is why the gate is mandatory.
4. **The library's own constants** (cutoff 0.93, lower limit 80 Hz) are hard-coded and need to become settings.

## Not verified in this phase

- The release upload step (it only runs on a tag).
- Loading in Cubase.
- The installer on a machine with SmartScreen on.

## Gate

Create the `v0.1.0-alpha` tag on GitHub, then you confirm the empty plugin scans and loads in Cubase Pro 12. Phase 2 does not start before that.

## Fixes made on the way

The first CI run failed at Configure for two reasons, both fixed: the workflow named a Visual Studio version the runner does not have, and PowerShell split the `-DVA_VERSION_LABEL` argument at its dots.
