# inteiliDOS — Developer Reference

```
  _       _       _ _ _  ___   ___  ____
 (_)_ __ | |_ ___(_) (_)|   \ / _ \/ ___|
 | | '_ \| __/ _ \ | | || |) | | | \___ \
 | | | | | ||  __/ | | ||___/ | |_| |___) |
 |_|_| |_|\__\___|_|_|_||_|    \___/|____/

 Developer Reference — Version 1.0
 Inteilix Software Corporation
```

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Boot Sequence — Step by Step](#2-boot-sequence--step-by-step)
3. [Memory Layout](#3-memory-layout)
4. [Kernel APIs](#4-kernel-apis)
   - [VGA / Display](#41-vga--display)
   - [Heap & Memory Utilities](#42-heap--memory-utilities)
   - [Keyboard Input](#43-keyboard-input)
   - [Timer & PC Speaker](#44-timer--pc-speaker)
   - [Interrupt Handling](#45-interrupt-handling)
   - [PCI Bus Scanner](#46-pci-bus-scanner)
   - [USB HID Keyboard Driver](#47-usb-hid-keyboard-driver)
   - [ATAPI CD-ROM Driver](#48-atapi-cd-rom-driver)
   - [InteiliTalk — SAM TTS Subsystem](#49-inteilitalk--sam-tts-subsystem)
   - [InteiliBASIC Cassette Interface (CSAVE / CLOAD)](#410-inteilibasic-cassette-interface-csave--cload)
5. [The IntelliShell Application Model](#5-the-intellishell-application-model)
   - [Adding a Shell Command](#adding-a-shell-command)
   - [Adding an NLP Phrase](#adding-an-nlp-phrase)
   - [Writing a Full-Screen Application](#writing-a-full-screen-application)
6. [Build System](#6-build-system)
   - [Build Targets — Modern vs Legacy](#build-targets--modern-vs-legacy)
   - [Prerequisites](#prerequisites)
   - [Compiler Flags](#compiler-flags-set-in-cmakeliststxt)
   - [Adding Source Files](#adding-source-files)
   - [Build Commands](#build-commands)
   - [Inspecting the Binary](#inspecting-the-binary)
7. [Compiler Constraints & Gotchas](#7-compiler-constraints--gotchas)
8. [Interrupt & Exception Reference](#8-interrupt--exception-reference)
9. [Coding Conventions](#9-coding-conventions)
10. [Roadmap & Contribution Areas](#10-roadmap--contribution-areas)

---

## 1. Architecture Overview

inteiliDOS is a flat, single-address-space, single-privilege-level (ring 0) operating system. There is no kernel/user split, no virtual memory, no process scheduler, and no system-call boundary. Every piece of code — kernel, shell, editor, BASIC interpreter — runs as part of one monolithic binary at ring 0 with full hardware access.

```
┌────────────────────────────────────────────────────┐
│                IntelliShell REPL                   │  shell/shell.c
├──────────────┬─────────────────────────────────────┤
│  Commands    │  Applications                       │  shell/commands.c
│  (40+ cmds)  │  IEdit │ BASIC │ Sheets │ Talk      │  shell/iedit.c, basic.c
│              │  DEMO  │ TOUR  │ Tetris │ QUIT      │  shell/sheets.c, talk.c, tour.c
│              │  LaunchPad │ FileManager │ Setup    │  shell/launchpad.c, filemanager.c
├──────────────┴─────────────────────────────────────┤
│               Kernel Services                      │
│  VGA │ Keyboard │ Timer │ Memory │ ISR/IRQ         │  kernel/*.c
│  PCI scanner │ USB HID keyboard (UHCI)             │  kernel/pci.c, usb.c
│  ATAPI CD-ROM │ ATA/IDE │ FDC floppy               │  kernel/cdrom.c, ata.c, fdc.c
│  ISO 9660 reader │ FAT12 reader │ AC'97 audio      │  kernel/iso9660.c, fat12.c, ac97.c
│  Program Loader (IPGM + ELF32)                     │  kernel/loader.c
├────────────────────────────────────────────────────┤
│           Protected-Mode Stubs                     │  boot/isr_stubs.asm
│           GDT / IDT / TSS                          │  kernel/gdt.c, idt.c
├────────────────────────────────────────────────────┤
│              Boot Entry (_start)                   │  boot/boot.asm
└────────────────────────────────────────────────────┘
              Real x86 Hardware (HP Vectra VEi8)
```

> **This is the HP Vectra VEi8 port.** The additional drivers (AC'97, FDC, ISO 9660, FAT12, loader) and applications (LaunchPad, InteiliFile Manager, Tetris, SETUP) are HP-specific. The kernel and shell compile and run identically on any other x86 PC; the HP-only code is isolated in its own source files and gracefully degrades (silent no-ops) when the hardware is not present.

**Key facts:**

- Target architecture: **IA-32 (32-bit protected mode)**, flat segments. Two CPU targets available via `build.sh`: `i686` (modern build, Pentium Pro and later) and `i486` (legacy build, 486DX / Pentium / Pentium MMX).
- Compiled with: `i686-elf-gcc` cross-compiler, `-ffreestanding -nostdlib`. Optimisation: `-O2` for modern, `-O1` for legacy (avoids instruction-scheduling assumptions that crash pre-Pentium-Pro CPUs).
- No standard library. No `libc`, no `libm`, no `libgcc` soft-float helpers.
- No dynamic allocation of descriptors; all major tables (GDT, IDT, ISR handler array) are static fixed-size arrays.
- The entire OS — kernel + shell + all applications — links into a **single ELF binary** (`inteilidOS.elf`) and is packaged into a bootable ISO via GRUB. Legacy builds additionally produce a 1.44 MB raw floppy image.
- **LaunchPad programs** are separate ELF32 executables compiled by their own build systems and distributed as plain ISO 9660 data discs. They run at 0x00500000 inside the same flat-32 address space — no kernel re-entry, no mode change. See §4.11 for the full program interface.

---

## 2. Boot Sequence — Step by Step

```
Power on / GRUB loads ISO
        │
        ▼
boot/boot.asm  _start
  ├── jmp over Multiboot header
  ├── mov esp, stack_top        (16 KB stack in .bss)
  ├── push EBX (Multiboot info ptr)
  ├── push EAX (Multiboot magic)
  ├── popf 0                    (zero EFLAGS, clear IF)
  └── call kernel_main
        │
        ▼
kernel/kernel.c  kernel_main(mb_magic, mb_info_phys)
  ├── vga_init()                 clear screen, reset cursor
  ├── clts                       clear CR0.TS (avoids #NM on implicit FPU checks)
  ├── gdt_init()                 load 6-entry GDT (null/kcode/kdata/ucode/udata/TSS)
  ├── idt_init()                 zero-fill 256-entry IDT, lidt
  ├── isr_init()                 wire CPU exception gates (0–31)
  ├── irq_init()                 remap 8259A PIC to vectors 32–47, wire IRQ gates
  ├── heap_init()                initialise 2 MB embedded heap
  ├── memory_init(mb_info_phys)  bitmap allocator from Multiboot mmap (GRUB path)
  ├── timer_init(1000)           program PIT at 1 kHz, register IRQ0 handler
  ├── keyboard_init()            register IRQ1 handler
  ├── sti                        enable interrupts
  ├── speaker_boot_chime()       C5→E5→G5→C6 arpeggio
  ├── usb_keyboard_init()        PCI scan → UHCI reset → USB HID enumeration
  ├── ata_detect(ata_drives[])   enumerate all four IDE positions; count ATA hard disks
  ├── cdrom_init()               detect ATAPI drives; fills cdrom_drives[]; prints "[OK] N HDD, M CD-ROM"
  ├── print_banner(mem_kb)       welcome screen
  └── shell_run()                ← control never returns from here
```

> **Important for driver authors:** `isr_init` and `irq_init` must be called before any code that registers an IRQ handler. The PIC is **not** remapped until `irq_init` runs, so any hardware interrupt that fires before that point will land on a CPU exception vector.

---

## 3. Memory Layout

```
Physical address space (32-bit, flat)
─────────────────────────────────────────────────────────────────
0x00000000 – 0x000003FF   Real-mode IVT (unused in PM, but don't write here)
0x00000400 – 0x000004FF   BIOS Data Area
0x00007C00 – 0x00007DFF   MBR load address (irrelevant after hand-off)
0x00090000 – 0x0009FFFF   GRUB scratch / Multiboot info structure
0x000A0000 – 0x000BFFFF   VGA framebuffer (not text; unused)
0x000B8000 – 0x000B8F9F   VGA text buffer (80×25 × 2 bytes = 4000 bytes)
0x000C0000 – 0x000FFFFF   Video BIOS / option ROMs (do not write)

0x00100000                 ← kernel load address (_start)
  .text                    executable code
  .rodata                  read-only data (string literals, const tables)
  .data                    initialised globals
  ─── _kernel_data_end ─── (aligned to 512 bytes)
  .bss
    stack_bottom            }
    ...                     }  16 KB kernel stack
    stack_top               }
    vga state (3 vars)
    gdt[6], gdtp, tss
    idt[256], idtp
    isr_handlers[256]
    ticks (uint32_t)
    keyboard buffer + state
    phys_bitmap[131072]    ← 128 KB; maps 4 GB @ 4 KB pages
    heap_storage[2097152]  ← 2 MB embedded heap
    heap_head (pointer)
  ─── _kernel_end ──────── (aligned to 4 KB)

0xB8000                    VGA text framebuffer (also mapped in low memory above)
```

### Stack

The kernel has a **single 16 KB stack** defined in `boot.asm` as a BSS reservation between `stack_bottom` and `stack_top`. There is no guard page. Stack overflows are silent and will corrupt adjacent BSS variables (starting with the VGA state).

Estimated worst-case call depth at runtime (shell → command → interrupt handler): **< 400 bytes**. The 16 KB stack has ample headroom for normal use, but recursive code or very deep call chains should be avoided.

### Heap

The heap is a **2 MB static array** (`heap_storage[]` in `memory.c`). It uses a simple linked-list first-fit allocator with block coalescing on `kfree`. The heap is initialised by `heap_init()` before `memory_init()`, so `kmalloc` is available throughout the kernel.

There is no `brk`/`mmap` — the heap cannot grow beyond 2 MB. Plan allocations accordingly.

---

## 4. Kernel APIs

All kernel headers live in `kernel/`. Include them with relative paths from your source file location.

---

### 4.1 VGA / Display

**Header:** `kernel/vga.h`

The VGA driver writes directly to the text buffer at `0xB8000`. The display is 80 columns × 25 rows. Each cell is 2 bytes: low byte = ASCII character, high byte = colour attribute (4 bits background | 4 bits foreground).

#### Colour constants

```c
typedef enum {
    VGA_COLOR_BLACK         = 0,   VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,   VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,   VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,   VGA_COLOR_LIGHT_GREY    = 7,
    VGA_COLOR_DARK_GREY     = 8,   VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,  VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,  VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN   = 14,  VGA_COLOR_WHITE         = 15,
} vga_color_t;
```

The 16 "light" colours are only valid as **foreground**. Background colours are limited to the first 8 (0–7), because the hardware high bit of the background nibble controls the "blink" attribute rather than a colour.

#### API

```c
/* Initialise the driver. Called once by kernel_main. */
void vga_init(void);

/* Set the active foreground and background colour for all subsequent output. */
void vga_set_color(vga_color_t fg, vga_color_t bg);

/* Write a single character at the current cursor position, advancing it. */
void vga_putchar(char c);

/* Write a null-terminated string. Handles \n by moving to the next line. */
void vga_puts(const char *str);

/* printf-style formatted output. Supported specifiers: %s %c %d %u %x */
void vga_printf(const char *fmt, ...);

/* Clear the entire screen and reset the cursor to (0, 0). */
void vga_clear(void);

/* Move the hardware cursor to (row, col). Both are 0-based. */
void vga_set_cursor(int row, int col);

/* Read the current cursor position into *row and *col. */
void vga_get_cursor(int *row, int *col);

/* Scroll the screen up one line. Called automatically by vga_putchar on overflow. */
void vga_scroll(void);

/* Write a single character with an explicit colour, without changing
   the active colour state. Useful for status bars. */
void vga_put_colored(char c, vga_color_t fg, vga_color_t bg);
```

#### Direct VGA access for full-screen applications

When you need arbitrary cursor positioning (as IEdit does), bypass the sequential API and write directly to the framebuffer:

```c
#define VGA_BUF ((volatile uint16_t *)0xB8000)

static inline void vga_write_at(int row, int col, char c,
                                 vga_color_t fg, vga_color_t bg) {
    uint8_t attr = (uint8_t)((bg << 4) | (fg & 0x0F));
    VGA_BUF[row * 80 + col] = (uint16_t)((attr << 8) | (uint8_t)c);
}
```

After writing directly to the buffer, call `vga_set_cursor(row, col)` to move the hardware cursor to where the user's focus is. If you do not, the blinking cursor will appear at the wrong position.

#### Saving and restoring colour state

`vga_set_color` changes a driver-global variable. When your command or application returns to the shell, the colour must be **white on black** — the shell expects this as its baseline. Always restore before returning:

```c
vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
```

---

### 4.2 Heap & Memory Utilities

**Header:** `kernel/memory.h`

#### Allocation

```c
/* Allocate size bytes. Returns NULL on failure (heap exhausted). */
void *kmalloc(size_t size);

/* Allocate nmemb × size bytes, zero-initialised. Returns NULL on failure. */
void *kcalloc(size_t nmemb, size_t size);

/* Free a block previously returned by kmalloc or kcalloc. */
void kfree(void *ptr);
```

`kmalloc` has **no alignment guarantee beyond 4 bytes**. If you need a larger alignment (e.g. for a DMA buffer), over-allocate and align the pointer manually.

There is no `krealloc`. To resize a buffer, `kmalloc` a new one, `kmemcpy` the data, and `kfree` the old one.

#### Memory utility functions

```c
void  *kmemset(void *dst, int c, size_t n);           /* fill n bytes with c */
void  *kmemcpy(void *dst, const void *src, size_t n); /* copy n bytes */
int    kmemcmp(const void *a, const void *b, size_t n);/* compare n bytes */
```

#### String utility functions

```c
size_t      kstrlen (const char *s);
int         kstrcmp (const char *a, const char *b);
int         kstrncmp(const char *a, const char *b, size_t n);
char       *kstrcpy (char *dst, const char *src);
char       *kstrncpy(char *dst, const char *src, size_t n);
char       *kstrcat (char *dst, const char *src);
char       *kstrtolower(char *s);             /* in-place; returns dst */
const char *kstrstr (const char *haystack, const char *needle);
```

> **Do not use `<string.h>` or `<stdlib.h>`.** The kernel is compiled freestanding — including standard library headers will either fail to compile or silently pull in unsupported intrinsics. Use the `k`-prefixed functions above for all string and memory operations.

#### Memory statistics

```c
size_t memory_total_kb(void); /* total RAM detected (0 if no Multiboot mmap) */
size_t memory_free_kb(void);  /* estimated free physical pages               */
```

---

### 4.3 Keyboard Input

**Header:** `kernel/keyboard.h`

```c
/* Block until a key is pressed and return its character value. */
int keyboard_getchar(void);

/* Return the next character from the keyboard buffer, or -1 if empty. */
int keyboard_poll(void);

/* Inject a character directly into the keyboard ring buffer.
 * Used by external drivers (e.g. the USB HID driver) to share the same
 * buffer without touching IRQ1 or the PS/2 port.
 * Safe to call from any context, including timer callbacks.            */
void keyboard_inject(uint8_t c);
```

#### Special key constants

```c
#define KEY_BACKSPACE  0x08
#define KEY_ENTER      0x0D
#define KEY_ESCAPE     0x1B
#define KEY_UP         0x80
#define KEY_DOWN       0x81
#define KEY_LEFT       0x82
#define KEY_RIGHT      0x83
#define KEY_F1         0x90
/* … F2–F7 are KEY_F1 + offset (0x91–0x96) … */
#define KEY_F8         0x97
```

Normal printable ASCII characters are returned as their ASCII values. Special keys return values ≥ `0x80`.

#### Usage pattern — blocking read loop

```c
while (1) {
    int ch = keyboard_getchar();   /* blocks until a key arrives */
    if (ch == KEY_ESCAPE) break;
    if (ch == KEY_ENTER)  { handle_enter(); continue; }
    if (ch >= 0x20 && ch < 0x80) { handle_printable((char)ch); }
}
```

#### Usage pattern — non-blocking poll

```c
int ch;
while (running) {
    ch = keyboard_poll();
    if (ch != -1) handle_key(ch);
    /* do other work here */
}
```

`keyboard_getchar` internally calls `hlt` to yield the CPU while waiting, so it does not busy-spin. `keyboard_poll` never blocks.

---

### 4.4 Timer & PC Speaker

**Header:** `kernel/timer.h`

```c
/* Returns the number of milliseconds elapsed since the PIT was started. */
uint32_t timer_get_ticks(void);

/* Busy-wait for ms milliseconds (uses hlt; requires interrupts enabled). */
void timer_sleep(uint32_t ms);

/* Register a function to be called on every 1 kHz PIT tick, immediately
 * after the tick counter is incremented.  Only one secondary callback is
 * supported; a second call replaces the first.  Pass NULL to unregister.
 * The callback executes inside the IRQ0 handler with interrupts disabled —
 * keep it short (< 1 ms) and do not call timer_sleep() from within it.  */
void timer_register_secondary(void (*cb)(void));

/* PC speaker control */
void speaker_on(uint32_t freq_hz);    /* start a tone at freq_hz */
void speaker_off(void);               /* stop the tone */
void speaker_beep(uint32_t freq_hz, uint32_t duration_ms); /* blocking beep */
void speaker_boot_chime(void);        /* C5→E5→G5→C6 startup arpeggio */

/* Play 8-bit PCM audio through the PC speaker.
 * buf  — pointer to an array of 8-bit unsigned samples.
 * len  — number of samples to play.
 * Rate is SAM_PCM_RATE (22050 Hz).  Blocks until playback is complete.
 * Temporarily reprograms IRQ0 to 22050 Hz and restores it afterwards.
 * Corrects the system tick counter for the elapsed time on return.     */
void speaker_play_pcm(const unsigned char *buf, int len);
```

`timer_sleep` uses `hlt` to suspend the CPU between ticks. **Interrupts must be enabled** (i.e. `sti` must have been called) before calling it. If called with interrupts off it will hang indefinitely.

The PIT is programmed at **1 kHz** — `timer_get_ticks()` returns a millisecond count. The counter is a `uint32_t`, so it wraps after ~49.7 days of uptime.

#### How `speaker_play_pcm` works

The PC speaker is a single-bit device: it is either on or off. To produce arbitrary audio levels, the function uses **PIT channel 2 in mode 0 (one-shot countdown) as a pulse-width modulator**:

```
For each sample s (0–255):
  off_count = (255 − s) × 54 / 255   (clamped to ≥ 1)
  Load off_count into PIT ch2.
  PIT ch2 output is LOW (speaker ON) while counting, HIGH when expired.
  Duty cycle ≈ s / 255.
```

IRQ0 is temporarily reprogrammed to fire at **22050 Hz** (one interrupt per sample). Each IRQ0 invocation loads the next sample's count. The secondary timer callback (`timer_register_secondary`) is suppressed during playback to prevent the USB poll routine from being called 22× faster than intended. The system tick counter is corrected by `len × 1000 / 22050` ms on return so that `timer_sleep()` remains accurate after playback.

---

### 4.5 Interrupt Handling

**Header:** `kernel/isr.h`

#### The registers_t struct

Every ISR or IRQ handler receives a pointer to the CPU state at the time of the interrupt:

```c
typedef struct {
    /* Segment registers saved by isr_common_stub (lowest address first) */
    uint32_t gs, fs, es, ds;

    /* General-purpose registers saved by pusha */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;

    /* Pushed by the ISR stub */
    uint32_t int_no;      /* interrupt / exception number (0–47) */
    uint32_t err_code;    /* CPU error code, or 0 if not applicable */

    /* Pushed automatically by the CPU on exception entry */
    uint32_t eip;         /* address of the faulting instruction */
    uint32_t cs;          /* code segment at time of fault */
    uint32_t eflags;      /* processor flags */
    uint32_t useresp;     /* only valid on privilege change (ring 3→0) */
    uint32_t ss;          /* only valid on privilege change */
} __attribute__((packed)) registers_t;
```

#### Registering a handler

```c
typedef void (*isr_handler_t)(registers_t *regs);

void isr_register_handler(int num, isr_handler_t handler);
```

`num` is the interrupt vector number:

- **0–31** — CPU exceptions (page fault = 14, general protection = 13, etc.)
- **32–47** — Hardware IRQs (IRQ0 = timer = 32, IRQ1 = keyboard = 33, etc.)

```c
static void my_irq_handler(registers_t *regs) {
    (void)regs;
    /* do work */
}

/* Call this during your driver's init function (after irq_init has run): */
isr_register_handler(35, my_irq_handler);   /* IRQ3 = COM2 */
```

Only one handler can be registered per vector. Registering a handler for a vector that already has one silently replaces the old handler.

For **hardware IRQs (vectors 32–47)**, the `irq_common_stub` sends the End-Of-Interrupt (EOI) to the PIC automatically **before** calling your handler. You do not need to send EOI yourself.

For **CPU exceptions (vectors 0–31)**, if you do not register a handler, the kernel's default `isr_dispatch` will print the exception number, error code, and EIP to the screen and halt.

#### IRQ to vector mapping

| IRQ | Vector | Default use |
|-----|--------|-------------|
| 0 | 32 | PIT timer (1 kHz tick) |
| 1 | 33 | PS/2 keyboard |
| 2 | 34 | Cascade (slave PIC) |
| 3 | 35 | COM2 serial port |
| 4 | 36 | COM1 serial port |
| 5 | 37 | LPT2 / Sound Card |
| 6 | 38 | Floppy disk |
| 7 | 39 | LPT1 (spurious) |
| 8 | 40 | RTC |
| 9 | 41 | ACPI / PCI |
| 10 | 42 | Available |
| 11 | 43 | Available |
| 12 | 44 | PS/2 mouse |
| 13 | 45 | FPU coprocessor |
| 14 | 46 | Primary ATA |
| 15 | 47 | Secondary ATA |

---

---

### 4.6 PCI Bus Scanner

**Headers:** `kernel/pci.h`, `kernel/pci.c`

The PCI scanner reads configuration space through the standard x86 port pair 0xCF8 (address) / 0xCFC (data). It scans buses 0–7, devices 0–31, functions 0–7, and provides two public calls used by the USB driver.

```c
/* Search all PCI functions for the first device matching the given
 * class code, subclass, and programming interface.
 *
 * On success writes the bus/device/function triple into *bus_out,
 * *dev_out, *fn_out and returns 1.  Returns 0 if nothing matched.  */
int pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                    uint8_t *bus_out, uint8_t *dev_out, uint8_t *fn_out);

/* Set Command register bits 0 (I/O enable), 1 (memory enable), and
 * 2 (bus-master enable) for the given BDF.  Required before the CPU
 * can issue DMA or port-I/O transactions on behalf of the device.    */
void pci_enable_busmaster(uint8_t bus, uint8_t dev, uint8_t fn);
```

#### PCI class codes for common devices

| Class | Subclass | Prog IF | Device type |
|-------|----------|---------|-------------|
| 0x0C | 0x03 | 0x00 | UHCI USB host controller |
| 0x0C | 0x03 | 0x10 | OHCI USB host controller |
| 0x0C | 0x03 | 0x20 | EHCI USB host controller |
| 0x01 | 0x01 | — | IDE/ATA controller |
| 0x02 | 0x00 | — | Ethernet controller |

#### Reading other config-space registers

If you need to read additional config-space DWORDs (e.g. BARs), use the same port pair:

```c
/* Build a type-1 config-space address */
static inline uint32_t pci_addr(uint8_t bus, uint8_t dev,
                                 uint8_t fn, uint8_t reg) {
    return (1u << 31)
         | ((uint32_t)bus  << 16)
         | ((uint32_t)dev  << 11)
         | ((uint32_t)fn   <<  8)
         | (reg & 0xFC);          /* reg must be DWORD-aligned */
}

/* Example: read BAR0 (offset 0x10) */
outl(0xCF8, pci_addr(bus, dev, fn, 0x10));
uint32_t bar0 = inl(0xCFC);
```

The base I/O address for a port-I/O BAR is `bar0 & ~0x3u` (mask off the type bits).

---

### 4.7 USB HID Keyboard Driver

**Headers:** `kernel/usb.h`, `kernel/usb.c`

The USB driver implements a UHCI (Universal Host Controller Interface) keyboard driver for the boot HID protocol. It is entirely self-contained: it finds the UHCI controller via PCI, owns all UHCI data structures (frame list, Queue Heads, Transfer Descriptors), and feeds decoded keycodes into the shared keyboard ring buffer via `keyboard_inject()`.

#### Public API

```c
/* Locate the UHCI host controller, enumerate the first USB HID keyboard
 * found on ports 1 or 2, and arm the interrupt-endpoint polling loop.
 * Called once by kernel_main after sti.
 * Prints a green confirmation line on success; silent on failure.      */
void usb_keyboard_init(void);
```

That is the only exported symbol. Everything else in `usb.c` is `static`.

#### Enumeration sequence

On a successful init the driver performs this sequence of synchronous control transfers to the device at USB address 0, then address 1:

| Step | Request | Notes |
|------|---------|-------|
| 1 | `SET_ADDRESS(1)` | Moves device off address 0 |
| 2 | `GET_DESCRIPTOR(Device, 18 B)` | Reads `bcdUSB`, `bMaxPacketSize0` |
| 3 | `GET_DESCRIPTOR(Configuration, 64 B)` | Locates the interrupt IN endpoint descriptor |
| 4 | `SET_CONFIGURATION(bConfigurationValue)` | Activates the configuration |
| 5 | `SET_PROTOCOL(0)` — boot protocol | Switches to the fixed 8-byte report format |
| 6 | `SET_IDLE(0, 0)` — report on change only | Optional; STALL is silently ignored |

After step 6 a single interrupt-endpoint TD is armed in the UHCI frame list and the polling loop begins.

#### Polling loop

`timer_register_secondary(usb_poll)` installs `usb_poll` as the 1 kHz tick callback. The callback decrements an 8 ms counter; when it reaches zero it checks whether the interrupt TD has completed (Active bit cleared without error). If a report arrived, it decodes the 8-byte HID boot report and calls `keyboard_inject()` for each pressed key, then re-arms the TD and resets the counter.

#### UHCI data structure alignment

UHCI link pointers store a physical address in bits 31:4 and use bits 3:0 for flags (T = terminate, Q = QH, Vf = depth-first). This means every structure used as a link target — both Queue Heads (`uhci_qh_t`) and Transfer Descriptors (`uhci_td_t`) — **must be 16-byte aligned**. The driver declares these structures with `__attribute__((aligned(16)))`. If you add new QH or TD arrays, observe the same constraint or the controller will decode their addresses incorrectly and transfers will silently fail.

#### HID boot-protocol report format

The keyboard sends 8-byte reports on each key state change:

| Byte | Field | Notes |
|------|-------|-------|
| 0 | Modifier bitmap | Bit 1 = Left Shift, Bit 5 = Right Shift; others ignored |
| 1 | Reserved | Always 0x00 |
| 2–7 | Keycode[0–5] | Up to 6 simultaneously pressed keys (HID Usage IDs) |

The driver translates Usage IDs 0x04–0x52 to ASCII using two tables (`hid_normal[]` and `hid_shift[]`). Keys outside that range, or keycodes 0x01 (Rollover error), are silently dropped.

#### Extending the driver

- **Support more keys:** extend the `hid_normal` and `hid_shift` translation tables in `usb.c`. HID Usage IDs are defined in the *HID Usage Tables* document (USB-IF, usage page 0x07).
- **Support OHCI/EHCI:** call `pci_find_device(0x0C, 0x03, prog_if, ...)` with `prog_if = 0x10` (OHCI) or `0x20` (EHCI). Each controller type has a different register set and TD format; OHCI and EHCI each require a separate driver module.
- **Support multiple devices:** the current driver tracks a single device address (`usb_kbd_addr = 1`). To support multiple keyboards, the enumeration loop would need to probe all ports and assign a unique address to each device.

---

### 4.8 ATAPI CD-ROM Driver

**Headers:** `kernel/cdrom.h`, `kernel/cdrom.c`

The ATAPI driver sends SCSI-style PACKET commands over the standard ATA port pair (`0x1F0` primary, `0x170` secondary). It is independent of `ata.c`: both files probe the same ports, but the ATA driver skips any position where LBA-mid = `0x14` and LBA-high = `0xEB` (the ATAPI signature), while `cdrom.c` looks for exactly those bytes.

#### Public types

```c
#define CDROM_MAX_DRIVES  4           /* Primary M/S + Secondary M/S */
#define CDROM_PRESENT     1
#define CDROM_NOT_PRESENT 0

typedef struct {
    uint8_t  present;    /* CDROM_PRESENT or CDROM_NOT_PRESENT          */
    uint32_t last_lba;   /* highest valid LBA (0 if no disc / READ CAP failed) */
    uint32_t block_size; /* bytes per logical block (normally 2048)     */
} cdrom_drive_t;
```

#### Public API

```c
/* Probe all four IDE positions for ATAPI devices.  Calls READ CAPACITY(10)
 * on each drive found.  Prints a boot status line: "[OK]  (N HDD, M CD-ROM)".
 * Call once from kernel_main, after ata_detect() and after sti.            */
void cdrom_init(void);

/* Return the number of ATAPI drives detected during cdrom_init().           */
int cdrom_count(void);

/* Return a pointer to the internal drives array (CDROM_MAX_DRIVES entries). */
const cdrom_drive_t *cdrom_drives(void);

/* Read one 2048-byte sector from drive idx into buf.
 * lba is a logical block address (0 = first sector of disc).
 * Returns 0 on success, non-zero on error.                                  */
int cdrom_read_sector(uint8_t idx, uint32_t lba, void *buf);

/* Send a START STOP UNIT command with LoEj=1, Start=0 (eject the tray).
 * Returns 0 if the drive accepted the command, non-zero otherwise.
 * Some drives reject software eject; treat non-zero as informational.       */
int cdrom_eject(uint8_t idx);
```

#### ATAPI command sequence

Every command follows the PACKET protocol:

1. Poll `STATUS` (port `base+7`) until `BSY` clears and `DRQ` clears.
2. Write `0xA0` (PACKET command) to the `COMMAND` register.
3. Poll until `DRQ` sets — the drive is ready to receive the CDB.
4. Write the 12-byte SCSI CDB as six 16-bit words to the `DATA` register (`outw` × 6).
5. Poll until `DRQ` sets again — the drive is ready to transfer data.
6. Read the response data (`inw` loop).
7. Poll until `BSY` clears (transfer complete).

| Shell command | SCSI CDB | Notes |
|---|---|---|
| `cdrom_init` per drive | `IDENTIFY PACKET DEVICE` (ATA 0xA1), then `READ CAPACITY(10)` (0x25) | Detects drive; reads `last_lba` and `block_size` |
| `cdrom_read_sector` | `READ(10)` — opcode `0x28` | Reads `block_size` bytes (2048 for standard CDs) |
| `cdrom_eject` | `START STOP UNIT` — opcode `0x1B`, `LoEj=1, Start=0` | Mechanical tray eject |

#### Relationship with `ata.c`

`ata.c` calls `IDENTIFY DEVICE` (ATA `0xEC`). If a drive returns `0x14` in LBA-mid and `0xEB` in LBA-high, it is ATAPI — `ata.c` skips it and it is picked up by `cdrom_init()`. If you probe the same IDE position from both drivers simultaneously, the second probe will see a busy bus. Always call `ata_detect()` before `cdrom_init()` and do not call either concurrently.

#### Adding new SCSI commands

All CDB construction is in `cdrom.c`. To send a new command:

1. Build a 12-byte CDB array: `uint8_t cdb[12] = { opcode, ... };`
2. Issue the PACKET sequence above (steps 1–7).
3. Parse the response according to the SCSI command's data format (see *SCSI MMC-5* specification, freely available from T10.org).

---

### 4.9 InteiliTalk — SAM TTS Subsystem

**Headers:** `shell/sam/sam_phoneme.h`, `shell/sam/sam_render.h`  
**Public entry point:** `shell/talk.h` → `talk_speak(const char *text)`

InteiliTalk produces synthesised speech through the PC speaker using a bare-metal port of **SAM (Software Automatic Mouth)**, the formant speech synthesiser originally shipped with the Commodore 64.

#### File layout

| File | Role |
|------|------|
| `shell/sam/sam_reciter.c` | English text → SAM phoneme notation (`TextToPhonemes`) |
| `shell/sam/sam_phoneme.c` | Phoneme parser chain + PCM pipeline driver (`SAMMain`, `PrepareOutput`) |
| `shell/sam/sam_phoneme.h` | Public API for the parser stage |
| `shell/sam/sam_render.c` | Full SAM formant synthesiser — produces 8-bit PCM (`Render`, `SetMouthThroat`) |
| `shell/sam/sam_render.h` | Public API for the render stage; declares `sam_pcm_buf[]` and `sam_pcm_len` |
| `shell/sam/RenderTabs.h` | Render-side lookup tables (formant data, sinus, rectangle, sampleTable, etc.) — included only by `sam_render.c` |
| `shell/sam/SamTabs.h` | Parser-side lookup tables — included only by `sam_phoneme.c` and `sam_reciter.c` |
| `shell/talk.c` | Shell-facing wrapper: calls the pipeline and then `speaker_play_pcm` |

#### Pipeline

```
talk_speak(text)
    │
    ├─ sam_set_input(text)         — copy text into sam_input[]; pre-fill
    │                                 remainder with '[' (reciter stop char)
    ├─ TextToPhonemes(sam_input)   — English → SAM phoneme notation in-place
    ├─ SAMMain()
    │     ├─ Init()                — SetMouthThroat, sam_render_reset
    │     ├─ Parser1/2, CopyStress, SetPhonemeLength, AdjustLengths, Code41240
    │     ├─ InsertBreath
    │     └─ PrepareOutput()       — split at breath boundaries; call Render()
    │           └─ Render()        — formant synthesis → sam_pcm_buf[]
    │                                 updates sam_pcm_len
    └─ speaker_play_pcm(sam_pcm_buf, sam_pcm_len)
```

#### PCM output buffer

```c
/* Declared in shell/sam/sam_render.h */
#define SAM_PCM_RATE  22050u           /* samples per second */
extern unsigned char sam_pcm_buf[];    /* 8-bit unsigned PCM samples */
extern int           sam_pcm_len;      /* number of valid bytes after SAMMain() */
```

`sam_pcm_buf` is a **131 072-byte (128 KB) static array in BSS** (`shell/sam/sam_render.c`). It costs nothing in the `.text` or `.data` segments. At 22050 Hz it holds up to ~5.9 seconds of audio. The buffer is implicitly zero-initialised at startup and reset by `sam_render_reset()` at the start of each `SAMMain()` call.

#### Voice parameters

These setters must be called **before** `SAMMain()`:

```c
void SetSpeed(unsigned char speed);   /* frames per output cycle; default 72 (higher = slower) */
void SetPitch(unsigned char pitch);   /* fundamental pitch 0–255; default 64                   */
void SetMouth(unsigned char mouth);   /* mouth formant scaling 0–255; default 128               */
void SetThroat(unsigned char throat); /* throat formant scaling 0–255; default 128              */
```

#### Using the synthesiser directly

```c
#include "sam/sam_phoneme.h"
#include "sam/sam_render.h"
#include "../kernel/timer.h"

/* Speak a phrase at a higher pitch */
SetPitch(90);
sam_set_input("Hello from bare metal.");
if (TextToPhonemes(sam_input) && SAMMain())
    speaker_play_pcm(sam_pcm_buf, sam_pcm_len);
```

Or use the convenience wrapper (recommended for shell commands):

```c
#include "talk.h"
talk_speak("Hello from bare metal.");
```

#### The `sam_set_input` pre-fill rule

The reciter (`TextToPhonemes`) copies all 255 bytes of `sam_input[]` into its internal working buffer — it does not stop at the `0x9B` end-of-input marker. If `sam_input[]` contains phoneme notation from a previous call in the bytes past the current text, the reciter re-processes that stale data as if it were new English text and produces garbage speech after the intended phrase.

`sam_set_input()` prevents this by **pre-filling the entire 256-byte buffer with `[`** before writing the text. The `[` character is the reciter's own output-termination signal: when the reciter encounters `[` in its working copy, it immediately writes the `0x9B` terminator to the output and returns. This ensures the reciter stops exactly at the end of the real text regardless of what was in the buffer previously.

> **Rule:** always call `sam_set_input()` to load text. Never write to `sam_input[]` directly.

#### Formant synthesis overview (`sam_render.c`)

`Render()` is a five-step pipeline that converts the parsed phoneme sequence into PCM:

| Step | What it does |
|------|-------------|
| 1 — Frame expansion | Repeats each phoneme's formant parameters for `phonemeLengthOutput[i]` frames into per-frame arrays (`frequency1[]`, `amplitude1[]`, `pitches[]`, etc.) |
| 2 — Transition interpolation | Linearly interpolates formant values across blend regions at phoneme boundaries using `blendRank[]`, `inBlendLength[]`, and `outBlendLength[]` |
| 3 — Pitch contour | Subtracts half F1 from the pitch array to create the natural falling-then-rising pitch contour of English speech (skipped in sing mode) |
| 4 — Amplitude rescaling | Maps raw amplitude values through `amplitudeRescale[]` to approximate a dB-linear response |
| 5 — Synthesis loop | Mixes two sinusoidal formants (F1, F2 via `sinus[]`) and one rectangular formant (F3 via `rectangle[]`), plus sampled consonant bursts from `sampleTable[]`, into 8-bit PCM via `Output8BitAry()` |

`Output8BitAry()` advances an internal cursor (`s_bufferpos`) using the C64 sample-timing `timetable[]` and writes 5-sample blocks into `sam_pcm_buf[]`. The cursor is cumulative across multiple `Render()` calls (one per breath group), so a long utterance with internal pauses accumulates cleanly into one contiguous buffer.

#### Shared globals between `sam_phoneme.c` and `sam_render.c`

The following globals are **defined** in `sam_phoneme.c` and declared `extern` in `sam_render.c`:

| Symbol | Type | Purpose |
|--------|------|---------|
| `A`, `X`, `Y` | `unsigned char` | 6502 register emulators used by the reciter and render |
| `mem39`…`mem56` | `unsigned char` | SAM working registers shared across pipeline stages |
| `speed` | `unsigned char` | Frames per output cycle (default 72) |
| `pitch` | `unsigned char` | Fundamental pitch (default 64) |
| `singmode` | `int` | 1 = skip pitch contour (sing mode) |
| `phonemeIndexOutput[60]` | `unsigned char[]` | Phoneme indices for one breath group |
| `phonemeLengthOutput[60]` | `unsigned char[]` | Frame counts for one breath group |
| `stressOutput[60]` | `unsigned char[]` | Stress levels for one breath group |

These are populated by `PrepareOutput()` in `sam_phoneme.c` and consumed by `Render()` in `sam_render.c`. Do not modify them between `SAMMain()` and the subsequent `speaker_play_pcm()` call.

---

### 4.10 InteiliBASIC Cassette Interface (CSAVE / CLOAD)

**Source file:** `shell/basic.c`  
**Headers used:** `kernel/timer.h`, `kernel/fs.h`, `kernel/keyboard.h`, `kernel/ac97.h`

InteiliBASIC includes a Kansas City Standard (KCS) cassette tape interface. `CSAVE` encodes the in-memory BASIC program as KCS audio and writes it to both the PC speaker and a temporary RIFF/WAV file on disk. `CLOAD` captures audio via the AC'97 driver (`kernel/ac97.c`) — which targets the Crystal CS4281 or compatible AC'97 PCI codec on the HP Vectra VEi8 — and decodes the incoming KCS stream back into the program buffer.

---

#### KCS Encoding

```
Data format: Kansas City Standard (KCS), 1200 baud
  0 bit  =  1 cycle  of 1200 Hz  per bit period (≈ 0.833 ms)
  1 bit  =  2 cycles of 2400 Hz  per bit period (≈ 0.833 ms)
  Frame  =  start(0)  D0 D1 D2 D3 D4 D5 D6 D7  stop(1)  [8N1, LSB first]

Wire sequence for CSAVE:
  1 s leader (2400 Hz) → data bytes → 0.5 s trailer (2400 Hz)
```

Both `speaker_on(1200)` / `speaker_on(2400)` with `timer_sleep(1)` are used for live playback. The 1 ms granularity of `timer_sleep` produces ≈ 1000 baud rather than the canonical 1200 baud; cassette recorders tolerate this deviation without decoding errors.

---

#### WAV File Generation

The WAV file is produced in the static buffer `g_cass_wav[512 KB]` before playback begins:

```
g_cass_wav layout
┌──────────────────────┬─────────────────────────────────────────┐
│  bytes  0 – 43       │  RIFF/WAV header (44 bytes)             │
│  bytes 44 – N        │  8-bit unsigned PCM, mono, 8000 Hz      │
│    samples 44…       │    1 s leader  (8000 samples, 2400 Hz)  │
│    samples …         │    KCS-encoded program bytes            │
│    samples …N        │    0.5 s trailer (4000 samples, 2400 Hz)│
└──────────────────────┴─────────────────────────────────────────┘
```

PCM samples use a square-wave approximation:
- LOW half-cycle → `0x20`  
- HIGH half-cycle → `0xE0`

The WAV header is filled in last (once the PCM length is known) by `cass_build_wav_header(pcm_len)`.

**Buffer capacity:** at 7 samples/bit × 10 bits/byte = 70 samples/program-byte, the 512 KB buffer holds approximately **7 300 bytes of serialised program text** after the leader and trailer overhead. Programs that exceed this limit are played through the PC speaker only (the WAV file step is skipped and a yellow warning is displayed).

---

#### BSS footprint

Two new static arrays are added to `shell/basic.c`:

| Symbol | Size | Purpose |
|--------|------|---------|
| `g_cass_wav[CASSETTE_WAV_MAX]` | 512 KB | WAV PCM scratch buffer |
| `g_cass_pos` | 4 bytes | Write cursor into `g_cass_wav` |

Both are in `.bss` (zero-initialised) and add no cost to the `.data` or binary image size.

---

#### CSAVE flow

```
do_csave()
  │
  ├─ 1. Serialise program → basic_save_buf[]    (reuses existing format)
  │      "<linenum> <text>\n" per line
  │
  ├─ 2. KCS-encode → g_cass_wav[]
  │      g_cass_pos = 44  (skip header)
  │      cass_emit_wave(8000, 2400)             1 s leader
  │      for each byte: cass_emit_wav_byte(b)   8N1 KCS framing
  │      cass_emit_wave(4000, 2400)             0.5 s trailer
  │      cass_build_wav_header(pcm_len)         fill RIFF header
  │
  ├─ 3. fs_write(0, "CASSETTE.WAV", g_cass_wav, g_cass_pos)
  │      → C:\CASSETTE.WAV on the HDD
  │      (warns and skips if WAV buffer overflowed or HDD write fails)
  │
  ├─ 4. UI: "Press RECORD … then press ENTER"
  │      Waits for Enter (ESC cancels)
  │
  ├─ 5. PC speaker playback
  │      speaker_on(2400); timer_sleep(1000)    1 s leader
  │      for each byte: cass_speak_byte(b)      8N1 via speaker_on + timer_sleep(1)
  │      speaker_on(2400); timer_sleep(500)     0.5 s trailer
  │      speaker_off()
  │      Progress dot printed every 64 bytes
  │
  └─ 6. "Save complete! Deleting the temporary wav file from the HDD…"
         fs_write(0, "CASSETTE.WAV", &zero, 1)  overwrite → effectively erase
         (fs has no delete call; overwriting is the closest equivalent)
```

---

#### CLOAD flow and AC'97 capture hook

```
do_cload()
  │
  ├─ 1. UI: "Press PLAY on your tape recorder"
  │      Waits for Enter (ESC cancels)
  │
  ├─ 2. 10-second listen loop (100 ms tick, ESC abort, spinner animation)
  │
  └─ 3. === AC'97 audio capture (kernel/ac97.c) ===
         ┌──────────────────────────────────────────────────────────┐
         │  if (ac97_has_signal()) {                                │
         │      do_new();                                           │
         │      int b;                                              │
         │      while ((b = ac97_capture_kcs_byte()) >= 0)         │
         │          basic_save_buf[prog_len++] = (uint8_t)b;       │
         │      /* parse lines from basic_save_buf (→ insert_line) */│
         │      got_signal = 1; break;                              │
         │  }                                                       │
         └──────────────────────────────────────────────────────────┘
         On timeout without signal: yellow "No signal detected" message.
```

---

#### AC'97 audio driver (kernel/ac97.c)

`CLOAD` uses `kernel/ac97.c` / `kernel/ac97.h` to capture audio from the
HP Vectra VEi8's AC'97 codec.  The driver exposes two functions:

```c
/* Returns 1 if the AC'97 capture stream has audio above the noise floor
 * on the default capture source (microphone / line-in).                 */
int ac97_has_signal(void);

/* Blocks until one KCS-framed byte is received and decoded, or until a
 * timeout elapses (~2 s per byte at 1200 baud).
 * Returns the byte value (0–255) on success, -1 on framing error,
 * or -2 on timeout / dropout.                                           */
int ac97_capture_kcs_byte(void);
```

**PCI detection** (in priority order):

| Priority | Match | Hardware |
|----------|-------|----------|
| 1st | Vendor `0x1013` / Device `0x4281` | Crystal Semiconductor CS4281 |
| 2nd | Class `0x04` / Subclass `0x01` | Generic AC'97 audio controller |

The CS4281 is the codec shipped in the HP Vectra VEi8.  Generic AC'97
controllers (class 0x04 / subclass 0x01) are also accepted as a fallback
for other mid-1990s PCI audio cards.

**Capture pipeline:**

```
Microphone/line-in
  → AC'97 codec ADC (8 kHz / 8-bit / mono, or 48 kHz fallback)
  → AC-Link serial bus (48 kHz frame clock, slot 3 = left ADC)
  → CS4281 DMA engine (channel B, auto-increment, 4 KB ping-pong ring)
  → g_capture_buf[] (static BSS, 128-byte aligned)
  → ac97_has_signal() / ac97_capture_kcs_byte() (KCS decoder)
```

**Variable Rate Audio (VRA):** if the AC'97 codec advertises VRA support
(Extended Audio ID register bit 0), the driver programmes the ADC rate
register to 8000 Hz for exact KCS timing.  Without VRA the codec runs at
48 kHz; the KCS zero-crossing decoder remains functional but the
samples-per-bit ratio shifts from 7 to ~40 — the decoder's tolerance
accommodates this.

**HDA note:** `kernel/hda.c` / `kernel/hda.h` remain in the tree for
reference and for machines that use Intel HDA (post-2004 hardware).
`do_cload()` in `shell/basic.c` uses the AC'97 path exclusively;
`hda.c` is compiled but its symbols are not called at runtime on the
HP Vectra VEi8.  To switch a port of inteilidOS to an HDA machine,
replace the `ac97_has_signal()` / `ac97_capture_kcs_byte()` calls in
`do_cload()` and the `#include "../kernel/ac97.h"` with their HDA
counterparts.

---

---

### 4.11 LaunchPad & Program Loader

**Headers:** `kernel/loader.h`, `shell/launchpad.h`  
**Sources:** `kernel/loader.c`, `shell/launchpad.c`

LaunchPad is the HP port's external program manager. It reads real filesystems from attached hardware and transfers control to external programs that run as ordinary C functions inside the kernel's flat-32 address space.

#### Architecture overview

```
launchpad_run()                         shell/launchpad.c
    │
    ├── iso9660_read_dir()  or  fat12_read_dir()    ← list files
    │       kernel/iso9660.c          kernel/fat12.c
    │
    ├── iso9660_read_file() or  fat12_read_file()   ← load file into RAM
    │           → destination: 0x00500000 (lp_load_buf)
    │
    └── loader_exec_elf()   or  loader_exec()       ← validate + run
                kernel/loader.c
                  │
                  └── fn()   ← calls entry point; blocks until program returns
```

LaunchPad keeps a local mutable copy of the ATAPI drive table and calls `cdrom_rescan_media()` on source switches and manual rescans (F2/F3) so that discs inserted after boot are detected without a reboot.

#### Supported media

| Source | Driver | Filesystem |
|--------|---------|------------|
| CD-ROM 0–3 | `kernel/cdrom.c` (ATAPI PACKET) | ISO 9660, Mode 1 / Mode 2 Form 1 |
| Floppy A: | `kernel/fdc.c` (82077AA FDC) | FAT12 (1.44 MB, 18 sectors/track) |

#### Supported program formats

| Format | Magic | Description |
|--------|-------|-------------|
| **IPGM** | `IPGM` at offset 0 | inteiliDOS native flat binary — 16-byte header + flat 32-bit code |
| **ELF32** | `\x7fELF` at offset 0 | Standard System V i386 ELF executable (`ET_EXEC`, `EM_386`) |

LaunchPad reads the first 4 bytes after loading to identify the format, then dispatches to the appropriate loader function.

#### IPGM header format

```
Offset   Size   Field         Notes
0        4      magic         "IPGM" (0x4D475049)
4        2      version       must be 1
6        4      entry_offset  byte offset from start of file to entry function
10       4      load_addr     preferred physical load address (0 → 0x00500000)
14       2      reserved      0
16+             code & data   flat 32-bit protected-mode binary
```

`loader_exec()` validates the magic and version, adds `entry_offset` to `load_addr` to obtain the entry physical address, and calls it as `void fn(void)`.

#### ELF32 loading

`loader_exec_elf()` walks the program header table:

1. Reject if class ≠ ELFCLASS32, type ≠ ET_EXEC, or machine ≠ EM_386.
2. For each `PT_LOAD` segment: copy `p_filesz` bytes from `file_buf + p_offset` to `(uint8_t *)p_paddr`; zero the `[p_filesz, p_memsz)` tail (BSS).
3. Call `(void (*)(void))e_entry`.

Because no paging is active, virtual and physical addresses are identical. Link programs at 0x00500000 or higher.

#### Writing a LaunchPad-compatible ELF program

A minimal bare-metal program:

```c
/* entry.c — linked at 0x00500000 */
#include <stdint.h>

/* inteiliDOS flat-32 flat selectors — already loaded by the kernel GDT */
#define KCS  0x08   /* kernel code segment */
#define KDS  0x10   /* kernel data segment */

/* Mask / unmask IRQ1 (keyboard) so the kernel handler doesn't race us */
static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0,%1" :: "a"(v),"Nd"(p));
}

void my_entry(void) {
    /* Mask IRQ1 so we own the keyboard port exclusively */
    uint8_t mask = __builtin_ia32_inb(0x21);
    outb(0x21, mask | 0x02);

    /* Switch to our own 16 KB BSS stack */
    static uint8_t my_stack[16384];
    uint32_t new_esp = (uint32_t)(my_stack + sizeof(my_stack));
    uint32_t old_esp;
    __asm__ volatile (
        "mov %%esp, %0\n"
        "mov %1, %%esp\n"
        : "=r"(old_esp) : "r"(new_esp)
    );

    /* ── your program here ── */

    /* Restore kernel stack and re-enable IRQ1 */
    __asm__ volatile ("mov %0, %%esp\n" :: "r"(old_esp));
    outb(0x21, mask & ~0x02u);
}
```

Linker script (`linker_launchpad.ld`):

```ld
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(my_entry)

SECTIONS {
    . = 0x00500000;
    .text   ALIGN(4K) : { *(.text*)   }
    .rodata ALIGN(4K) : { *(.rodata*) }
    .data   ALIGN(4K) : { *(.data*)   }
    .bss    ALIGN(4K) : { *(COMMON) *(.bss*) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
```

Build and package as a plain ISO 9660 data disc (no GRUB needed):

```bash
xorriso -as mkisofs -o myprogram.iso -input-charset utf-8 \
    -rational-rock -joliet myprogram.elf
```

LaunchPad reads the root directory for `.ELF` files and runs them directly.

> **Three complete example games** are available in `programs for inteiliDOS/`:  
> `Apocalypse/` — VGA raycaster (uses Mode 13h in standalone, text mode via LaunchPad)  
> `DungeonsOfDoom/` — D&D text-mode RPG with PC-speaker sound effects  
> `OdysseyOfHAL/` — 2001: A Space Odyssey CYOA with Daisy Bell PC-speaker ending

#### ISO 9660 reader API

**Header:** `kernel/iso9660.h`

```c
#define ISO9660_MAX_FILES 64
#define ISO9660_NAME_MAX  32

typedef struct {
    char     name[ISO9660_NAME_MAX]; /* filename, version suffix (";1") stripped */
    uint32_t lba;                    /* start sector on disc                      */
    uint32_t size;                   /* file size in bytes                        */
    uint8_t  is_dir;                 /* 1 = directory entry                       */
} iso9660_dirent_t;

/* Read the root directory of the disc in drive_index.
 * Returns entry count (>= 0), or -1 on error (no disc, bad PVD). */
int iso9660_read_dir(uint8_t drive_index,
                     iso9660_dirent_t out[ISO9660_MAX_FILES]);

/* Read an arbitrary subdirectory by LBA and byte length.
 * Pass the lba/size fields from a directory's dirent entry.
 * "." and ".." are skipped automatically. */
int iso9660_read_dir_at(uint8_t drive_index,
                         uint32_t dir_lba, uint32_t dir_size,
                         iso9660_dirent_t out[ISO9660_MAX_FILES]);

/* Find a file in the root directory (case-insensitive, version-suffix-ignored). */
int iso9660_find_file(uint8_t drive_index, const char *name,
                      iso9660_dirent_t *out);

/* Read up to max_bytes of a file into buf.
 * Returns bytes read, or -1 on I/O error. */
int32_t iso9660_read_file(uint8_t drive_index, const iso9660_dirent_t *ent,
                           uint8_t *buf, uint32_t max_bytes);
```

The reader parses the Primary Volume Descriptor at sector 16, locates the root directory extent, and calls `cdrom_read_sector()` for all sector I/O. No Rock Ridge, no Joliet extensions, no path table — root and subdirectory traversal only.

#### FAT12 reader API

**Header:** `kernel/fat12.h`

```c
#define FAT12_MAX_FILES  64
#define FAT12_NAME_MAX   13    /* 8.3 + NUL */
#define FAT12_ATTR_DIRECTORY  0x10

typedef struct {
    char     name[FAT12_NAME_MAX]; /* 8.3 filename, null-terminated */
    uint32_t size;                 /* file size in bytes (0 for dirs) */
    uint16_t first_cluster;        /* first data cluster (>= 2)       */
    uint8_t  attr;                 /* directory entry attribute byte   */
} fat12_dirent_t;

/* Read the root directory of floppy drive_index (0 = A:).
 * Returns entry count, or -1 on I/O error / no disk. */
int fat12_read_dir(uint8_t drive_index,
                   fat12_dirent_t out[FAT12_MAX_FILES]);

/* Read a subdirectory starting at first_cluster. */
int fat12_read_subdir(uint8_t drive_index, uint16_t first_cluster,
                      fat12_dirent_t out[FAT12_MAX_FILES]);

/* Read up to max_bytes of a file's data.
 * Returns bytes read, or -1 on I/O error. */
int32_t fat12_read_file(uint8_t drive_index, const fat12_dirent_t *ent,
                         uint8_t *buf, uint32_t max_bytes);
```

All sector I/O goes through `fdc_read_sector()`. The FAT12 chain is walked cluster-by-cluster; multi-sector clusters are reassembled into a contiguous buffer.

---

## 5. The IntelliShell Application Model

IntelliShell is a synchronous single-threaded REPL. "Applications" in inteiliDOS are simply C functions that the shell calls, run to completion, and return. There is no process creation, no `fork`, no `exec`. When your function returns, the shell prompt reappears.

### Adding a Shell Command

All commands live in `shell/commands.c`. If your command is simple, add it there. For large applications (like IEdit or InteiliBASIC), create a new source file and add it to `CMakeLists.txt`.

**Step 1 — Write the handler function**

```c
/*
 * CMD_GREET — print a personalised greeting
 * Usage: GREET <name>
 */
static int cmd_greet(int argc, const char *argv[]) {
    if (argc < 1) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_puts("Usage: GREET <name>\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return 1;
    }
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_printf("  Hello, %s! Welcome to inteiliDOS.\n", argv[0]);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    return 0;
}
```

- `argc` is the number of whitespace-separated tokens **after** the command name. The command name itself is not in `argv`.
- `argv[0]` is the first argument, `argv[1]` the second, and so on.
- Return **0** for success, non-zero for an error. The shell currently ignores the return value but this may be used by scripting in the future.
- Always restore colour to `VGA_COLOR_WHITE` on `VGA_COLOR_BLACK` before returning.

**Step 2 — Register it in the dispatcher**

Find the `command_dispatch` function (near the bottom of `commands.c`) and add one line:

```c
if (kstrcmp(cmd, "GREET") == 0) return cmd_greet(argc, argv);
```

Commands are case-normalised to uppercase by the shell before dispatch, so `GREET`, `greet`, and `Greet` all reach the same handler. You can also add aliases by chaining conditions:

```c
if (kstrcmp(cmd, "GREET") == 0 || kstrcmp(cmd, "HI") == 0)
    return cmd_greet(argc, argv);
```

**Step 3 — Add a HELP entry**

Find `cmd_help()` and add a line in the appropriate category section:

```c
println("  GREET    - Print a greeting for the given name");
```

**Step 4 — (Optional) Add it to a new source file**

If your command is large enough to deserve its own file, create `shell/myapp.c` and `shell/myapp.h`, then add the source to `CMakeLists.txt`:

```cmake
set(SHELL_C_SOURCES
    shell/shell.c
    shell/commands.c
    shell/iedit.c
    shell/tour.c
    shell/basic.c
    shell/sheets.c
    shell/talk.c
    shell/myapp.c        # ← add here
)
```

---

### Adding an NLP Phrase

The NLP translator in `commands.c` maps plain-English input to canonical commands. Add new entries to the `nlp[]` table inside `nlp_translate()`:

```c
struct { const char *pattern; const char *replacement; } nlp[] = {
    /* existing entries … */
    {"say hello",    "GREET"},     /* ← new entry */
    {NULL, NULL}
};
```

Matching is case-insensitive substring search — the pattern just needs to appear somewhere in the user's input. Phrases that take arguments (like `MKDIR`) need custom handling below the table because the argument must be extracted from the input string.

---

### The Universal `quit` Convention

Every interactive application in inteiliDOS should recognise the word **`quit`** typed at any line-input prompt as a signal to exit and return to IntelliShell. The mechanism differs per application because each owns its own event loop:

| Application | How `quit` is detected |
|---|---|
| **IntelliShell** | `QUIT` and `EXIT` are registered commands; the NLP table also maps the phrase `"quit"` |
| **InteiliBASIC** | Checked at the REPL dispatch level (`kstrcmp(cmd, "QUIT") == 0`) |
| **TOUR** | `q`/`Q` at any choice prompt immediately returns `S_QUIT`; since `'q'` exits on the first character, the full word `quit` is covered automatically |
| **InteiliTalk** | Line-input loop compares the entered string against `"quit"` and `"exit"` before passing it to the speech engine |
| **InteiliSheets** | `sh_edit_cell()` intercepts `"quit"` before committing any cell value and returns signal code `1`; `sheets_run()` breaks on that code |
| **IEdit** | After `op_insert_char()`, checks if the entire current line (length == 4, case-insensitive) equals `"quit"`; erases the word and breaks the event loop |

When implementing a new interactive application, add the following check to your line-input handler before acting on the input:

```c
/* Universal quit — return to IntelliShell */
if ((input[0]=='q'||input[0]=='Q') &&
    (input[1]=='u'||input[1]=='U') &&
    (input[2]=='i'||input[2]=='I') &&
    (input[3]=='t'||input[3]=='T') &&
     input[4]=='\0') {
    break;
}
```

For character-by-character event loops (like IEdit), check the accumulated line content after each `op_insert_char()` call instead of checking a completed readline.

---

### Writing a Full-Screen Application

Full-screen applications (like IEdit) take over the entire 80×25 VGA display. The pattern is:

```c
int cmd_myapp(int argc, const char *argv[]) {
    (void)argc; (void)argv;

    /* 1. Save shell state if needed, then clear the screen */
    vga_clear();

    /* 2. Draw initial UI directly into the VGA buffer */
    draw_ui();

    /* 3. Event loop */
    int running = 1;
    while (running) {
        int ch = keyboard_getchar();   /* blocking — CPU halts until key */
        switch (ch) {
            case KEY_ESCAPE: running = 0; break;
            /* … handle other keys … */
        }
        redraw();
    }

    /* 4. Restore the screen for the shell on exit */
    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    return 0;
}
```

For direct pixel-level control of the VGA buffer:

```c
volatile uint16_t *vbuf = (volatile uint16_t *)0xB8000;

/* Write character c with given colour at (row, col) */
void put_at(int row, int col, char c, vga_color_t fg, vga_color_t bg) {
    uint8_t attr = (uint8_t)((bg << 4) | (fg & 0x0F));
    vbuf[row * 80 + col] = (uint16_t)((attr << 8) | (uint8_t)c);
}
```

Move the hardware cursor separately — it is not updated by direct buffer writes:

```c
vga_set_cursor(cursor_row, cursor_col);
```

---

## 6. Build System

### Build Targets — Modern vs Legacy

`build.sh` prompts you to choose a target every time it runs. You can also pass `--modern` or `--legacy` to skip the prompt.

| | Modern | Legacy |
|---|---|---|
| **Flag** | `./build.sh --modern` | `./build.sh --legacy` |
| **CPU target** | `i686` — Pentium Pro and later | `i486` — 486DX / Pentium / Pentium MMX |
| **Optimisation** | `-O2` | `-O1` (avoids P6-specific scheduling) |
| **GRUB config** | `grub/grub.cfg` | `grub/grub_legacy.cfg` |
| **Primary ISO** | `build_modern/inteilidOS.iso` | `build_legacy/inteilidOS_legacy.iso` |
| **Extra output** | — | `build_legacy/inteilidOS_floppy.img` (1.44 MB) |
| **QEMU CPU** | default (`qemu64`) | `-cpu pentium` |
| **QEMU targets** | `run`, `run-iso`, `run-debug` | `run`, `run-legacy`, `run-floppy` |

Build directories are separate (`build_modern/` vs `build_legacy/`) so both can coexist without cleaning.

CMake receives `BUILD_TARGET` as a cache variable (`-DBUILD_TARGET=modern|legacy`). To configure manually without `build.sh`:

```bash
cmake -S . -B build_modern \
    -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf.cmake \
    -DBUILD_TARGET=modern -DCMAKE_BUILD_TYPE=Release
cmake --build build_modern -j$(nproc)
```

### Prerequisites

| Tool | Version | Purpose |
|---|---|---|
| `i686-elf-gcc` | GCC 12+ recommended | C cross-compiler targeting bare-metal i686 |
| `i686-elf-ld` | comes with binutils | Linker |
| `nasm` | 2.15+ | Assembles the boot stubs |
| `cmake` | 3.18+ | Build orchestration |
| `xorriso` | any recent | Creates the `.iso` image |
| `grub-mkrescue` | GRUB 2 | Packages GRUB into the ISO |
| `qemu-system-i386` | any | Optional — for testing |

### Compiler flags (set in `CMakeLists.txt`)

Flags common to both targets:

```
-m32                  target 32-bit x86 (IA-32)
-ffreestanding        no standard library assumptions
-fno-builtin          do not replace calls with compiler builtins
-fno-stack-protector  no __stack_chk_guard (not available freestanding)
-fno-pic              no position-independent code
-fno-pie              no position-independent executable
-nostdlib             do not link the C standard library
-Wall -Wextra         full warning set
-std=gnu11            C11 with GNU extensions
```

Target-specific flags (controlled by `BUILD_TARGET`):

| Flag | Modern (`i686`) | Legacy (`i486`) |
|---|---|---|
| `-march` | `-march=i686` — allows Pentium Pro instructions (CMOV, etc.) | `-march=i486` — safe for 486DX, Pentium, Pentium MMX; **no CMOV** |
| `-O` | `-O2` — standard optimisation | `-O1` — avoids P6 instruction-scheduling that crashes pre-P6 CPUs |

> Do **not** add `-msse`, `-msse2`, or any floating-point flags. The FPU/SSE state is not saved across interrupts, which would corrupt state silently. This applies to both build targets.

### Adding source files

Open `CMakeLists.txt` and find the source lists:

```cmake
set(KERNEL_C_SOURCES
    kernel/kernel.c
    kernel/vga.c
    kernel/gdt.c
    kernel/idt.c
    kernel/isr.c
    kernel/timer.c
    kernel/keyboard.c
    kernel/pci.c
    kernel/usb.c
    kernel/ata.c
    kernel/cdrom.c
    kernel/memory.c
    # HP Vectra VEi8 additions:
    kernel/ac97.c          # AC'97 audio driver (Crystal CS4281) for CLOAD
    kernel/fdc.c           # Floppy disk controller (82077AA)
    kernel/fat12.c         # FAT12 filesystem reader
    kernel/iso9660.c       # ISO 9660 filesystem reader
    kernel/loader.c        # IPGM + ELF32 program loader
    kernel/hda.c           # Intel HDA reference driver (compiled, not called at runtime)
)

set(SHELL_C_SOURCES
    shell/shell.c
    shell/commands.c
    shell/iedit.c
    shell/tour.c
    shell/basic.c
    shell/sheets.c
    shell/talk.c
    shell/sam/sam_phoneme.c
    shell/sam/sam_reciter.c
    shell/sam/sam_render.c
    # HP Vectra VEi8 additions:
    shell/launchpad.c      # LaunchPad program manager
    shell/filemanager.c    # InteiliFile Manager
    shell/setup.c          # OS installation wizard
    tetris/tetris.c        # Tetris game (Korobeiniki BGM)
)
```

Add your file to the appropriate list. Both lists compile into the same static library (`kernel_c`) and are linked together. The distinction between `KERNEL_C_SOURCES` and `SHELL_C_SOURCES` is organisational only — there is no binary-level difference.

> **Both build targets share the same source lists.** `BUILD_TARGET` controls compiler flags and boot sector selection, not which C files are compiled. If your code uses features not available on `i486` (such as CMOV), guard it with `#ifdef __i686__` or avoid it entirely so the legacy build continues to work.

### Link order

The linker processes files in this order:

1. ASM objects: `boot.o`, `gdt_flush.o`, `idt_load.o`, `isr_stubs.o`
2. All C objects from `kernel_c` static library (kernel + shell sources)

The kernel entry symbol is `_start` (in `boot.asm`), which GRUB calls directly. Everything else is reachable by following the call graph from `_start`.

### Build commands

```bash
cd inteiliDOS

# Interactive (asks Modern or Legacy):
./build.sh

# Non-interactive:
./build.sh --modern    # → build_modern/inteilidOS.iso
./build.sh --legacy    # → build_legacy/inteilidOS_legacy.iso
                       #   build_legacy/inteilidOS_floppy.img

# Or manually with CMake (modern example):
cmake -S . -B build_modern \
    -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf.cmake \
    -DBUILD_TARGET=modern -DCMAKE_BUILD_TYPE=Release
cmake --build build_modern -j$(nproc)

# CMake convenience run targets:
cmake --build build_modern -- run-iso     # modern ISO in QEMU
cmake --build build_legacy -- run-legacy  # legacy ISO, -cpu pentium
cmake --build build_legacy -- run-floppy  # floppy image, -cpu pentium
cmake --build build_modern -- run-debug   # waits for GDB on port 1234

# Direct QEMU (modern):
qemu-system-i386 -cdrom build_modern/inteilidOS.iso -m 128

# Direct QEMU (legacy, Pentium CPU):
qemu-system-i386 -cdrom build_legacy/inteilidOS_legacy.iso -cpu pentium -m 64

# Run with serial output (useful for debugging):
qemu-system-i386 -cdrom build_modern/inteilidOS.iso -m 128 -serial stdio

# Run and attach GDB:
qemu-system-i386 -cdrom build_modern/inteilidOS.iso -m 128 -s -S &
i686-elf-gdb build_modern/inteilidOS.elf \
    -ex "target remote :1234" \
    -ex "break kernel_main" \
    -ex "continue"
```

### Inspecting the binary

```bash
# List all symbols with addresses (modern build):
i686-elf-nm -n build_modern/inteilidOS.elf

# Disassemble a specific function:
i686-elf-objdump -d build_modern/inteilidOS.elf | grep -A 40 "<my_function>:"

# Dump section sizes:
i686-elf-size build_modern/inteilidOS.elf

# Verify the legacy binary contains no i686-only instructions (CMOV etc.):
i686-elf-objdump -d build_legacy/inteilidOS.elf | grep -i "cmov\|fcmov" \
    && echo "WARNING: i686 instructions in legacy binary" || echo "OK"

# See the memory map (what address each object ends up at):
i686-elf-ld --print-map ... (or inspect build_modern/inteilidOS.map if generated)
```

---

## 7. Compiler Constraints & Gotchas

These are the non-obvious rules that will bite you if you ignore them.

### No 64-bit arithmetic

`i686-elf-gcc` without `libgcc` cannot emit 64-bit division (`__udivmoddi4`) or 64-bit multiply helpers. **Use `uint32_t` / `int32_t` for all arithmetic.** If you need 64-bit values for storage (e.g. disk LBA addresses), break operations into 32-bit halves manually.

```c
/* WRONG — may generate __udivmoddi4 call → linker error */
uint64_t a = 0x100000000ULL / some_value;

/* RIGHT */
uint32_t a_hi = 0x1;
uint32_t a_lo = 0x0;
/* … manual 32-bit long division … */
```

### No floating point

The FPU is never initialised — `CR0.EM` is set, and GCC emits an implicit `#NM` check whenever it generates FPU instructions. **Do not use `float` or `double`.** For percentages or rates, use integer fixed-point (e.g. multiply by 100 before dividing).

The `clts` instruction in `kernel_main` clears `CR0.TS` (which the BIOS sometimes leaves set) but does **not** initialise the FPU state. Even with TS clear, using FPU instructions will produce meaningless results because the FPU registers are uninitialised.

### No variable-length arrays (VLAs)

VLAs call `__alloca` and interact poorly with the stack protector (which is disabled) and GCC's stack management in freestanding mode. Always declare local arrays with a compile-time constant size.

```c
/* WRONG */
void foo(int n) { char buf[n]; }

/* RIGHT */
#define BUF_MAX 256
void foo(int n) { char buf[BUF_MAX]; (void)n; }
```

### No `printf` — use `vga_printf`

There is no `printf`, `sprintf`, or `snprintf` from libc. The kernel provides `vga_printf` which supports: `%s`, `%c`, `%d` (signed decimal), `%u` (unsigned decimal), `%x` (lowercase hex). There is no `%f`, `%e`, `%g`, `%p`, `%l`, or field-width specifiers. For anything more complex, format into a manual buffer using the `k`-string utilities.

### Static allocation preferred over heap

The 2 MB heap is shared by every command and application that runs. Prefer static arrays (declared at file scope or `static` inside a function) for large fixed-size buffers. This avoids fragmentation and makes memory usage predictable.

```c
/* Preferred for large fixed buffers */
static char editor_lines[200][80];

/* Fine for small, short-lived allocations */
char *tmp = kmalloc(64);
/* … use tmp … */
kfree(tmp);
```

### Interrupts and the `sti` / `cli` window

The kernel runs with interrupts enabled after `kernel_main` issues `sti`. If you write code that must not be interrupted (e.g. a critical section updating shared state between a foreground function and an IRQ handler), bracket it with `cli` / `sti`:

```c
__asm__ volatile ("cli");
/* critical section */
__asm__ volatile ("sti");
```

Do not leave interrupts disabled for more than a few microseconds — the keyboard buffer and timer tick will stall.

### Port I/O

Access hardware registers via port I/O, not memory-mapped I/O (unless you know the device's specific address). Use inline assembly:

```c
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
```

Both functions are already defined as static inlines in `kernel/keyboard.c` and `kernel/timer.c` — you can copy the pattern into your own source file.

---

## 8. Interrupt & Exception Reference

### CPU Exceptions (vectors 0–31)

| Vector | Mnemonic | Name | Error code? |
|--------|----------|------|-------------|
| 0 | #DE | Divide Error | No |
| 1 | #DB | Debug | No |
| 2 | — | NMI | No |
| 3 | #BP | Breakpoint | No |
| 4 | #OF | Overflow | No |
| 5 | #BR | Bound Range Exceeded | No |
| 6 | #UD | Invalid Opcode | No |
| 7 | #NM | Device Not Available (FPU) | No |
| 8 | #DF | Double Fault | Yes (always 0) |
| 10 | #TS | Invalid TSS | Yes |
| 11 | #NP | Segment Not Present | Yes |
| 12 | #SS | Stack-Segment Fault | Yes |
| 13 | #GP | General Protection Fault | Yes |
| 14 | #PF | Page Fault | Yes |
| 16 | #MF | x87 FPU Error | No |
| 17 | #AC | Alignment Check | Yes |
| 18 | #MC | Machine Check | No |

Unregistered exceptions are caught by the default `isr_dispatch` handler, which prints the exception number, error code, and faulting EIP to the screen in red and halts the CPU.

### Error code for #GP (vector 13)

The error code for a General Protection Fault encodes which segment selector caused the fault:

```
Bits 15–3  Selector index (GDT entry number)
Bit 2      TI flag: 0 = GDT, 1 = LDT
Bit 1      IDT flag: 1 = from IDT gate
Bit 0      EXT: 1 = external event (hardware)
```

An error code of `0` means the fault was not caused by a specific segment.

---

## 9. Coding Conventions

The existing codebase follows these conventions. New code should match them.

### Style

- **Indentation:** 4 spaces. No tabs.
- **Braces:** K&R style — opening brace on the same line as the control statement.
- **Line length:** Aim for 80 characters; hard limit of 100.
- **Function names:** `lower_snake_case`. Kernel internal functions are `static`. Public API functions match their header declarations exactly.
- **Constants and macros:** `UPPER_SNAKE_CASE`.
- **Types:** Use `stdint.h` fixed-width types (`uint8_t`, `uint32_t`, etc.) for anything where the bit width matters. Use `size_t` for sizes and counts. Avoid plain `int` for hardware values.

### Headers

Every `.c` file has a matching `.h` file. Headers use include guards:

```c
#ifndef MY_MODULE_H
#define MY_MODULE_H

/* declarations */

#endif /* MY_MODULE_H */
```

Headers include only what they need. Implementation-private types and helpers stay in the `.c` file as `static`.

### Error handling

There are no exceptions, no `errno`, and no signal mechanism. Functions that can fail return a value: `NULL` for pointer-returning functions, a non-zero integer for `int`-returning commands. Callers must check return values.

Fatal unrecoverable errors should halt the CPU:

```c
vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
vga_puts("FATAL: heap corrupted\n");
__asm__ volatile ("cli; hlt");
for (;;) {}  /* silence compiler "control reaches end of non-void function" */
```

### Comments

Comment the *why*, not the *what*. Assume the reader can read C. Hardware-specific sequences (port I/O, descriptor encoding) should have brief comments explaining the register layout or protocol reference.

---

## 10. Roadmap & Contribution Areas

These are the open tasks most likely to be useful to new contributors, roughly ordered by difficulty.

### Beginner

- **More NLP phrases** — extend the `nlp[]` table in `nlp_translate()` with more plain-English synonyms for existing commands. No hardware knowledge required.
- **InteiliBASIC BEEP statement** — `shell/basic.c`, `exec_stmt()`: the `BEEP` case is a no-op. Wire in `speaker_beep(freq_hz, duration_ms)` from `kernel/timer.h`. A simple `BEEP` could use a fixed 800 Hz / 250 ms tone; an extended `BEEP freq, dur` variant is optional.
- **New shell commands** — `ECHO`, `SET`, `VER`, `PAUSE`, simple calculator (`CALC 2 + 3`). All ring-0 C code, no driver knowledge needed. Remember to add `quit` detection if your command has an interactive input loop (see §5 — The Universal `quit` Convention).
- **HELP improvements** — more detailed per-command help text; a `HELP <command>` mode that shows extended usage.
- **InteiliBASIC extensions** — `LOCATE row, col` (calls `vga_set_cursor`), `COLOR fg, bg` (calls `vga_set_color`), `SCREEN` (clear), `BEEP freq, dur` (calls `speaker_beep` — the API already exists in `timer.h` and is used by InteiliTalk; only the BASIC statement itself is a stub).
- **LaunchPad BASIC loader** — when a `.BAS` file is selected in LaunchPad, offer to load it into InteiliBASIC's program buffer and open the REPL automatically. The BASIC `LOAD` path in `shell/basic.c` already accepts a source buffer; LaunchPad just needs to call `fat12_read_file` / `iso9660_read_file` and hand the bytes to an exposed `basic_load_buf()` entry point.

### Intermediate

- **ATA disk I/O** — PIO-mode sector reads and writes. Drive detection already works (`ata_detect()` in `kernel/ata.c`); the next step is `ata_read_sector()` / `ata_write_sector()` using the ATA PIO data phase. IRQ14 (vector 46) is reserved for the primary ATA channel. Needed for persistent file I/O in IEdit and BASIC `SAVE`/`LOAD`.
- **PS/2 mouse driver** — IRQ12 (vector 44). Useful for a graphical cursor in full-screen apps.
- **RTC driver** — IRQ8 (vector 40). Read the CMOS RTC to provide a real date and time for `DATE` and `TIME`.
- **COM1 serial output** — initialise UART at port `0x3F8`, wire it to `vga_puts` for a debug output channel. Invaluable for low-level debugging via QEMU's `-serial stdio`.
- **LaunchPad write-back for floppy** — `fat12.c` currently supports read-only access. Adding `fat12_write_file()` (FAT12 chain allocation + sector writes via `fdc_write_sector`) would let LaunchPad save files to floppy — useful for IEdit or BASIC `CSAVE` output.
- **LaunchPad programme list improvements** — show a DEMO preview for IPGM programs (read a metadata section after the 16-byte header) and colour-code file rows by type (programs in green, BASIC in cyan, text in white).

### Advanced

- **Persistent file system** — design a simple flat filesystem on top of the ATA disk I/O driver. Wire it into the existing simulated directory structure in `commands.c`. The ATAPI CD-ROM ISO 9660 reader and FAT12 floppy reader already exist as structural references for filesystem reader code.
- **Multitasking scheduler** — a round-robin pre-emptive scheduler using the PIT IRQ0. Requires a per-task `registers_t`-equivalent context block, a task switch on each tick, and separate stacks per task. This is the single biggest architectural change possible.
- **OHCI/EHCI USB keyboard** *(UHCI already implemented)* — extend `kernel/usb.c` to support OHCI (prog_if 0x10) and EHCI (prog_if 0x20) host controllers. See §4.7 for data-structure alignment requirements. Also planned: detect and gracefully handle USB device hot-unplug mid-session.
- **Network stack** — NE2000 or RTL8139 Ethernet driver (IRQ10 or IRQ11), ARP, IP, UDP.
- **VGA graphics mode** — switch the display into a planar 640×480 16-colour graphics mode (INT 10h mode 0x12 via VESA, or direct VGA register programming). Enables a graphical shell or bitmap rendering.
- **LaunchPad ISO writer** — add a `FORMAT ISO` command path that calls `cdrom_write_sector()` (once a writable CD-RW / DVD-RW driver exists) to produce LaunchPad-compatible discs from files in the virtual filesystem.
- **Legacy build: driver validation** — the legacy build targets real 1990s hardware. Before adding new drivers for AC'97, FDC, or ISO 9660, verify them under QEMU with `-cpu pentium` (or `-cpu 486`) to catch any i686-only instructions early. The FDC driver is particularly sensitive to timing; real hardware may require longer settle delays than QEMU.

---

```
inteiliDOS Developer Reference
Version 1.0 — Inteilix Software Corporation

"The future still has a blinking cursor."
```
