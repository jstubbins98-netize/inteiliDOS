#ifndef TALK_H
#define TALK_H

/* InteiliTalk 2.0 — SAM-based PC-speaker text-to-speech for inteiliDOS.
 *
 * Synthesise and speak a null-terminated string via the PC speaker.
 * Called by the TALK <text> shell command and the DEMO command.
 * Blocks until the entire string has been spoken (or Escape is pressed).
 */
void talk_speak(const char *text);

#endif /* TALK_H */
