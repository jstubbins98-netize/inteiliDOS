/*
 * inteilidOS -- kernel/keyboard.c
 * PS/2 keyboard driver (US QWERTY scancode set 1)
 */

#include "keyboard.h"
#include "isr.h"
#include <stdint.h>

#define KB_DATA 0x60
#define KB_STATUS 0x64
#define KB_BUF_SIZE 256

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ---- Circular key buffer ---- */
static volatile keyboard_event_t kb_buf[KB_BUF_SIZE];
static volatile int kb_head = 0, kb_tail = 0;
static uint8_t ps2_modifiers, usb_modifiers, ps2_caps, usb_caps;

void keyboard_current_event(keyboard_event_t *event) {
    *event = (keyboard_event_t){0, 0, ps2_modifiers | usb_modifiers,
                               ps2_caps | usb_caps};
}

void keyboard_inject_event(uint8_t c, uint8_t scan, uint8_t mods, uint8_t caps) {
    int next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = (keyboard_event_t){c, scan, mods, caps};
        kb_head = next;
    }
}

static void kb_buf_push(uint8_t c, uint8_t scan) {
    keyboard_inject_event(c, scan, ps2_modifiers | usb_modifiers, ps2_caps | usb_caps);
}

void keyboard_usb_modifiers(uint8_t mods, uint8_t caps) {
    if (mods == usb_modifiers && caps == usb_caps) return;
    usb_modifiers = mods;
    usb_caps = caps;
    kb_buf_push(0, 0);
}

int keyboard_poll_event(keyboard_event_t *event) {
    if (kb_head == kb_tail) return 0;
    *event = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return 1;
}

static int kb_buf_pop(void) {
    keyboard_event_t event;
    while (keyboard_poll_event(&event))
        if (event.character) return event.character;
    return -1;
}

/* ---- US QWERTY scancode map ---- */
static const char sc_normal[128] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,  /* F1-F10 */
    0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,
    0,0                    /* F11, F12 */
};
static const char sc_shift[128] = {
    0, 0, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?',0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
};

/* Kept separate from port I/O so the set-1 decoder can be regression tested. */
static void keyboard_scancode(uint8_t sc) {
    static int extended, pause_bytes;
    if (pause_bytes) { --pause_bytes; return; }
    if (sc == 0xe1) { pause_bytes = 5; extended = 0; return; }
    if (sc == 0xe0) { extended = 1; return; }
    int ext = extended, released = sc & 0x80;
    extended = 0;
    sc &= 0x7f;
    uint8_t modifier = 0;
    /* E0 2A/36 are PrintScreen's fake shifts, not physical Shift keys. */
    if (!ext && sc == 0x2a) modifier = KEY_MOD_LSHIFT;
    if (!ext && sc == 0x36) modifier = KEY_MOD_RSHIFT;
    if (sc == 0x1d) modifier = ext ? KEY_MOD_RCTRL : KEY_MOD_LCTRL;
    if (sc == 0x38) modifier = ext ? KEY_MOD_RALT : KEY_MOD_LALT;
    if (modifier) {
        uint8_t old = ps2_modifiers;
        if (released) ps2_modifiers &= ~modifier;
        else ps2_modifiers |= modifier;
        if (old != ps2_modifiers) kb_buf_push(0, 0);
        return;
    }
    static int caps_down;
    if (!ext && sc == 0x3a) {
        if (!released && !caps_down) { ps2_caps ^= 1; kb_buf_push(0, 0); }
        caps_down = !released;
        return;
    }
    if (released || (ext && (sc == 0x2a || sc == 0x36))) return;

    /* Special keys that need explicit mapping before the table lookup */
    if (sc == 0x01) { kb_buf_push(KEY_ESCAPE, sc); return; }
    if (sc == 0x1C) { kb_buf_push(KEY_ENTER, sc);  return; }

    if (sc == 0x48) { kb_buf_push(KEY_UP, sc);    return; }
    if (sc == 0x50) { kb_buf_push(KEY_DOWN, sc);  return; }
    if (sc == 0x4B) { kb_buf_push(KEY_LEFT, sc);  return; }
    if (sc == 0x4D) { kb_buf_push(KEY_RIGHT, sc); return; }

    /* F-keys */
    if (sc >= 0x3B && sc <= 0x44) {
        kb_buf_push((uint8_t)(KEY_F1 + (sc - 0x3B)), sc);
        return;
    }
    if (sc == 0x57) { kb_buf_push(KEY_F11, sc); return; }
    if (sc == 0x58) { kb_buf_push(KEY_F12, sc); return; }

    if (sc >= 128) return;

    int shift_held = (ps2_modifiers & KEY_MOD_SHIFT) != 0;
    int use_shift = shift_held;
    char c = use_shift ? sc_shift[sc] : sc_normal[sc];

    if (ps2_caps && c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (ps2_caps && c >= 'A' && c <= 'Z' && !shift_held) c = (char)(c + 32);

    if (c) kb_buf_push((uint8_t)c, sc);
}

static void keyboard_handler(registers_t *regs) {
    (void)regs;
    keyboard_scancode(inb(KB_DATA));
}

void keyboard_init(void) {
    isr_register_handler(33, keyboard_handler);
}

/* Called by the USB HID driver to push a character through the same buffer
 * as PS/2 keystrokes, making keyboard_getchar() and keyboard_poll()
 * transparent to whichever source produced the key.                     */
void keyboard_inject(uint8_t c) {
    kb_buf_push(c, 0);
}

int keyboard_getchar(void) {
    int c;
    while ((c = kb_buf_pop()) == -1)
        __asm__ volatile ("hlt");
    return c;
}

int keyboard_poll(void) {
    return kb_buf_pop();
}
