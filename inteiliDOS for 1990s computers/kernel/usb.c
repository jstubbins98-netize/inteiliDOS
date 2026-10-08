/*
 * inteilidOS -- kernel/usb.c
 * USB HID keyboard driver
 *
 * Supports UHCI (USB 1.1) host controllers only, which covers:
 *   - All QEMU emulated USB keyboards  (run QEMU with: -usb -device usb-kbd)
 *   - Real hardware from roughly 1996-2010 (PIIX3/PIIX4 south bridges)
 *
 * The driver:
 *   1. Scans PCI for a UHCI controller  (class 0x0C / sub 0x03 / pi 0x00)
 *   2. Resets the controller and attached port
 *   3. Enumerates the device via synchronous control transfers
 *   4. Selects USB HID boot protocol (8-byte reports, no descriptor parsing)
 *   5. Polls the interrupt endpoint every 8 ms via the timer secondary hook
 *   6. Translates HID usage IDs → ASCII / special-key codes and injects them
 *      into the kernel keyboard buffer via keyboard_inject()
 *
 * If anything fails (no UHCI found, no device, stall during enumeration) the
 * function returns silently.  PS/2 keyboard operation is unaffected.
 */

#include "usb.h"
#include "pci.h"
#include "keyboard.h"
#include "timer.h"
#include "vga.h"
#include <stdint.h>

/* =========================================================================
 * Port I/O helpers
 * ========================================================================= */
