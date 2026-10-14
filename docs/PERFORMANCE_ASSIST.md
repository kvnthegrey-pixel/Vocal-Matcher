# Performance Assist

A VST3 for Cubase Pro 12 (Windows). It sits on your vocal track, listens to what you sing against a reference vocal (the Suno vocal), and turns it into a game with feedback you can act on. It is a separate plugin from Vocal Assistor: its own window, its own saved state, its own code in `Source/Assist/`. It shares only the pitch engine and the reference analysis code.

## How to use it

1. Insert **Performance Assist** on the vocal track (Fx > Analyzer).
2. **Load reference vocal...** A MIDI file of the Suno vocal is exact. An audio stem (WAV, FLAC, AIFF, OGG) is analysed into notes in the background. If the Suno stem produced a wrong block, fix it on the **Edit reference** tab and lock it.
3. Put the reference where it belongs on the timeline. If the reference starts later than 0:00 in your project, set **Reference starts at** in Settings.
4. Record-arm the track, press play and sing.

## What you see

- The note blocks scroll toward a fixed playhead. Blue = not reached, then green / amber / red as graded.
- **Your voice is a wave.** It sits at the height of the note you are singing, thickness shows loudness, and where it overlaps a block it is coloured by closeness. A glowing dot at the playhead shows the live result. Grey means no pitch was heard.
- **Octave banner**: orange, "octave too high / too low", when you are singing the right note in the wrong octave.
- **Cents meter**: centre is on pitch; the green and amber bands are the Perfect and Good limits.
- **HUD**: score, combo multiplier, notes in a row, accuracy, phrase number, and a PERFECT / GOOD / MISS flash as each note is graded.
- **Overview strip**: the whole song with each note coloured by its grade. Click a phrase to focus it; click again for all phrases.

## How notes are graded

| Setting | Default | What it does |
|---|---|---|
| Perfect within | 25 cents | A note counts as Perfect time while your smoothed pitch is this close to the target. |
| Good within | 50 cents | Half a semitone: still the right note, a little out. |
| Note start forgiveness | 100 ms | The first part of each note is ignored, because singers scoop up to pitch. Capped at 35% of a short note. |
| Vibrato smoothing | 180 ms | Your pitch is averaged over this window before it is judged (about one vibrato cycle at 5 to 6 per second). |
| Consonant bridge | 80 ms (fixed) | A gap with no pitch shorter than this inside a note is a consonant, not a mistake. A longer gap counts as a miss. |

A note earns **Perfect** when at least 60% of it was Perfect time, otherwise **Good** when at least 60% was Perfect-or-Good time, otherwise **Miss**. Points are 100 / 50 / 0, multiplied by 1.0 plus 0.1 for each note in a row (up to x4.0). A Miss resets the streak. Accuracy is the duration-weighted share of Perfect time (Good time counts half).

Singing the right note a whole number of octaves away (within a semitone) is recorded as an octave slip and counts as a Miss; it is never mistaken for "just flat or sharp".

## Takes, heatmap and comp plan

A take is filed when a pass ends: when you stop, or when the transport jumps (a Cubase cycle loop, a punch-in, a locate). A pass in which no pitch was heard in at least 20% of the graded time (you were only listening) is not filed.

- **Heatmap**: one square per reference note. Green = centred, blue = flat, orange = sharp (stronger colour = further out), purple "O" = octave slip, dark = not sung. A white outline marks a Perfect note. Hover for the details.
- **Tendency**: a plain sentence such as "Overall 12 cents flat. High notes 30 cents flat, low notes centred. Weakest: phrase 3 (54%)."
- **Comp plan**: for each phrase, the take that scored best on it (a phrase needs at least 80% of its time sung to be scored), and the projected accuracy of the comp. **Copy comp plan** puts it on the clipboard as text.

A phrase starts after 0.6 s of silence in the reference. A melody with no real gaps is cut into groups of 8 notes instead.

## Practising

- **Practise: Phrase N** grades only that phrase. The top bar shows the times to type into Cubase's cycle locators. The plugin cannot move Cubase's cycle itself, so you set it; **Copy cycle times** copies them.
- **Mode: Ghost** shows only the first note of each phrase as a cue. The other notes are hidden until they have been graded, so you sing from memory.
- Slowing the reference down is a Cubase job (change the project tempo or use the track's time-stretch). The plugin follows the transport, so it works at any speed.

## Settings worth knowing

- **Voice timing** shifts the wave and pitch line earlier or later to line up with the notes if your interface latency makes them sit off.
- **Reference octave shift** moves the target without touching your audio (use it if your voice is an octave from the Suno singer).
- **Voice wave size** and **Seconds on screen** are purely visual.
- The pitch method and voicing gates are the same as Vocal Assistor's.

## What it does not do

It does not change your audio, does not pitch-correct, does not set Cubase's cycle or tempo, and does not judge timing or lyrics, only pitch and octave. The reference must be a single melody line: harmonies in an audio stem will confuse the analysis, so use a lead-vocal stem or MIDI.

## Tests

`tests/AssistCoreTests.cpp` (grading, streaks, phrases, takes, comp plan, saving; plain C++) and `tests/AssistHostTests.cpp` (the real plugin in a headless host: bit-exact pass-through, a sung pass graded, takes filed at stop and at a loop jump, saved project restored, screens rendered) run on every build.
