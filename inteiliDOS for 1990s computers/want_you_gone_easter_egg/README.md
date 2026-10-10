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

The original attachments are bundled as `assets/want_you_gone.mid` and
`assets/lyrics.txt`. The MIDI plays for **136.806 seconds**, preserving its
tempo events, note timing and rests. The PC speaker is monophonic: track 9,
channel 8 is the lead voice; accompaniment fills the gaps. This is a note-based
instrumental rendition, not a recording of the sung vocals.

Page changes are aligned with this arrangement's verse/chorus sections.
Characters are distributed across each section, leaving 2.5 seconds to read
the complete stanza before changing pages. The text is unchanged from the
attachment, including headings, punctuation and repeated final lines.

The amber colour uses a temporarily remapped VGA DAC entry rather than the
blink bit, so the background stays amber instead of flashing.

## Build and verify

The regular OS CMake build includes this module. Embedded `data.h` is checked
in, so playback requires neither external files nor a runtime MIDI parser.
All runtime arithmetic is integer-only and the standard i386 kernel build
flags apply.

After changing either asset, regenerate:

```sh
python3 want_you_gone_easter_egg/tools/generate.py
```

Check asset freshness without editing:

```sh
python3 want_you_gone_easter_egg/tools/generate.py --check
```

Full OS/QEMU integration test (approximately 2.5 minutes):

```sh
python3 tests/run_glados_test.py
```
