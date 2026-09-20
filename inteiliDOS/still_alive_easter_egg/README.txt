still_alive_easter_egg
======================
Easter egg triggered by typing "STILL ALIVE" at the IntelliShell prompt.

Plays Jonathan Coulton's "Still Alive" (Portal end-credits song, 2007)
through the PC speaker using the PIT timer for exact note timing.

stillalive.c     — player code; note table extracted from the MIDI above
stillalive.h     — public header (exports stillalive_play)

The MIDI is polyphonic (melody + bass/accompaniment on one track).
Because the PC speaker is monophonic, a highest-note-wins strategy is
used per time slice: at each MIDI event boundary the highest-pitched
simultaneously-sounding note is selected.  Duration: ~2 min 54 s.

"This was a triumph.  I'm making a note here: HUGE SUCCESS."
   — GLaDOS, Portal (2007)
