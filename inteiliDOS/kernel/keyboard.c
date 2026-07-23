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
static volatile uint8_t kb_buf[KB_BUF_SIZE];
static volatile int kb_head = 0, kb_tail = 0;

static void kb_buf_push(uint8_t c) {
    int next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) { kb_buf[kb_head] = c; kb_head = next; }
}
static int kb_buf_pop(void) {
    if (kb_head == kb_tail) return -1;
    uint8_t c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return (int)c;
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

static int shift_held = 0;
static int caps_lock  = 0;

static void keyboard_handler(registers_t *regs) {
    (void)regs;
    uint8_t sc = inb(KB_DATA);

    if (sc & 0x80) {
        /* Key release */
        sc &= 0x7F;
        if (sc == 0x2A || sc == 0x36) shift_held = 0;
        return;
    }

    /* Key press */
    if (sc == 0x2A || sc == 0x36) { shift_held = 1; return; }
    if (sc == 0x3A) { caps_lock ^= 1; return; }

    /* Arrow keys (extended — simple approach without E0 prefix handling) */
    if (sc == 0x48) { kb_buf_push(KEY_UP);    return; }
    if (sc == 0x50) { kb_buf_push(KEY_DOWN);  return; }
    if (sc == 0x4B) { kb_buf_push(KEY_LEFT);  return; }
    if (sc == 0x4D) { kb_buf_push(KEY_RIGHT); return; }

    /* F-keys */
    if (sc >= 0x3B && sc <= 0x44) {
        kb_buf_push((uint8_t)(KEY_F1 + (sc - 0x3B)));
        return;
    }
    if (sc == 0x42) { kb_buf_push(KEY_F8); return; }

    if (sc >= 128) return;

    int use_shift = shift_held;
    char c = use_shift ? sc_shift[sc] : sc_normal[sc];

    if (caps_lock && c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (caps_lock && c >= 'A' && c <= 'Z' && !shift_held) c = (char)(c + 32);

    if (c) kb_buf_push((uint8_t)c);
}

void keyboard_init(void) {
    isr_register_handler(33, keyboard_handler);
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
