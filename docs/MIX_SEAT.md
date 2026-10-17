# Mix Seat

A VST3 for Cubase Pro 12 (Windows). It goes on the **vocal track**. You upload the beat, it measures where the beat is crowded and where it leaves room, and it nudges the vocal's EQ so the voice sits in the gaps.

**End goal in plain words:** when a beat and a vocal are loud in the same frequencies, they fight, and the vocal sounds buried or muddy even when it is loud enough. Mix Seat gently moves the vocal out of the spots the beat is already using and into the spots the beat leaves open, so the voice is clear without you turning it up.

## How to use it

1. Put **Mix Seat** on the vocal track (Fx > EQ).
2. Press **Upload beat...** and choose the instrumental (WAV, AIFF, FLAC, OGG or MP3). Use the beat **without the vocal**.
   - Why: the plugin cannot hear other tracks in the project. A VST on one track only receives that track's audio, so the beat has to be handed to it as a file.
   - If you skip this: the plugin has nothing to compare against and does nothing.
3. Play the vocal through the track (press play in Cubase, or just sing with monitoring on). After about 3 seconds of loud singing it has learned how the vocal sounds, and the EQ starts.
   - Why: the EQ must be based on *your* vocal, because the same beat needs a different nudge for a thin voice than for a boomy one.
   - If you skip this: no EQ is applied. You can also press **Learn vocal from file...** and give it a dry vocal instead.
4. Look at the bottom-left panel. It lists the biggest moves and why, for example "Cut 3.5 dB around 318 Hz: the beat is busy in the low-mids, so your voice would only be buried and add clutter there."
5. Use **Bypass** to compare before and after. Level stays about the same, so it is a fair comparison.

## What it does, step by step

1. **Measures the beat.** It splits the sound into 25 bands, one third of an octave wide (63 Hz up to 16 kHz), and averages the energy in each band over the whole song and over each 4-second stretch.
   - Why 25 third-octave bands: this is about how finely the ear separates frequencies, and it is the same measuring code Vocal Assistor already uses for tone matching.
   - Why 4-second stretches: short enough to follow a verse turning into a chorus, long enough that one drum hit does not swing the EQ. They are only used when "Follow the beat" is on.
2. **Learns the vocal** the same way, only counting the loud parts (within 25 dB of its 90th-percentile level, and above -55 dBFS), so silence and breaths do not count. It needs 3 seconds of those loud parts before it trusts the result.
3. **Compares shapes, not levels.** For both tracks it draws the straight line (in dB per octave) that best fits the spectrum, and keeps only what sticks up or dips below that line.
   - Why: every track has a natural slope (a bass-heavy beat, a dark vocal). That is not a clash. Only bumps and dips say where it is crowded or open.
   - Result: making the beat or the vocal louder, or brighter or darker overall, changes nothing. This is checked by the tests.
4. **Works out the nudge** for each band: a cut where the beat sticks up, a lift where the beat dips, each scaled by how much that region matters for hearing a voice (the table below).
5. **Applies it** with 12 gentle EQ filters (the same ones Vocal Assistor uses), with no delay, and keeps the overall level where it was.

## The numbers, and why they are there

These are starting values, not facts of nature. Tune them by ear. They are all in `Source/Seat/SeatEngine.h`; the first two are knobs on the screen.

| Number | What it does | Where it comes from | If you change or skip it |
|---|---|---|---|
| **How much: 70 %** | Scales every move. 0 % does nothing, 100 % is the full nudge. | A safe default that is audible but not obvious. It is a knob. | Higher: bolder moves, a more "processed" sound. Lower: subtler. |
| **Biggest change: 4 dB** | The most it will cut. Lifts are limited to 60 % of this (2.4 dB). | A few dB is what a careful engineer does; bigger swings change the voice's character. Lifts are limited more than cuts because boosting adds noise and harshness. | Without a limit, an odd beat could swing the voice by 10 dB or more. |
| **Importance of each region** (0.15 at 63 Hz up to 1.0 from 1 to 3 kHz, 0.2 at 16 kHz) | Scales the move by how much that region matters for understanding a voice. | An assumption: the 1-3 kHz range carries most of the clarity of speech and singing; the very low and very high ends carry little of it. | Without it, the plugin would spend big moves on sub-bass and air that make no audible difference to the vocal. |
| **60 % of the shape error** (the 0.6 factor) | Turns "how far the beat sticks up" into dB: a bump of 10 dB gives a 6 dB move before the other limits. | Chosen so a clear clash reaches the limit, and a mild one gives a mild move. | Higher reacts too hard to small differences. |
| **Never boost below 80 Hz; above 12 kHz at most 1 dB** | Protects against lifting rumble and hiss. | Boosting these regions adds noise and mud, and the vocal has little to give there anyway. | Without it, a beat with a hole in the bass would make the plugin lift rumble. |
| **No lift into a band that already sticks out of the vocal** (more than 4 dB above its own trend, fully blocked at 8 dB) | Avoids boosting a harsh resonance or "s" sounds. | A band that already sticks out is probably a resonance or sibilance. | Without it, a lift into a harsh spot makes the vocal piercing. |
| **Cut less where the vocal is thin** | A cut only helps where the vocal has energy to cut. | Common sense: cutting something that is not there changes nothing but the balance. | Without it, a thin vocal would be cut in places it has nothing to give. |
| **3 seconds to learn** | Minimum loud singing before the vocal measurement is trusted. | Shorter than that is one or two words, not the voice's usual sound. | Shorter would start the EQ from a measurement of a single vowel. |
| **Level kept (weighted average of the moves from 150 Hz to 6 kHz forced to 0 dB)** | The nudge moves energy; it does not make the vocal louder or quieter. | So that "bypass" is a fair comparison and you do not re-balance the fader after every change. | Without it, the vocal would jump in level when the plugin switches on. |

## Following the beat

- **Off (default):** one EQ for the whole song, from the average of the beat. Safe and steady.
- **On:** the EQ follows the beat second by second (blending between the 4-second stretches). Use it when the beat changes a lot (a sparse verse and a thick chorus).
- **Beat starts at (s):** if the beat file starts later than the start of the Cubase project (for example at bar 5), enter the time here so the plugin lines the beat up with the project clock.
  - If you skip this when the offset is not zero: the EQ changes arrive early or late.

## Limits you should know about

- It only reshapes **the vocal**. It cannot tell the beat to make room. If the beat is the problem, a cut on the beat is the better fix.
- It listens to a **file you give it**, not to the project. If you change the beat in the project, upload the new bounce.
- It uses a **long-term average**, so it is a tonal balance nudge. It does not replace a compressor, a de-esser or a good microphone, and it cannot fix a bad recording.
- It measures a **single mono mix** of the vocal. A stereo vocal is measured as the average of both sides.
- The numbers above are tuned from reasoning and synthetic tests, not yet from hours of real mixes. Treat the first results as a starting point and use **How much** and **Biggest change** to taste.

## Project saving

The measured beat, the learned vocal and every knob are saved inside the Cubase project. When you reopen it, the plugin works immediately, even if the beat file has been moved. The beat file itself is never copied into the project, only its measured shape (about 25 numbers per 4 seconds).

## Tests

`SeatCoreTests` checks the maths (cuts and lifts land where they should, loudness and slope are ignored, limits hold, saved text round-trips, real audio files are measured). `SeatHostTests` runs the real plugin in a simulated host (bit-exact audio when nothing is loaded, measured cut and lift on test tones, following the beat through the song, bypass, trim, freeze, save and reopen, bad files, mono, other sample rates). Both run on every build.
