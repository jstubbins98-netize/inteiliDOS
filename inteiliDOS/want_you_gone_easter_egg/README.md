# GLADOS — Want You Gone

Type **GLADOS** at the inteiliDOS shell prompt (case-insensitive).
This replaces the built-in Still Alive Easter egg and its old **ALIVE** command.
It does not remove LaunchPad's ability to run third-party DOS programs.

The entire 80×25 screen becomes solid amber, with black typewriter lyrics.
Only one stanza is shown at a time; each of the six supplied verse/chorus
sections replaces the previous one. The final stanza remains visible until
Enter or Esc. **Esc stops playback at any time.** Exiting stops the PC speaker,
restores the original VGA palette, cursor and shell screen, and leaves the OS
volume, timer and keyboard handling unchanged.

The melody now comes exclusively from the active `PLAY` strings in the supplied
QBasic program, bundled unchanged as `assets/melody.bas`. It plays for
**135.450 seconds**, preserving its tempo, octave changes, accidentals, dotted
notes, rests and normal/legato/staccato articulation. QBasic octave 3 starts at
middle C. No startup sequence, screen effects, alternate lyrics, credits,
SLEEP statements or typewriter routines from that program are implemented.
This is a PC-speaker instrumental rendition, not a vocal recording.

The previous MIDI is retained as a source reference but is no longer used.
The original lyric attachment is still `assets/lyrics.txt`; the existing display
text and positioning are preserved in `assets/stanzas.inc`, alongside the
phrase-aligned start and typing times.

Page changes are aligned with this arrangement's verse/chorus sections.
Characters are distributed across each section, leaving 2.5 seconds to read
the complete stanza before changing pages. The existing lyric text, hidden
headings, punctuation and repeated final lines remain unchanged. The
presentation still finishes at 136.806 seconds.

The cues below are elapsed times in the extracted PLAY stream, not wall-clock
times in the BASIC program. Verse cues use the opening A of the vocal phrase
after the intro/interlude's E pickup. Chorus cues use the opening B-flat octave
figure after the preceding F-sharp/C-sharp/B-flat turnaround. No BASIC calls,
screen commands or typewriter delays contribute to these times.

| Section | Start (seconds) | Typing (seconds) | Complete (seconds) |
| --- | ---: | ---: | ---: |
| Verse 1 | 5.400 | 17.300 | 22.700 |
| Chorus 1 | 25.200 | 19.400 | 44.600 |
| Verse 2 | 47.100 | 17.300 | 64.400 |
| Chorus 2 | 66.900 | 18.800 | 85.700 |
| Verse 3 | 88.200 | 17.300 | 105.500 |
| Final chorus | 108.000 | 26.306 | 134.306 |

The final page is complete during the ending rests, remains readable for
2.5 seconds before the return prompt, and stays visible until Enter or Esc.

The amber colour uses a temporarily remapped VGA DAC entry rather than the
blink bit, so the background stays amber instead of flashing.

## Build and verify

The regular OS CMake build includes this module. Embedded `data.h` is checked
in, so playback requires neither external files nor a runtime MIDI parser.
All runtime arithmetic is integer-only and the standard i386 kernel build
flags apply.

After changing the melody, regenerate:

```sh
python3 want_you_gone_easter_egg/tools/generate.py
```

Check asset freshness without editing:

```sh
python3 want_you_gone_easter_egg/tools/generate.py --check
```

Check QBasic pitch/timing and melody-only extraction:

```sh
python3 want_you_gone_easter_egg/tools/test_qbasic_music.py
```

Full OS/QEMU integration test (approximately 2.5 minutes):

```sh
python3 tests/run_glados_test.py
```
