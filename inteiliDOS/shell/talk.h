#ifndef TALK_H
#define TALK_H

/* InteiliTalk 1.0 — PC-speaker text-to-speech for inteiliDOS
 * Entry point called by the TALK shell command.
 */
void talk_run(void);

/* Synthesise and speak a null-terminated string via the PC speaker.
 * Can be called directly by other subsystems (e.g. DEMO command).
 * Blocks until the entire string has been spoken.
 */
void talk_speak(const char *text);

#endif /* TALK_H */