static inline uint8_t  inb (uint16_t p) { uint8_t  v; __asm__ volatile("inb  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void     outb(uint16_t p, uint8_t  v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint16_t inw (uint16_t p) { uint16_t v; __asm__ volatile("inw  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void     outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline uint32_t inl (uint16_t p) { uint32_t v; __asm__ volatile("inl  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void     outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline void     io_wait(void) { outb(0x80, 0); }

/* =========================================================================
 * UHCI register offsets (from I/O base in BAR4)
 * ========================================================================= */
#define UHCI_USBCMD   0x00u   /* 16-bit */
#define UHCI_USBSTS   0x02u   /* 16-bit */
#define UHCI_USBINTR  0x04u   /* 16-bit */
#define UHCI_FRNUM    0x06u   /* 16-bit */
#define UHCI_FLBASE   0x08u   /* 32-bit — frame list base physical address */
#define UHCI_SOF      0x0Cu   /*  8-bit */
#define UHCI_PORTSC1  0x10u   /* 16-bit */
#define UHCI_PORTSC2  0x12u   /* 16-bit */

/* USBCMD bits */
#define UHCI_CMD_RS      0x0001u   /* Run/Stop */
#define UHCI_CMD_HCRESET 0x0002u   /* Host Controller Reset (self-clearing) */
#define UHCI_CMD_GRESET  0x0004u   /* Global Reset */
#define UHCI_CMD_MAXP    0x0080u   /* Max Packet — 0=32 bytes, 1=64 bytes */

/* USBSTS bits */
#define UHCI_STS_HCH     0x0020u   /* HC Halted */

/* PORTSC bits */
#define UHCI_PORT_CCS    0x0001u   /* Current Connect Status */
#define UHCI_PORT_CSC    0x0002u   /* Connect Status Change  (RW1C) */
#define UHCI_PORT_EN     0x0004u   /* Port Enabled */
#define UHCI_PORT_ENC    0x0008u   /* Port Enable Change     (RW1C) */
#define UHCI_PORT_LSDA   0x0100u   /* Low Speed Device Attached */
#define UHCI_PORT_RESET  0x0200u   /* Port Reset */

/* =========================================================================
 * UHCI data structures
 * ========================================================================= */

/* Transfer Descriptor — 16 bytes, 16-byte aligned */
typedef struct {
    volatile uint32_t link;    /* next TD/QH link pointer           */
    volatile uint32_t status;  /* control / status                  */
    volatile uint32_t token;   /* PID | ADDR | ENDP | TOGGLE | LEN  */
    volatile uint32_t buffer;  /* data buffer physical address       */
} __attribute__((packed, aligned(16))) uhci_td_t;

/* Queue Head — 8 bytes of data, but MUST be 16-byte aligned.
 * The UHCI link pointer format stores the address in bits 31:4 and uses
 * bits 3:0 for flags (T, Q, Vf).  The hardware reconstructs the address
 * as (pointer & 0xFFFFFFF0), so any structure used as a link target must
 * sit on a 16-byte boundary — even though the struct itself is only 8 bytes.
 * Using aligned(8) would let a QH land at e.g. 0x102008, which the
 * controller decodes as 0x102000: wrong address, broken transfers.       */
typedef struct {
    volatile uint32_t link;    /* horizontal link (next QH/TD)      */
    volatile uint32_t element; /* vertical  link (first TD)         */
} __attribute__((packed, aligned(16))) uhci_qh_t;

/* Link pointer flags (used in frame list entries, QH links, TD links) */
#define LP_T   0x1u  /* Terminate — no more descriptors       */
#define LP_Q   0x2u  /* Type: 0 = TD, 1 = QH                 */
#define LP_Vf  0x4u  /* Depth-first vs breadth-first for TDs */

/* TD Control/Status field bits */
#define TD_ACTIVE  (1u << 23)           /* set = hardware owns this TD     */
#define TD_LSPD    (1u << 26)           /* low-speed device                */
#define TD_CERR(n) ((uint32_t)(n) << 27)/* error retry count (0-3)         */
#define TD_SPD     (1u << 29)           /* short-packet detect             */
#define TD_STALLED (1u << 22)
#define TD_DBERR   (1u << 21)
#define TD_BABBLE  (1u << 20)
#define TD_CRCERR  (1u << 18)
#define TD_BSERR   (1u << 17)
#define TD_ERR_MASK (TD_STALLED|TD_DBERR|TD_BABBLE|TD_CRCERR|TD_BSERR)

/* Convenient status value for an armed TD (3 retries, active) */
#define TD_STATUS_ACTIVE(lowspeed) \
    (TD_ACTIVE | TD_CERR(3) | ((lowspeed) ? TD_LSPD : 0u))

/* TD Token PIDs */
#define PID_SETUP 0x2Du
#define PID_IN    0x69u
#define PID_OUT   0xE1u

/* Build token field:
 *   maxlen = (payload - 1) & 0x7FF;  use 0x7FF for zero-byte status phase
 *   toggle: 0=DATA0, 1=DATA1
 */
static inline uint32_t make_token(uint8_t pid, uint8_t addr, uint8_t endp,
                                   uint8_t toggle, uint16_t maxlen) {
    return (uint32_t)pid
         | ((uint32_t)addr   <<  8)
         | ((uint32_t)endp   << 15)
         | ((uint32_t)toggle << 19)
         | ((uint32_t)maxlen << 21);
}

/* =========================================================================
 * Static allocations — all in .bss, virtual == physical (no paging)
 * ========================================================================= */

/* Frame list: 1024 × 4 bytes, must be 4 KB-aligned */
static uint32_t  uhci_frame_list[1024] __attribute__((aligned(4096)));

/* Two queue heads: one for synchronous control transfers, one for the
 * live interrupt endpoint after enumeration is complete.                */
static uhci_qh_t uhci_ctrl_qh  __attribute__((aligned(8)));
static uhci_qh_t uhci_intr_qh  __attribute__((aligned(8)));

/* TD pool:
 *   [0 .. CTRL_TD_MAX-1]  — reused for each synchronous control transfer
 *   [CTRL_TD_MAX]         — the single persistent interrupt-endpoint TD   */
#define CTRL_TD_MAX 20
static uhci_td_t uhci_tds[CTRL_TD_MAX + 1] __attribute__((aligned(16)));
#define INTR_TD_IDX CTRL_TD_MAX

/* Buffers for control transfers */
static uint8_t uhci_setup_buf[8];   /* USB SETUP packet (8 bytes)        */
static uint8_t uhci_data_buf[128];  /* descriptor / response data        */

/* HID report double-buffer */
static volatile uint8_t uhci_kbd_report[8]; /* filled by UHCI DMA        */
static          uint8_t uhci_kbd_prev[8];   /* previous report snapshot  */

/* =========================================================================
 * Driver state
 * ========================================================================= */
static uint16_t uhci_base      = 0;  /* I/O base address from BAR4       */
static uint8_t  usb_kbd_addr   = 0;  /* USB device address (1 after enum)*/
static uint8_t  usb_kbd_endp   = 1;  /* interrupt IN endpoint number     */
static uint8_t  usb_kbd_toggle = 0;  /* DATA0/DATA1 toggle for intr TD   */
static int      usb_kbd_ls     = 0;  /* 1 = low-speed device             */
static volatile int usb_ready  = 0;  /* 1 = interrupt polling is active  */

/* =========================================================================
 * Low-level UHCI helpers
 * ========================================================================= */

/* Fill all 1024 frame-list entries with the given link pointer value.    */
static void uhci_fill_frames(uint32_t lp) {
    uint32_t i;
    for (i = 0; i < 1024; i++)
        uhci_frame_list[i] = lp;
}

/* Point all frame-list entries at the interrupt QH (every 1 ms).
 * Called after enumeration to start live polling.                        */
static void uhci_arm_intr_frames(void) {
    uint32_t qh_phys = (uint32_t)(uint32_t *)&uhci_intr_qh;
    uhci_fill_frames(qh_phys | LP_Q);   /* point to intr QH */
}

/* Wait for the last TD in a control transfer chain to complete.
 * Returns 0 on success, -1 on error/timeout.
 * last_td: index in uhci_tds[] of the STATUS-phase TD.                  */
static int uhci_wait_ctrl(int last_td) {
    uint32_t deadline = timer_get_ticks() + 200u;  /* 200 ms timeout */
    while (uhci_tds[last_td].status & TD_ACTIVE) {
        if (timer_get_ticks() >= deadline)
            return -1;  /* timeout */
        io_wait();
    }
    /* Check for hardware-reported errors */
    if (uhci_tds[last_td].status & TD_ERR_MASK)
        return -1;
    return 0;
}

/* =========================================================================
 * Synchronous control transfer
 *
 * Executes a complete USB control transfer (SETUP + optional DATA + STATUS)
 * by occupying all frame-list entries for the duration, then spinning until
 * completion.
 *
 * setup:    8-byte USB SETUP packet
 * data:     data buffer (NULL if no data phase)
 * data_len: number of bytes to transfer (0 if no data phase)
 * dir_in:   1 = DATA IN (device→host GET), 0 = no data (SET commands)
 * addr:     USB device address (0 before SET_ADDRESS, 1 after)
 * endp:     endpoint number (always 0 for control pipe)
 * ls:       1 if low-speed device
 *
 * Returns 0 on success, -1 on error.
 * ========================================================================= */
static int uhci_control(uint8_t addr, uint8_t endp, int ls,
                        const uint8_t *setup,
                        uint8_t *data, uint16_t data_len, int dir_in) {
    int td = 0;

    /* --- SETUP phase (always DATA0) --- */
    uhci_setup_buf[0] = setup[0]; uhci_setup_buf[1] = setup[1];
    uhci_setup_buf[2] = setup[2]; uhci_setup_buf[3] = setup[3];
    uhci_setup_buf[4] = setup[4]; uhci_setup_buf[5] = setup[5];
    uhci_setup_buf[6] = setup[6]; uhci_setup_buf[7] = setup[7];

    uhci_tds[td].buffer = (uint32_t)uhci_setup_buf;
    uhci_tds[td].token  = make_token(PID_SETUP, addr, endp, 0, 7); /* DATA0, 8 bytes */
    uhci_tds[td].status = TD_STATUS_ACTIVE(ls);
    td++;

    /* --- DATA phase (starts DATA1, toggles each packet) --- */
    if (data_len > 0 && data != (void *)0) {
        uint8_t  toggle   = 1;          /* first data packet = DATA1 */
        uint16_t remain   = data_len;
        uint32_t buf_phys = (uint32_t)data;
        uint8_t  pid      = dir_in ? PID_IN : PID_OUT;

        while (remain > 0 && td < CTRL_TD_MAX - 1) {
            uint16_t pkt = (remain > 8u) ? 8u : remain;
            uhci_tds[td].buffer = buf_phys;
            uhci_tds[td].token  = make_token(pid, addr, endp, toggle,
                                              (uint16_t)(pkt - 1u));
            uhci_tds[td].status = TD_STATUS_ACTIVE(ls);
            buf_phys += pkt;
            remain   -= pkt;
            toggle   ^= 1;
            td++;
        }
    }

    /* --- STATUS phase (opposite direction, DATA1, 0 bytes = token 0x7FF) --- */
    {
        uint8_t status_pid = (data_len > 0 && dir_in) ? PID_OUT : PID_IN;
        uhci_tds[td].buffer = 0;
        uhci_tds[td].token  = make_token(status_pid, addr, endp, 1, 0x7FFu);
        uhci_tds[td].status = TD_STATUS_ACTIVE(ls);
    }
    int last = td;

    /* Chain the TDs: each points to the next (breadth-first), last terminates */
    int i;
    for (i = 0; i < last; i++)
        uhci_tds[i].link = (uint32_t)&uhci_tds[i + 1];  /* next TD, Q=0 T=0 */
    uhci_tds[last].link = LP_T;  /* terminate */

    /* Aim the control QH at the first TD */
    uhci_ctrl_qh.link    = LP_T;                       /* no next QH */
    uhci_ctrl_qh.element = (uint32_t)&uhci_tds[0];    /* first TD   */

    /* Occupy all frame-list entries with the control QH */
    uhci_fill_frames((uint32_t)&uhci_ctrl_qh | LP_Q);

    /* Spin until the STATUS TD completes */
    int rc = uhci_wait_ctrl(last);

    /* Restore frame list to terminate (cleared; intr polling not started yet) */
    uhci_fill_frames(LP_T);
    uhci_ctrl_qh.element = LP_T;

    return rc;
}

/* =========================================================================
 * USB SETUP packet builder helpers
 * ========================================================================= */
static void make_setup(uint8_t *pkt, uint8_t bmRequestType, uint8_t bRequest,
                       uint16_t wValue, uint16_t wIndex, uint16_t wLength) {
    pkt[0] = bmRequestType;
    pkt[1] = bRequest;
    pkt[2] = (uint8_t)(wValue & 0xFF);
    pkt[3] = (uint8_t)(wValue >> 8);
    pkt[4] = (uint8_t)(wIndex & 0xFF);
    pkt[5] = (uint8_t)(wIndex >> 8);
    pkt[6] = (uint8_t)(wLength & 0xFF);
    pkt[7] = (uint8_t)(wLength >> 8);
}

/* =========================================================================
 * HID boot-protocol keyboard report translation
 *
 * USB HID boot protocol keyboard reports are exactly 8 bytes:
 *   byte 0: modifier bitmap  (bits: LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI)
 *   byte 1: reserved (always 0)
 *   bytes 2-7: up to 6 simultaneous keycodes (HID usage page 0x07)
 * ========================================================================= */

/* Normal (unshifted) map indexed by HID usage ID */
static const char hid_normal[0x53] = {
    /*0x00*/ 0,  0,  0,  0,                 /* 00-03: reserved / errors    */
    /*0x04*/ 'a','b','c','d','e','f','g',   /* 04-0A: a-g                  */
    /*0x0B*/ 'h','i','j','k','l','m','n',   /* 0B-11: h-n                  */
    /*0x12*/ 'o','p','q','r','s','t','u',   /* 12-18: o-u                  */
    /*0x19*/ 'v','w','x','y','z',           /* 19-1D: v-z                  */
    /*0x1E*/ '1','2','3','4','5',           /* 1E-22: 1-5                  */
    /*0x23*/ '6','7','8','9','0',           /* 23-27: 6-0                  */
    /*0x28*/ '\r',                          /* 28: Enter                   */
    /*0x29*/ '\x1B',                        /* 29: Escape                  */
    /*0x2A*/ '\b',                          /* 2A: Backspace               */
    /*0x2B*/ '\t',                          /* 2B: Tab                     */
    /*0x2C*/ ' ',                           /* 2C: Space                   */
    /*0x2D*/ '-',                           /* 2D: Minus                   */
    /*0x2E*/ '=',                           /* 2E: Equals                  */
    /*0x2F*/ '[',                           /* 2F: Left Bracket            */
    /*0x30*/ ']',                           /* 30: Right Bracket           */
    /*0x31*/ '\\',                          /* 31: Backslash               */
    /*0x32*/ 0,                             /* 32: Non-US #                */
    /*0x33*/ ';',                           /* 33: Semicolon               */
    /*0x34*/ '\'',                          /* 34: Apostrophe              */
    /*0x35*/ '`',                           /* 35: Grave                   */
    /*0x36*/ ',',                           /* 36: Comma                   */
    /*0x37*/ '.',                           /* 37: Period                  */
    /*0x38*/ '/',                           /* 38: Slash                   */
    /*0x39*/ 0,                             /* 39: Caps Lock (handled sep.)*/
    /*0x3A*/ '\x90',                        /* 3A: F1  = KEY_F1            */
    /*0x3B*/ '\x91',                        /* 3B: F2                      */
    /*0x3C*/ '\x92',                        /* 3C: F3                      */
    /*0x3D*/ '\x93',                        /* 3D: F4                      */
    /*0x3E*/ '\x94',                        /* 3E: F5                      */
    /*0x3F*/ '\x95',                        /* 3F: F6                      */
    /*0x40*/ '\x96',                        /* 40: F7                      */
    /*0x41*/ '\x97',                        /* 41: F8 = KEY_F8             */
    /*0x42*/ 0,                             /* 42: F9  (not mapped)        */
    /*0x43*/ 0,                             /* 43: F10                     */
    /*0x44*/ 0,                             /* 44: F11                     */
    /*0x45*/ 0,                             /* 45: F12                     */
    /*0x46*/ 0,                             /* 46: Print Screen            */
    /*0x47*/ 0,                             /* 47: Scroll Lock             */
    /*0x48*/ 0,                             /* 48: Pause                   */
    /*0x49*/ 0,                             /* 49: Insert                  */
    /*0x4A*/ 0,                             /* 4A: Home                    */
    /*0x4B*/ 0,                             /* 4B: Page Up                 */
    /*0x4C*/ '\b',                          /* 4C: Delete (→ backspace)    */
    /*0x4D*/ 0,                             /* 4D: End                     */
    /*0x4E*/ 0,                             /* 4E: Page Down               */
    /*0x4F*/ '\x83',                        /* 4F: Right Arrow = KEY_RIGHT */
    /*0x50*/ '\x82',                        /* 50: Left Arrow  = KEY_LEFT  */
    /*0x51*/ '\x81',                        /* 51: Down Arrow  = KEY_DOWN  */
    /*0x52*/ '\x80',                        /* 52: Up Arrow    = KEY_UP    */
};

/* Shifted map — only entries that differ from hid_normal */
static const char hid_shift[0x53] = {
    0, 0, 0, 0,
    'A','B','C','D','E','F','G',
    'H','I','J','K','L','M','N',
    'O','P','Q','R','S','T','U',
    'V','W','X','Y','Z',
    '!','@','#','$','%',
    '^','&','*','(',')',
    '\r','\x1B','\b','\t',' ',
    '_','+','{','}','|',
    0,
    ':','"','~','<','>','?',
    0,
    '\x90','\x91','\x92','\x93','\x94','\x95','\x96','\x97',
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,
    '\x83','\x82','\x81','\x80',
};

/* State for caps lock (toggled in process_report) */
static int usb_caps = 0;

static void process_report(const uint8_t *cur, const uint8_t *prev) {
    uint8_t mod = cur[0];
    int shifted = (mod & 0x22u) != 0;  /* L or R Shift */

    /* Check for Caps Lock toggle on byte 2-7 */
    uint8_t ki;
    for (ki = 2; ki < 8; ki++) {
        uint8_t kc = cur[ki];
        if (kc == 0) continue;
        if (kc == 0x39) {  /* Caps Lock usage ID */
            /* Only fire on press (not held) */
            int was_held = 0;
            uint8_t pi;
            for (pi = 2; pi < 8; pi++)
                if (prev[pi] == 0x39) { was_held = 1; break; }
            if (!was_held) usb_caps ^= 1;
            continue;
        }

        /* Only inject newly-pressed keys (not held from previous report) */
        int was_held = 0;
        uint8_t pi;
        for (pi = 2; pi < 8; pi++)
            if (prev[pi] == kc) { was_held = 1; break; }
        if (was_held) continue;

        /* Translate HID usage ID → character */
        if (kc >= 0x53u) continue;  /* out of our table range */

        char c = shifted ? hid_shift[kc] : hid_normal[kc];

        /* Apply Caps Lock to alphabetic keys only */
        if (usb_caps && c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (usb_caps && c >= 'A' && c <= 'Z' && !shifted) c = (char)(c + 32);

        if (c) keyboard_inject((uint8_t)c);
    }
}

/* =========================================================================
 * Timer callback — checks for completed interrupt TD every 8 ms
 * ========================================================================= */
static void usb_timer_tick(void) {
    if (!usb_ready) return;

    /* Check only on every 8th millisecond */
    if ((timer_get_ticks() & 7u) != 0u) return;

    uhci_td_t *td = &uhci_tds[INTR_TD_IDX];

    /* Not yet completed → still waiting for keypress data */
    if (td->status & TD_ACTIVE) return;

    /* Error? Re-arm and continue */
    if (td->status & TD_ERR_MASK) {
        td->status = TD_STATUS_ACTIVE(usb_kbd_ls);
        uhci_intr_qh.element = (uint32_t)td;
        return;
    }

    /* A report arrived — snapshot it (volatile prevents tear) */
    uint8_t snap[8];
    uint8_t i;
    for (i = 0; i < 8; i++)
        snap[i] = (uint8_t)uhci_kbd_report[i];

    /* Translate and inject */
    process_report(snap, uhci_kbd_prev);

    /* Save as previous */
    for (i = 0; i < 8; i++)
        uhci_kbd_prev[i] = snap[i];

    /* Flip data toggle and re-arm the interrupt TD */
    usb_kbd_toggle ^= 1u;
    td->token  = make_token(PID_IN, usb_kbd_addr, usb_kbd_endp,
                             usb_kbd_toggle, 7u);   /* MaxLen = 7 → 8 bytes */
    td->status = TD_STATUS_ACTIVE(usb_kbd_ls);

    /* Re-link QH element to this TD */
    uhci_intr_qh.element = (uint32_t)td;
}

/* =========================================================================
 * Enumeration helpers
 * ========================================================================= */

/* Parse a raw configuration-descriptor blob for a HID interrupt-IN endpoint.
 * Sets usb_kbd_endp to the endpoint number (1-15) on success.
 * Returns 0 if found, -1 if not found.                                   */
static int parse_config_descriptor(const uint8_t *buf, uint16_t total_len) {
    uint16_t off = 0;
    while (off + 2u <= total_len) {
        uint8_t bLen  = buf[off];
        uint8_t bType = buf[off + 1];
        if (bLen == 0) break;

        if (bType == 0x05u && bLen >= 7u) {  /* Endpoint Descriptor */
            uint8_t addr  = buf[off + 2];
            uint8_t attrs = buf[off + 3];
            /* We want: direction=IN (bit7=1), transfer type=interrupt (bits1:0=3) */
            if ((addr & 0x80u) && (attrs & 0x03u) == 0x03u) {
                usb_kbd_endp = addr & 0x0Fu;  /* endpoint number */
                return 0;
            }
        }
        off = (uint16_t)(off + bLen);
    }
    return -1;
}

/* =========================================================================
 * Main initialisation
 * ========================================================================= */

void usb_keyboard_init(void) {
    /* ---- 1. Find UHCI controller via PCI ---- */
    pci_device_t uhci_pci;
    /* UHCI: class=0x0C (Serial Bus), subclass=0x03 (USB), prog_if=0x00 (UHCI) */
    if (pci_find_device(0x0Cu, 0x03u, 0x00u, &uhci_pci) != 0) {
        return;  /* no UHCI controller — PS/2 only */
    }

    /* Enable bus mastering so the controller can DMA */
    pci_enable_busmaster(&uhci_pci);

    /* BAR4 is the I/O base for UHCI registers.
     * Bit 0 = 1 (I/O space indicator); mask it off with 0xFFFC.          */
    uhci_base = (uint16_t)(uhci_pci.bar[4] & 0xFFFCu);
    if (uhci_base == 0) return;  /* malformed BAR */

    /* ---- 2. Reset the host controller ---- */
    /* Stop the controller first */
    outw((uint16_t)(uhci_base + UHCI_USBCMD), 0x0000u);
    timer_sleep(5);

    /* Assert global reset */
    outw((uint16_t)(uhci_base + UHCI_USBCMD), UHCI_CMD_GRESET);
    timer_sleep(15);
    outw((uint16_t)(uhci_base + UHCI_USBCMD), 0x0000u);
    timer_sleep(5);

    /* Host-controller reset (self-clearing) */
    outw((uint16_t)(uhci_base + UHCI_USBCMD), UHCI_CMD_HCRESET);
    {
        uint32_t t = timer_get_ticks() + 50u;
        while ((inw((uint16_t)(uhci_base + UHCI_USBCMD)) & UHCI_CMD_HCRESET) && timer_get_ticks() < t)
            io_wait();
    }

    /* Clear all pending status bits (RW1C) */
    outw((uint16_t)(uhci_base + UHCI_USBSTS),  0x003Fu);
    /* Disable all interrupts */
    outw((uint16_t)(uhci_base + UHCI_USBINTR), 0x0000u);
    /* Reset frame number */
    outw((uint16_t)(uhci_base + UHCI_FRNUM), 0x0000u);
    /* Set SOF timing (default 64 = 1 ms frame) */
    outb((uint16_t)(uhci_base + UHCI_SOF), 0x40u);
    /* Set frame list base address */
    outl((uint16_t)(uhci_base + UHCI_FLBASE), (uint32_t)uhci_frame_list);

    /* Point all frame-list entries to terminate (nothing to schedule yet) */
    uhci_fill_frames(LP_T);

    /* Set Configure Flag and start the controller (RS=1, MaxP=1 for 64-byte packets) */
    outw((uint16_t)(uhci_base + UHCI_USBCMD), UHCI_CMD_RS | UHCI_CMD_MAXP);
    timer_sleep(5);

    /* Verify it started (HCH should be 0) */
    if (inw((uint16_t)(uhci_base + UHCI_USBSTS)) & UHCI_STS_HCH)
        return;  /* controller failed to start */

    /* ---- 3. Probe ports 1 and 2 — use the first one with a device ---- */

    /* Helper: reset one port, return 1 if a device came up, 0 if empty.
     * portsc_reg: UHCI register offset (UHCI_PORTSC1 or UHCI_PORTSC2).
     * Sets usb_kbd_ls as a side-effect.                                  */
#define PROBE_PORT(portsc_reg) do {                                         \
    uint16_t _r = (uint16_t)(uhci_base + (portsc_reg));                     \
    uint16_t _p;                                                             \
    /* Assert reset — write only the RESET bit; never write to RW1C bits */ \
    _p = inw(_r);                                                            \
    outw(_r, (uint16_t)((_p & ~(UHCI_PORT_CSC|UHCI_PORT_ENC)) | UHCI_PORT_RESET)); \
    timer_sleep(60);                                                         \
    /* Deassert reset */                                                     \
    _p = inw(_r);                                                            \
    outw(_r, (uint16_t)(_p & ~UHCI_PORT_RESET));                            \
    timer_sleep(20);   /* USB recovery time */                               \
    /* Clear any stale change bits (RW1C) individually — do NOT read-modify-write \
     * with other bits ORed in, as that would inadvertently set RESET again */\
    _p = inw(_r);                                                            \
    if (_p & UHCI_PORT_CSC) outw(_r, (uint16_t)(UHCI_PORT_CSC));           \
    timer_sleep(2);                                                          \
    if (_p & UHCI_PORT_ENC) outw(_r, (uint16_t)(UHCI_PORT_ENC));           \
    timer_sleep(2);                                                          \
    _p = inw(_r);                                                            \
    if (!(_p & UHCI_PORT_CCS)) break;  /* nothing connected */              \
    /* Enable port if the controller didn't auto-enable it */               \
    if (!(_p & UHCI_PORT_EN)) {                                             \
        outw(_r, (uint16_t)((_p & ~(UHCI_PORT_CSC|UHCI_PORT_ENC)) | UHCI_PORT_EN)); \
        timer_sleep(10);                                                     \
        _p = inw(_r);                                                        \
    }                                                                        \
    if (!(_p & UHCI_PORT_EN)) break;  /* failed to enable */               \
    usb_kbd_ls = (_p & UHCI_PORT_LSDA) ? 1 : 0;                            \
    port_found = 1;                                                          \
} while (0)

    int port_found = 0;
    PROBE_PORT(UHCI_PORTSC1);
    if (!port_found) PROBE_PORT(UHCI_PORTSC2);
    if (!port_found) return;  /* no device on either port */

#undef PROBE_PORT

    /* ---- 4. Enumerate: SET_ADDRESS(1) ---- */
    static uint8_t setup[8];
    make_setup(setup, 0x00u, 0x05u, 0x0001u, 0x0000u, 0x0000u); /* SET_ADDRESS */
    if (uhci_control(0, 0, usb_kbd_ls, setup, (void *)0, 0, 0) != 0)
        return;
    timer_sleep(5);   /* device needs time to adopt its new address */
    usb_kbd_addr = 1;

    /* ---- 5. GET_DESCRIPTOR(Device) — 18 bytes ---- */
    make_setup(setup, 0x80u, 0x06u, 0x0100u, 0x0000u, 18u);
    if (uhci_control(usb_kbd_addr, 0, usb_kbd_ls, setup,
                     uhci_data_buf, 18u, 1) != 0)
        return;
    /* uhci_data_buf[7] = bMaxPacketSize0 — always 8 for HID keyboards;
     * we hard-code 8 throughout, so we skip reading it.                  */

    /* ---- 6. GET_DESCRIPTOR(Configuration) — up to 64 bytes ---- */
    make_setup(setup, 0x80u, 0x06u, 0x0200u, 0x0000u, 64u);
    if (uhci_control(usb_kbd_addr, 0, usb_kbd_ls, setup,
                     uhci_data_buf, 64u, 1) != 0)
        return;

    /* Parse to find the interrupt IN endpoint number */
    uint16_t total_len = (uint16_t)uhci_data_buf[2]
                       | ((uint16_t)uhci_data_buf[3] << 8);
    if (total_len > 64u) total_len = 64u;
    if (parse_config_descriptor(uhci_data_buf, total_len) != 0) {
        /* Descriptor parsing failed — assume endpoint 1 (works for most
         * standard HID keyboards)                                        */
        usb_kbd_endp = 1;
    }

    /* ---- 7. SET_CONFIGURATION(1) ---- */
    make_setup(setup, 0x00u, 0x09u, 0x0001u, 0x0000u, 0x0000u);
    if (uhci_control(usb_kbd_addr, 0, usb_kbd_ls, setup,
                     (void *)0, 0, 0) != 0)
        return;
    timer_sleep(5);

    /* ---- 8. HID SET_PROTOCOL(0) — boot protocol ----
     * bmRequestType = 0x21 (class, interface, host→device)
     * bRequest      = 0x0B (SET_PROTOCOL)
     * wValue        = 0x0000 (boot protocol)
     * wIndex        = 0x0000 (interface 0)
     */
    make_setup(setup, 0x21u, 0x0Bu, 0x0000u, 0x0000u, 0x0000u);
    /* Some devices STALL SET_PROTOCOL if they only support boot protocol;
     * that is harmless — ignore the return code.                         */
    uhci_control(usb_kbd_addr, 0, usb_kbd_ls, setup, (void *)0, 0, 0);
    timer_sleep(5);

    /* ---- 9. HID SET_IDLE(0, 0) — suppress repeat reports ----
     * wValue = 0x0000: duration=0 (indefinite — only report on change),
     *                  report ID=0 (all reports)
     */
    make_setup(setup, 0x21u, 0x0Au, 0x0000u, 0x0000u, 0x0000u);
    uhci_control(usb_kbd_addr, 0, usb_kbd_ls, setup, (void *)0, 0, 0);

    /* ---- 10. Set up the persistent interrupt-endpoint TD ---- */
    usb_kbd_toggle = 0;  /* data toggle resets to DATA0 after SET_CONFIGURATION */

    uhci_td_t *itd = &uhci_tds[INTR_TD_IDX];
    itd->buffer = (uint32_t)uhci_kbd_report;
    itd->token  = make_token(PID_IN, usb_kbd_addr, usb_kbd_endp, usb_kbd_toggle, 7u);
    itd->status = TD_STATUS_ACTIVE(usb_kbd_ls);
    itd->link   = LP_T;

    uhci_intr_qh.link    = LP_T;               /* no next QH          */
    uhci_intr_qh.element = (uint32_t)itd;      /* first (only) TD     */

    /* Zero out the previous-report buffer */
    uint8_t zi;
    for (zi = 0; zi < 8; zi++) uhci_kbd_prev[zi] = 0;

    /* Hook into the timer secondary callback so we poll every 8 ms */
    timer_register_secondary(usb_timer_tick);

    /* Point all frame-list entries at the interrupt QH */
    uhci_arm_intr_frames();

    /* Mark driver as operational */
    usb_ready = 1;

    /* Let the user know */
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  USB HID keyboard detected and active.\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
