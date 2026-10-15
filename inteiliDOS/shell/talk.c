/*
 * inteiliDOS -- shell/talk.c
 * InteiliTalk 2.0 — SAM text-to-speech via PC-speaker PWM.
 *
 * Pipeline:
 *   English text → reciter (TextToPhonemes)
 *               → phoneme parser (SAMMain)
 *               → formant synthesiser (Render, inside SAMMain)
 *               → sam_pcm_buf[]  (8-bit PCM, ~22050 Hz)
 *               → speaker_play_pcm()  (PIT ch2 PWM, IRQ0-driven)
 *
 * Public entry point: talk_speak(text)
 * Called by: TALK <text>  shell command, DEMO command.
 */

#include "talk.h"
#include "sam/sam_phoneme.h"
#include "sam/sam_render.h"
#include "sam/sam_reciter.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/timer.h"
#include <stdint.h>

/* ── talk_speak ──────────────────────────────────────────────────────────
 * Convert text to speech and play it through the PC speaker.
 * Returns immediately if text is empty or the Escape key is held.
 */
void talk_speak(const char *text) {
    if (!text || !text[0]) return;
    if (keyboard_poll() == KEY_ESCAPE) return;

    /* Status line */
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("[speaking] ");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_puts(text);
    vga_putchar('\n');
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* Step 1: copy text into SAM input buffer */
    sam_set_input(text);

    /* Step 2: English → SAM phoneme notation (reciter) */
    if (!TextToPhonemes(sam_input)) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("[talk] reciter failed\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    /* Step 3: phoneme notation → PCM in sam_pcm_buf (full SAM pipeline) */
    if (!SAMMain()) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("[talk] parse error\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    /* Step 4: play PCM through PC speaker */
    if (sam_pcm_len > 0)
        speaker_play_pcm(sam_pcm_buf, sam_pcm_len);
}
