#ifndef MSDOS_KEYBOARD_INPUT_H
#define MSDOS_KEYBOARD_INPUT_H

#include "../keyboard.h"

/* Private interpreter/ROM mailbox protocol:
 * low word = BIOS key (scan:ASCII), bits 16..21 = LCtrl/LShift/LAlt/
 * RCtrl/RShift/RAlt, bit 22 = CapsLock, bit 23 = state-only (no key),
 * bits 24..30 = physical set-1 make code, for guest INT 9h hooks.
 * The mailbox at F000:2400 is outside the original ROM/RTC scratch/stack.
 */
#define MSDOS_KEYBOARD_MAILBOX 0x2400
#define MSDOS_KEY_STATE_ONLY  0x00800000
#define MSDOS_KEY_STATE_MASK  0x007f0000

static inline unsigned msdos_keyboard_state(const keyboard_event_t *e) {
    unsigned mods=(e->modifiers & 7u) | ((e->modifiers >> 1) & 0x38u);
    return (mods << 16) | ((unsigned)!!e->caps_lock << 22);
}

static inline int msdos_keyboard_encode(const keyboard_event_t *e) {
    static const uint8_t letters[26] = {
        0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
        0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c
    };
    unsigned state = msdos_keyboard_state(e), scan = e->scancode;
    unsigned physical_scan = scan;
    unsigned c = e->character;
    int ctrl = e->modifiers & KEY_MOD_CTRL, alt = e->modifiers & KEY_MOD_ALT;
    int shift = e->modifiers & KEY_MOD_SHIFT;
    if (!c && !scan) return (int)(state | MSDOS_KEY_STATE_ONLY);

    /* Function keys use Ctrl > Alt > Shift BIOS precedence. */
    if (c >= KEY_F1 && c <= KEY_F12) {
        unsigned f = c - KEY_F1;
        if (!physical_scan) physical_scan = f < 10 ? 0x3b+f : 0x57+f-10;
        scan = f < 10 ? (ctrl ? 0x5e : alt ? 0x68 : shift ? 0x54 : 0x3b) + f :
                       (ctrl ? 0x89 : alt ? 0x8b : shift ? 0x87 : 0x85) + f - 10;
        c = 0;
    } else if (c >= KEY_UP && c <= KEY_RIGHT) {
        static const uint8_t normal[] = {0x48,0x50,0x4b,0x4d};
        static const uint8_t control[] = {0x8d,0x91,0x73,0x74};
        if (!physical_scan) physical_scan = normal[c-KEY_UP];
        scan = ctrl ? control[c-KEY_UP] : normal[c-KEY_UP] + (alt ? 0x50 : 0);
        c = 0;
    } else if (scan == 0x0f) {
        if (ctrl || alt || shift) {
            scan = ctrl ? 0x94 : alt ? 0xa5 : 0x0f;
            c = 0;
        }
    } else if (alt) {
        c = 0;
        if (scan >= 2 && scan <= 0x0d) scan += 0x76; /* Alt+1..= */
        else if (scan == 0x1c) scan = 0xa6;
    } else if (ctrl) {
        /* Translate physical positions, not an already shifted/USB Ctrl byte.
         * Ctrl+H/I/M must not lose their letter scan to Backspace/Tab/Enter. */
        for (unsigned i=0;i<26;i++) if (scan == letters[i]) { c=i+1; break; }
        if (scan == 0x1a) c=0x1b;
        if (scan == 0x2b) c=0x1c;
        if (scan == 0x1b) c=0x1d;
        if (scan == 7) c=0x1e;       /* Ctrl+6 */
        if (scan == 0x0c) c=0x1f;   /* Ctrl+- */
        if (scan == 3 || scan == 0x39) c=0; /* Ctrl+2/Space */
        if (scan == 0x0e) c=0x7f;
        if (scan == 0x1c) c=0x0a;
    }
    /* USB's legacy Ctrl translation differs from PS/2, but for ordinary
     * letters both DOS paths use the same CapsLock XOR Shift rule. */
    if (!ctrl && !alt) {
        for (unsigned i=0;i<26;i++)
            if (scan == letters[i]) c = (!!shift != !!e->caps_lock ? 'A' : 'a') + i;
    }
    return (int)(state | (physical_scan << 24) | (scan << 8) | c);
}

#endif
