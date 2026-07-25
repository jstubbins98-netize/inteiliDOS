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
5. [The IntelliShell Application Model](#5-the-intellishell-application-model)
   - [Adding a Shell Command](#adding-a-shell-command)
   - [Adding an NLP Phrase](#adding-an-nlp-phrase)
   - [Writing a Full-Screen Application](#writing-a-full-screen-application)
6. [Build System](#6-build-system)
7. [Compiler Constraints & Gotchas](#7-compiler-constraints--gotchas)
8. [Interrupt & Exception Reference](#8-interrupt--exception-reference)
9. [Coding Conventions](#9-coding-conventions)
10. [Roadmap & Contribution Areas](#10-roadmap--contribution-areas)

---

## 1. Architecture Overview

inteiliDOS is a flat, single-address-space, single-privilege-level (ring 0) operating system. There is no kernel/user split, no virtual memory, no process scheduler, and no system-call boundary. Every piece of code — kernel, shell, editor, BASIC interpreter — runs as part of one monolithic binary at ring 0 with full hardware access.

```
┌─────────────────────────────────────────────┐
│              IntelliShell REPL              │  shell/shell.c
├──────────────┬──────────────────────────────┤
│  Commands    │  Applications                │  shell/commands.c
│  (30+ cmds)  │  IEdit │ InteiliBASIC │ TOUR │  shell/iedit.c, basic.c, tour.c
├──────────────┴──────────────────────────────┤
│               Kernel Services               │
│  VGA │ Keyboard │ Timer │ Memory │ ISR/IRQ  │  kernel/*.c
├─────────────────────────────────────────────┤
│           Protected-Mode Stubs              │  boot/isr_stubs.asm
│           GDT / IDT / TSS                  │  kernel/gdt.c, idt.c
├─────────────────────────────────────────────┤
│              Boot Entry (_start)            │  boot/boot.asm
└─────────────────────────────────────────────┘
              Real x86 Hardware / QEMU
```

**Key facts:**

- Target architecture: **IA-32 (i686)**, 32-bit protected mode, flat segments.
- Compiled with: `i686-elf-gcc` cross-compiler, `-ffreestanding -nostdlib -O2`.
- No standard library. No `libc`, no `libm`, no `libgcc` soft-float helpers.
- No dynamic allocation of descriptors; all major tables (GDT, IDT, ISR handler array) are static fixed-size arrays.
- The entire OS — kernel + shell + all applications — links into a **single ELF binary** (`inteilidOS.elf`) and is packaged into a bootable ISO via GRUB.

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

/* PC speaker control */
void speaker_on(uint32_t freq_hz);    /* start a tone at freq_hz */
void speaker_off(void);               /* stop the tone */
void speaker_beep(uint32_t freq_hz, uint32_t duration_ms); /* blocking beep */
void speaker_boot_chime(void);        /* C5→E5→G5→C6 startup arpeggio */
```

`timer_sleep` uses `hlt` to suspend the CPU between ticks. **Interrupts must be enabled** (i.e. `sti` must have been called) before calling it. If called with interrupts off it will hang indefinitely.

The PIT is programmed at **1 kHz** — `timer_get_ticks()` returns a millisecond count. The counter is a `uint32_t`, so it wraps after ~49.7 days of uptime.

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

```
-m32                  target 32-bit x86
-march=i686           allow i686 instructions (no SSE, no MMX)
-ffreestanding        no standard library assumptions
-fno-builtin          do not replace calls with compiler builtins
-fno-stack-protector  no __stack_chk_guard (not available freestanding)
-fno-pic              no position-independent code
-fno-pie              no position-independent executable
-nostdlib             do not link the C standard library
-O2                   optimise; the kernel is small enough for -O2 safely
```

Do **not** add `-msse`, `-msse2`, or any floating-point flags. The FPU/SSE state is not saved across interrupts, which would corrupt state silently.

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
    kernel/memory.c
    kernel/ata.c
)

set(SHELL_C_SOURCES
    shell/shell.c
    shell/commands.c
    shell/iedit.c
    shell/tour.c
    shell/basic.c
)
```

Add your file to the appropriate list. Both lists compile into the same static library (`kernel_c`) and are linked together, so there is no practical distinction between "kernel" and "shell" at the binary level — it is organisational only.

### Link order

The linker processes files in this order:

1. ASM objects: `boot.o`, `gdt_flush.o`, `idt_load.o`, `isr_stubs.o`
2. All C objects from `kernel_c` static library (kernel + shell sources)

The kernel entry symbol is `_start` (in `boot.asm`), which GRUB calls directly. Everything else is reachable by following the call graph from `_start`.

### Build commands

```bash
cd inteiliDOS
./build.sh             # full clean build + ISO creation

# Or manually with CMake:
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/i686-elf.cmake
make -j$(nproc)

# Run in QEMU:
qemu-system-i386 -cdrom build/inteilidOS.iso -m 128

# Run with serial output (useful for debugging):
qemu-system-i386 -cdrom build/inteilidOS.iso -m 128 -serial stdio

# Run and attach GDB:
qemu-system-i386 -cdrom build/inteilidOS.iso -m 128 -s -S &
i686-elf-gdb build/inteilidOS.elf -ex "target remote :1234" -ex "break kernel_main" -ex "continue"
```

### Inspecting the binary

```bash
# List all symbols with addresses:
i686-elf-nm -n build/inteilidOS.elf

# Disassemble a specific function:
i686-elf-objdump -d build/inteilidOS.elf | grep -A 40 "<my_function>:"

# Dump section sizes:
i686-elf-size build/inteilidOS.elf

# See the memory map (what address each object ends up at):
i686-elf-ld --print-map ... (or inspect build/inteilidOS.map if generated)
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
- **New shell commands** — `ECHO`, `SET`, `VER`, `PAUSE`, simple calculator (`CALC 2 + 3`). All ring-0 C code, no driver knowledge needed.
- **HELP improvements** — more detailed per-command help text; a `HELP <command>` mode that shows extended usage.
- **InteiliBASIC extensions** — `LOCATE row, col` (calls `vga_set_cursor`), `COLOR fg, bg` (calls `vga_set_color`), `SCREEN` (clear), `BEEP freq, dur` (calls `speaker_beep`).

### Intermediate

- **PC speaker BEEP in BASIC** — wire `speaker_beep(freq_hz, duration_ms)` into the InteiliBASIC `BEEP` statement, which currently does nothing. The speaker API already exists in `timer.h`.
- **ATA/IDE disk driver** — PIO-mode reads and writes to an IDE drive. IRQ14 (vector 46) is reserved for the primary ATA channel. See the ATA PIO spec at OSDev Wiki. Needed for persistent file I/O in IEdit and BASIC `SAVE`/`LOAD`.
- **PS/2 mouse driver** — IRQ12 (vector 44). Useful for a graphical cursor in full-screen apps.
- **RTC driver** — IRQ8 (vector 40). Read the CMOS RTC to provide a real date and time for `DATE` and `TIME`.
- **COM1 serial output** — initialise UART at port `0x3F8`, wire it to `vga_puts` for a debug output channel. Invaluable for low-level debugging via QEMU's `-serial stdio`.

### Advanced

- **Persistent file system** — design a simple flat filesystem (FAT12 or a custom layout) on top of the ATA driver. Wire it into the existing simulated directory structure in `commands.c`.
- **Multitasking scheduler** — a round-robin pre-emptive scheduler using the PIT IRQ0. Requires a per-task `registers_t`-equivalent context block, a task switch on each tick, and separate stacks per task. This is the single biggest architectural change possible.
- **USB HID keyboard** — replace `kernel/keyboard.c` with a UHCI/OHCI driver for machines without a PS/2 port. Requires PCI enumeration first.
- **Network stack** — NE2000 or RTL8139 Ethernet driver (IRQ10 or IRQ11), ARP, IP, UDP. The `PING` and `NETWORK` commands are currently stubs waiting for this.
- **VGA graphics mode** — switch the display into a planar 640×480 16-colour graphics mode (INT 10h mode 0x12 via VESA, or direct VGA register programming). Enables a graphical shell or bitmap rendering.

---

```
inteiliDOS Developer Reference
Version 1.0 — Inteilix Software Corporation

"The future still has a blinking cursor."
```
