/*
 * inteilidOS -- kernel/usb.c
 * USB HID boot keyboard/mouse driver
 *
 * UHCI and xHCI direct-root-port boot HID; xHCI is in usb_xhci.c.
 *
 * The driver:
 *   1. Scans PCI for up to eight UHCI controllers (class 0x0C/0x03/pi 0x00)
 *   2. Resets each controller once and enumerates its two root ports at boot
 *   3. Enumerates the device via synchronous control transfers
 *   4. Parses configuration/interface/endpoint descriptors; selects boot HID
 *   5. Polls independent keyboard/mouse TDs via the timer secondary hook
 *   6. Translates HID usage IDs → ASCII / special-key codes and injects them
 *      into the shared kernel queue with character and modifier snapshots
 *
 * If anything fails (no UHCI found, no device, stall during enumeration) the
 * function returns silently.  PS/2 keyboard operation is unaffected.
 */

#include "usb.h"
#include "usb_xhci.h"
#include "usb_ehci.h"
#include "pci.h"
#include "keyboard.h"
#include "timer.h"
#include "vga.h"
#include "memory.h"
#include <stdint.h>

/* =========================================================================
 * Port I/O helpers
 * ========================================================================= */
#ifdef USB_UHCI_TEST
#include "../tests/usb_uhci_test_io.h"
#else
static inline uint8_t  inb (uint16_t p) { uint8_t  v; __asm__ volatile("inb  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void     outb(uint16_t p, uint8_t  v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint16_t inw (uint16_t p) { uint16_t v; __asm__ volatile("inw  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void     outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline uint32_t inl (uint16_t p) { uint32_t v; __asm__ volatile("inl  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void     outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline void     io_wait(void) { outb(0x80, 0); }
#endif

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

#define CTRL_TD_MAX 68
#define UHCI_MAX_CONTROLLERS 8
typedef struct {
    uhci_qh_t qh;
    uhci_td_t td;
    volatile uint8_t report[8];
    uint8_t previous[8];
    uint8_t address, endpoint, toggle, lowspeed, protocol, port;
    unsigned interval, next_poll;
    int ready;
} uhci_hid_t;
typedef struct {
    uint32_t frames[1024] __attribute__((aligned(4096)));
    uhci_qh_t control;
    uhci_td_t tds[CTRL_TD_MAX];
    uint8_t setup[8], data[512];
    uint16_t base;
    uint8_t ep0_packet;
    int unsafe; /* DMA descriptors remain pinned after failed retirement */
    uhci_hid_t hid[2]; /* one boot interface per direct root port */
} __attribute__((aligned(4096))) uhci_controller_t;
/* Controller DMA stays pinned until reboot. Allocate only discovered
 * controllers, not eight frame lists in the tightly bounded kernel BSS. */
static uhci_controller_t *controllers[UHCI_MAX_CONTROLLERS];
static unsigned controller_count;
static uhci_hid_t *mouse_device;
static uhci_controller_t *mouse_controller;
static int mouse_active, mouse_x, mouse_y;
static unsigned mouse_buttons;

/* =========================================================================
 * Driver state
 * ========================================================================= */
static int keyboard_ready;
static const char *usb_status = "Native USB not enabled";
const char *usb_keyboard_status(void) { return usb_status; }

/* =========================================================================
 * Low-level UHCI helpers
 * ========================================================================= */

/* Fill all 1024 frame-list entries with the given link pointer value.    */
static void uhci_fill_frames(uhci_controller_t *c,uint32_t lp) {
    uint32_t i;
    for (i = 0; i < 1024; i++)
        c->frames[i] = lp;
}

/* Initialize the control -> keyboard/mouse schedule before enumeration.
 * Every frame reaches both HID QHs, which remain idle until configured. */
static void uhci_arm_intr_frames(uhci_controller_t *c) {
    /* Publish a fixed chain once at controller startup. Control transfers
     * use its first QH without replacing the live HID schedule. */
    c->control.link=(uint32_t)&c->hid[0].qh|LP_Q;
    c->control.element=LP_T;
    c->hid[0].qh.link=(uint32_t)&c->hid[1].qh|LP_Q;
    c->hid[1].qh.link=LP_T;
    c->hid[0].qh.element=c->hid[1].qh.element=LP_T;
    uhci_fill_frames(c,(uint32_t)&c->control|LP_Q);
}

/* Wait for the last TD in a control transfer chain to complete.
 * Returns 0 on success, -1 on error/timeout.
 * last_td: index in uhci_tds[] of the STATUS-phase TD.                  */
static int uhci_wait_ctrl(uhci_controller_t *c,int last_td) {
    uint32_t deadline = timer_get_ticks() + 200u;  /* 200 ms timeout */
    while (c->tds[last_td].status & TD_ACTIVE) {
        if (timer_get_ticks() >= deadline)
            return -1;  /* timeout */
        io_wait();
    }
    /* Check for hardware-reported errors */
    for (int i=0;i<=last_td;i++) if (c->tds[i].status & TD_ERR_MASK)
        return -1;
    return 0;
}

/* Only used during boot enumeration. Halt (not reset) this UHCI engine
 * before reusing control DMA: merely unlinking a QH can race its writeback.
 * Keyboard TD/toggle state and port ownership survive this bounded pause.
 * UHCI never owns storage; the independent EHCI engine is untouched. */
static int uhci_retire_control(uhci_controller_t *c) {
    uint16_t command=inw(c->base+UHCI_USBCMD);
    outw(c->base+UHCI_USBCMD,command&~UHCI_CMD_RS);
    unsigned start=timer_get_ticks();
    while (!(inw(c->base+UHCI_USBSTS)&UHCI_STS_HCH)) {
        if (timer_get_ticks()-start>=50) {
            c->unsafe=1;
            for (unsigned p=0;p<2;p++) {
                if (c->hid[p].ready && c->hid[p].protocol==1) {
                    keyboard_ready=0;
                    usb_status="UHCI DMA retirement failed; reboot required";
                }
                c->hid[p].ready=0;
            }
            if (mouse_controller==c) {mouse_x=mouse_y=0;mouse_buttons=0;}
            return -1; /* never reuse or free DMA which may still be active */
        }
        io_wait();
    }
    c->control.element=LP_T;
    __asm__ volatile("":::"memory");
    outw(c->base+UHCI_USBCMD,command);
    return 0;
}

/* =========================================================================
 * Synchronous control transfer
 *
 * Executes a complete USB control transfer (SETUP + optional DATA + STATUS)
 * through a separate control QH, leaving live interrupt QHs reachable.
 *
 * setup:    8-byte USB SETUP packet
 * data:     data buffer (NULL if no data phase)
 * data_len: number of bytes to transfer (0 if no data phase)
 * dir_in:   1 = DATA IN (device→host GET), 0 = no data (SET commands)
 * addr:     USB device address (0 before SET_ADDRESS, root-port index + 1 after)
 * endp:     endpoint number (always 0 for control pipe)
 * ls:       1 if low-speed device
 *
 * Returns 0 on success, -1 on error.
 * ========================================================================= */
static int uhci_control(uhci_controller_t *c,uint8_t addr, uint8_t endp, int ls,
                        const uint8_t *setup,
                        uint8_t *data, uint16_t data_len, int dir_in) {
    int td = 0;
    if (c->unsafe) return -1;
    uhci_td_t *uhci_tds=c->tds;
    uint8_t *uhci_setup_buf=c->setup;

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
            uint16_t pkt = (remain > c->ep0_packet) ? c->ep0_packet : remain;
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
    __asm__ volatile("":::"memory");
    c->control.element = (uint32_t)&uhci_tds[0];

    /* Spin until the STATUS TD completes */
    int rc = uhci_wait_ctrl(c,last);
    if (uhci_retire_control(c)) return -1;

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
    /*0x42*/ '\x98',                        /* 42: F9                      */
    /*0x43*/ '\x99',                        /* 43: F10                     */
    /*0x44*/ '\x9A',                        /* 44: F11                     */
    /*0x45*/ '\x9B',                        /* 45: F12                     */
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
    [0x1E]='!', [0x1F]='@', [0x20]='#', [0x21]='$', [0x22]='%',
    [0x23]='^', [0x24]='&', [0x25]='*', [0x26]='(', [0x27]=')',
    [0x2D]='_', [0x2E]='+', [0x2F]='{', [0x30]='}', [0x31]='|',
    [0x33]=':', [0x34]='"', [0x35]='~', [0x36]='<', [0x37]='>', [0x38]='?',
};

/* State for caps lock (toggled in process_report) */
static int usb_caps = 0;

/* Physical US set-1 positions, independent of shifted/control characters. */
static const uint8_t hid_scancode[0x53] = {
    [4]=0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,
    0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,
    0x2f,0x11,0x2d,0x15,0x2c,
    [0x1e]=2,3,4,5,6,7,8,9,10,11,
    [0x28]=0x1c,1,0x0e,0x0f,0x39,0x0c,0x0d,0x1a,0x1b,0x2b,
    [0x33]=0x27,0x28,0x29,0x33,0x34,0x35,
    [0x3a]=0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44,0x57,0x58,
    [0x4c]=0x0e, /* retain the native Delete -> Backspace behavior */
    [0x4f]=0x4d,0x4b,0x50,0x48
};

int usb_process_boot_report(const uint8_t cur[8], const uint8_t prev[8]) {
    for (unsigned i=2;i<8;i++)
        if (cur[i]>=1 && cur[i]<=3) return 0;
    uint8_t mod = cur[0];
    int shifted = (mod & 0x22u) != 0;  /* L or R Shift */
    /* Publish even modifier-only reports (including the all-released report).
     * Caps state is published again below if this report toggles it. */
    keyboard_usb_modifiers(mod, (uint8_t)usb_caps);

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
            if (!was_held) {
                usb_caps ^= 1;
                keyboard_usb_modifiers(mod, (uint8_t)usb_caps);
            }
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

        char c = shifted && hid_shift[kc] ? hid_shift[kc] : hid_normal[kc];

        if (kc>=4 && kc<=0x1D) {
            c=(char)('a'+kc-4);
            if (mod&0x11) c=(char)(kc-3); /* Ctrl+A..Z */
            else if (shifted ^ usb_caps) c=(char)(c-'a'+'A');
        }

        if (c) {
            keyboard_event_t state;
            keyboard_current_event(&state);
            keyboard_inject_event((uint8_t)c, hid_scancode[kc],
                                  state.modifiers, state.caps_lock);
        }
    }
    return 1;
}

/* =========================================================================
 * Timer callback — checks completed interrupt TDs at their bounded interval
 * ========================================================================= */
static void uhci_poll(uhci_controller_t *c,uhci_hid_t *h) {
    if (!h->ready) return;
    uint16_t port=inw(c->base+UHCI_PORTSC1+h->port*2);
    /* Never reset an unplugged/replaced port from a timer or DOS session.
     * Reconnection is deliberately boot-time only. */
    if (!(port&UHCI_PORT_CCS) || !(port&UHCI_PORT_EN) ||
        (port&UHCI_PORT_CSC)) {
        h->ready=0;h->qh.element=LP_T;
        if (h==mouse_device) {mouse_x=mouse_y=0;mouse_buttons=0;}
        return;
    }
    unsigned now=timer_get_ticks();
    if ((int32_t)(now-h->next_poll)<0) return;
    h->next_poll=now+h->interval;
    uhci_td_t *td = &h->td;

    /* Not yet completed → still waiting for keypress data */
    if (td->status & TD_ACTIVE) return;

    /* Error? Re-arm and continue */
    if (td->status & TD_ERR_MASK) {
        td->status = TD_STATUS_ACTIVE(h->lowspeed);
        __asm__ volatile("":::"memory");
        h->qh.element = (uint32_t)td;
        return;
    }

    /* A report arrived — snapshot it (volatile prevents tear) */
    uint8_t snap[8];
    uint8_t i;
    for (i = 0; i < 8; i++)
        snap[i] = h->report[i];

    /* Translate and inject */
    /* Ignore short/empty reports rather than injecting stale DMA bytes. */
    unsigned length=((td->status&0x7FFu)+1)&0x7FF;
    if (h->protocol==1) {
        if (length==8 && usb_process_boot_report(snap,h->previous))
            for (i=0;i<8;i++) h->previous[i]=snap[i];
    } else if (length==3 && mouse_active && h==mouse_device) {
        mouse_buttons=snap[0]&7;
        int x=mouse_x+(int8_t)snap[1], y=mouse_y+(int8_t)snap[2];
        mouse_x=x<-32767?-32767:x>32767?32767:x;
        mouse_y=y<-32767?-32767:y>32767?32767:y;
    }

    /* Flip data toggle and re-arm the interrupt TD */
    h->toggle ^= 1u;
    td->token = make_token(PID_IN,h->address,h->endpoint,h->toggle,
                          h->protocol==1?7:2);
    td->status = TD_STATUS_ACTIVE(h->lowspeed);

    /* Re-link QH element to this TD */
    __asm__ volatile("":::"memory");
    h->qh.element = (uint32_t)td;
}
static void usb_timer_tick(void) {
    usb_xhci_poll();
    for (unsigned i=0;i<controller_count;i++)
        for (unsigned p=0;p<2;p++) uhci_poll(controllers[i],&controllers[i]->hid[p]);
}

/* These APIs are called with IRQs masked by mouse_input. UHCI enumeration
 * happens only at native boot, never while mounted storage/DOS is active. */
int usb_uhci_mouse_start(void) {
    mouse_active=0;
    /* Bypass the interval gate to discard a completed pre-session report. */
    if (mouse_device) {
        mouse_device->next_poll=timer_get_ticks();
        uhci_poll(mouse_controller,mouse_device);
    }
    mouse_x=mouse_y=0;mouse_buttons=0;
    mouse_active=mouse_device && mouse_device->ready;
    return mouse_active;
}
void usb_uhci_mouse_stop(void) {
    mouse_active=0;mouse_x=mouse_y=0;mouse_buttons=0;
}
void usb_uhci_mouse_poll(int *dx,int *dy,unsigned *buttons) {
    if (mouse_device) uhci_poll(mouse_controller,mouse_device);
    *dx=mouse_x;*dy=mouse_y;*buttons=mouse_buttons;
    mouse_x=mouse_y=0;
}

/* =========================================================================
 * Enumeration helpers
 * ========================================================================= */

/* Parse a raw configuration-descriptor blob for a HID interrupt-IN endpoint.
 * Selects an interrupt-IN endpoint number (1-15) on success.
 * Returns 0 if found, -1 if not found.                                   */
int usb_parse_boot_keyboard(const uint8_t *buf,unsigned total_len,
                            usb_keyboard_descriptor_t *out) {
    return usb_parse_boot_hid(buf,total_len,1,out);
}
int usb_parse_boot_hid(const uint8_t *buf,unsigned total_len,unsigned protocol,
                       usb_keyboard_descriptor_t *out) {
    if (!buf || !out || total_len<9 || buf[0]!=9 || buf[1]!=2 ||
        (buf[2]|((unsigned)buf[3]<<8))!=total_len || !buf[5] ||
        (protocol!=1 && protocol!=2)) return -1;
    unsigned off = 0;
    int keyboard=0, found=0;
    usb_keyboard_descriptor_t info={0};
    info.configuration=buf[5];
    while (off + 2u <= total_len) {
        uint8_t bLen  = buf[off];
        uint8_t bType = buf[off + 1];
        if (bLen<2 || bLen>total_len-off) return -1;
        if (bType==4) {
            if (bLen<9) return -1;
            keyboard=buf[off+3]==0 && buf[off+5]==3 &&
                     buf[off+6]==1 && buf[off+7]==protocol;
            if (keyboard && !found) info.interface=buf[off+2];
        }

        if (bType == 0x05u && keyboard && !found) {
            if (bLen<7) return -1;
            uint8_t addr  = buf[off + 2];
            uint8_t attrs = buf[off + 3];
            /* We want: direction=IN (bit7=1), transfer type=interrupt (bits1:0=3) */
            if ((addr & 0x80u) && (attrs & 0x03u) == 0x03u) {
                info.endpoint=addr&15;
                info.packet_size=buf[off+4]|((uint16_t)buf[off+5]<<8);
                info.interval=buf[off+6];
                if (!info.endpoint || (addr&0x70) ||
                    info.packet_size<(protocol==1?8:3) ||
                    info.packet_size>1024 || !info.interval) return -1;
                found=1;
            }
        }
        off = (uint16_t)(off + bLen);
    }
    if (off!=total_len || !found) return -1;
    *out=info;
    return 0;
}

/* =========================================================================
 * Main initialisation
 * ========================================================================= */

static int uhci_start(uhci_controller_t *c,pci_device_t uhci_pci) {
    if (!(uhci_pci.bar[4]&1) || (uhci_pci.bar[4]&0xFFFF0000u)) return 0;

    /* Enable bus mastering so the controller can DMA */
    pci_enable_busmaster(&uhci_pci);

    /* BAR4 is the I/O base for UHCI registers.
     * Bit 0 = 1 (I/O space indicator); mask it off with 0xFFFC.          */
    c->base = (uint16_t)(uhci_pci.bar[4] & 0xFFFCu);
    uint16_t uhci_base=c->base;
    if (uhci_base == 0) return 0;  /* malformed BAR */

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
        if (inw(uhci_base+UHCI_USBCMD)&UHCI_CMD_HCRESET) return 0;
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
    outl((uint16_t)(uhci_base + UHCI_FLBASE), (uint32_t)c->frames);

    /* Point all frame-list entries to terminate (nothing to schedule yet) */
    uhci_arm_intr_frames(c);

    /* Set Configure Flag and start the controller (RS=1, MaxP=1 for 64-byte packets) */
    outw((uint16_t)(uhci_base + UHCI_USBCMD), UHCI_CMD_RS | UHCI_CMD_MAXP);
    timer_sleep(5);

    /* Verify it started (HCH should be 0) */
    if (inw((uint16_t)(uhci_base + UHCI_USBSTS)) & UHCI_STS_HCH)
        return 0;  /* controller failed to start */
    return 1;
}

static void uhci_enumerate(uhci_controller_t *c,unsigned port) {
    uint16_t uhci_base=c->base;
    uhci_hid_t *h=&c->hid[port];
    uint8_t *uhci_data_buf=c->data;
    uint8_t usb_kbd_addr=port+1;
    int usb_kbd_ls=0;
    c->ep0_packet=8;
    /* No runtime port probing: this is called once per root port at boot. */

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
    if (_p & UHCI_PORT_CSC) outw(_r, (uint16_t)((_p & ~(UHCI_PORT_CSC|UHCI_PORT_ENC))|UHCI_PORT_CSC)); \
    timer_sleep(2);                                                          \
    if (_p & UHCI_PORT_ENC) outw(_r, (uint16_t)((_p & ~(UHCI_PORT_CSC|UHCI_PORT_ENC))|UHCI_PORT_ENC)); \
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
    PROBE_PORT(port ? UHCI_PORTSC2 : UHCI_PORTSC1);
    if (!port_found) return;  /* no device on either port */

#undef PROBE_PORT

    /* ---- 4. Enumerate: root ports use distinct addresses 1 and 2 ---- */
    static uint8_t setup[8];
    make_setup(setup,0x80,6,0x0100,0,8);
    if (uhci_control(c,0,0,usb_kbd_ls,setup,uhci_data_buf,8,1) ||
        uhci_data_buf[0]<18 || uhci_data_buf[1]!=1) return;
    c->ep0_packet=uhci_data_buf[7];
    if ((c->ep0_packet!=8 && c->ep0_packet!=16 &&
         c->ep0_packet!=32 && c->ep0_packet!=64) ||
        (usb_kbd_ls && c->ep0_packet!=8)) return;
    make_setup(setup, 0x00u, 0x05u, usb_kbd_addr, 0x0000u, 0x0000u);
    if (uhci_control(c,0, 0, usb_kbd_ls, setup, (void *)0, 0, 0) != 0)
        return;
    timer_sleep(5);   /* device needs time to adopt its new address */

    /* ---- 5. GET_DESCRIPTOR(Device) — 18 bytes ---- */
    make_setup(setup, 0x80u, 0x06u, 0x0100u, 0x0000u, 18u);
    if (uhci_control(c,usb_kbd_addr, 0, usb_kbd_ls, setup,
                     uhci_data_buf, 18u, 1) != 0)
        return;

    /* Header first, then complete configuration, never a guessed endpoint. */
    make_setup(setup, 0x80u, 0x06u, 0x0200u, 0x0000u, 9u);
    if (uhci_control(c,usb_kbd_addr, 0, usb_kbd_ls, setup,
                     uhci_data_buf, 9u, 1) != 0)
        return;

    /* Parse to find the interrupt IN endpoint number */
    uint16_t total_len = (uint16_t)uhci_data_buf[2]
                       | ((uint16_t)uhci_data_buf[3] << 8);
    if (total_len<9 || total_len>sizeof(c->data)) return;
    make_setup(setup,0x80,6,0x0200,0,total_len);
    if (uhci_control(c,usb_kbd_addr,0,usb_kbd_ls,setup,uhci_data_buf,total_len,1)) return;
    usb_keyboard_descriptor_t info;
    unsigned protocol=1;
    if (usb_parse_boot_hid(uhci_data_buf,total_len,1,&info)) {
        protocol=2;
        if (usb_parse_boot_hid(uhci_data_buf,total_len,2,&info)) return;
    }
    if (info.packet_size>64 || (usb_kbd_ls && info.packet_size>8) ||
        (protocol==1 && keyboard_ready) || (protocol==2 && mouse_device)) return;

    /* ---- 7. SET_CONFIGURATION(1) ---- */
    make_setup(setup, 0x00u, 0x09u, info.configuration, 0x0000u, 0x0000u);
    if (uhci_control(c,usb_kbd_addr, 0, usb_kbd_ls, setup,
                     (void *)0, 0, 0) != 0)
        return;
    timer_sleep(5);

    /* ---- 8. HID SET_PROTOCOL(0) — boot protocol ----
     * bmRequestType = 0x21 (class, interface, host→device)
     * bRequest      = 0x0B (SET_PROTOCOL)
     * wValue        = 0x0000 (boot protocol)
     * wIndex        = 0x0000 (interface 0)
     */
    make_setup(setup, 0x21u, 0x0Bu, 0x0000u, info.interface, 0x0000u);
    if (uhci_control(c,usb_kbd_addr, 0, usb_kbd_ls, setup, (void *)0, 0, 0)) return;
    timer_sleep(5);

    /* ---- 9. HID SET_IDLE(0, 0) — suppress repeat reports ----
     * wValue = 0x0000: duration=0 (indefinite — only report on change),
     *                  report ID=0 (all reports)
     */
    make_setup(setup, 0x21u, 0x0Au, 0x0000u, info.interface, 0x0000u);
    uhci_control(c,usb_kbd_addr, 0, usb_kbd_ls, setup, (void *)0, 0, 0);
    if (c->unsafe) return;

    /* ---- 10. Set up the persistent interrupt-endpoint TD ---- */
    h->address=usb_kbd_addr;h->endpoint=info.endpoint;h->toggle=0;
    h->lowspeed=usb_kbd_ls;h->protocol=protocol;h->port=port;
    /* UHCI executes all QHs each frame, but rearming no faster than bInterval
     * bounds the effective HID poll rate (including low-speed's >=10 ms). */
    h->interval=info.interval;
    if (h->interval<(usb_kbd_ls?10u:1u)) h->interval=usb_kbd_ls?10:1;
    h->next_poll=timer_get_ticks();
    uhci_td_t *itd = &h->td;
    itd->buffer = (uint32_t)h->report;
    itd->token = make_token(PID_IN,h->address,h->endpoint,0,protocol==1?7:2);
    itd->status = TD_STATUS_ACTIVE(usb_kbd_ls);
    itd->link   = LP_T;

    /* Acknowledge only this device's initial connect baseline before polling. */
    uint16_t reg=uhci_base+UHCI_PORTSC1+port*2;
    uint16_t p=inw(reg);
    outw(reg,(p&~(UHCI_PORT_CSC|UHCI_PORT_ENC))|UHCI_PORT_CSC|UHCI_PORT_ENC);
    if (protocol==1) {keyboard_ready=1;usb_status="UHCI USB keyboard active";}
    else {mouse_device=h;mouse_controller=c;}
    __asm__ volatile("":::"memory");
    h->qh.element=(uint32_t)itd;
    h->ready=1;

    /* Let the user know */
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts(protocol==1?"  UHCI USB HID keyboard active.\n":
                         "  UHCI USB boot mouse available for DOS /MOUSE.\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void usb_keyboard_init(void) {
    static int initialized;
    if (initialized) return; /* never reset controllers after initial boot */
    initialized=1;
    usb_caps=0;
    usb_status="No supported USB keyboard found (PS/2 fallback)";
    timer_register_secondary(usb_timer_tick);
    usb_ehci_init(); /* route companion ports before UHCI owns a keyboard */
    keyboard_ready=usb_xhci_init();
    if (keyboard_ready) {
        usb_status="xHCI USB keyboard active";
        vga_puts("  xHCI USB keyboard detected and active.\n");
    }
    pci_device_t current,next;
    if (!pci_find_class(0x0C,0x03,&current)) {
        do {
            if (current.prog_if==0 && controller_count<UHCI_MAX_CONTROLLERS) {
                void *allocation=kmalloc(sizeof(uhci_controller_t)+4095);
                if (!allocation) break;
                uhci_controller_t *c=(uhci_controller_t *)
                    (((uintptr_t)allocation+4095)&~(uintptr_t)4095);
                kmemset(c,0,sizeof(*c));
                controllers[controller_count++]=c;
                if (uhci_start(c,current)) {
                    for (unsigned port=0;port<2;port++) {
                        uhci_enumerate(c,port);
                        if (c->unsafe) {
                            vga_puts("  UHCI DMA retirement failed; reboot required.\n");
                            break;
                        }
                        /* A failed device at address zero must not answer the
                         * next port's enumeration. Never touch a live HID. */
                        if (!c->hid[port].ready) {
                            uint16_t reg=c->base+UHCI_PORTSC1+port*2;
                            outw(reg,inw(reg)&~(UHCI_PORT_EN|UHCI_PORT_CSC|UHCI_PORT_ENC));
                        }
                    }
                }
            }
            if (pci_find_class_after(0x0C,0x03,&current,&next)) break;
            current=next;
        } while (1);
    }
    if (!keyboard_ready)
        vga_puts("  Native USB keyboard unavailable; use PS/2/BIOS legacy input.\n");
}
