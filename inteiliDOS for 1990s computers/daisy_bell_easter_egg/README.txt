DAISY BELL EASTER EGG
=====================

Trigger: type  DAISY  at the IntelliShell prompt.

What it does
------------
Plays "Daisy Bell (Bicycle Built for Two)" through the PC speaker while
drawing an ASCII-art replica of the IBM 7094 operator's console in cyan
on a black 80x25 VGA screen.  The art characters are typed out one by one,
timed so the last character lands on the final note of the melody (~214 s).
Press any key to stop early.

Historical note
---------------
On 17 April 1961, an IBM 7094 mainframe at Bell Labs (Murray Hill, NJ)
became the first computer ever to sing.  The song it chose was Daisy Bell.
The arrangement was written by John L. Kelly and Carol Lockbaum using the
MUSIC II synthesis program by Max Mathews.

Four years later, Stanley Kubrick's 2001: A Space Odyssey immortalised the
moment when HAL 9000 slows down and sings the same song as it is shut off.

Implementation
--------------
daisy.c   — 653-note monophonic table extracted from a MIDI source;
            IBM 7094 ASCII art (22 rows); playback loop using speaker_on()
            and timer_sleep() from kernel/timer.h.
daisy.h   — exports daisy_play().
