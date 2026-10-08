/*
 * inteilidOS -- shell/tour.c
 * "A Tour of inteiliDOS and Your Computer"
 *
 * A text-based choose-your-own-adventure game.
 * You have been shrunk to the size of an ant and digitized into
 * the computer. Explore the hardware and inteiliDOS to find the
 * Escape Key and get out.
 *
 * Win condition:  reach the Keyboard with ITEM_CLOCK + ITEM_BOOT.
 * Optional items: ITEM_RAM, ITEM_BIOS (required to enter MBR),
 *                 ITEM_PIXEL, ITEM_IRQ.
 */

#include "tour.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Items                                                               */
/* ------------------------------------------------------------------ */
#define ITEM_CLOCK   (1u << 0)   /* CPU clock crystal   (from CPU)  */
#define ITEM_RAM     (1u << 1)   /* RAM chip fragment   (from RAM)  */
#define ITEM_BIOS    (1u << 2)   /* BIOS master key     (from BIOS) */
#define ITEM_BOOT    (1u << 3)   /* Boot sector map     (from MBR)  */
#define ITEM_PIXEL   (1u << 4)   /* Pixel brush         (from VGA)  */
#define ITEM_IRQ     (1u << 5)   /* IRQ-1 ticket        (from PIC)  */

/* Visit / state flags */
#define FLAG_CPU_DONE   (1u << 0)
#define FLAG_RAM_DONE   (1u << 1)
#define FLAG_BIOS_DONE  (1u << 2)
#define FLAG_VGA_DONE   (1u << 3)
#define FLAG_PIC_DONE   (1u << 4)
#define FLAG_SHELL_DONE (1u << 5)
#define FLAG_CACHE_LOST (1u << 6)  /* already got lost in cache once */

/* Scene IDs */
typedef enum {
    S_INTRO = 0,
    S_PSU,
    S_MOTHERBOARD,
    S_CPU,
    S_CPU_CACHE,
    S_RAM,
    S_BIOS,
    S_MBR,
    S_KERNEL,
    S_VGA,
    S_PIC,
    S_SHELL_SCENE,
    S_KEYBOARD,
    S_WIN,
    S_QUIT
} scene_t;

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */
static uint32_t g_items;
static uint32_t g_flags;

/* ------------------------------------------------------------------ */
/* Output helpers                                                      */
/* ------------------------------------------------------------------ */

/* Print s in color fg (background always black), restore to normal after */
static void color(vga_color_t fg, const char *s) {
    vga_set_color(fg, VGA_COLOR_BLACK);
    vga_puts(s);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

/* Thin separator line */
static void sep(void) {
    color(VGA_COLOR_DARK_GREY,
          "\n  --------------------------------------------------------\n");
}

/* Scene header: clears screen and prints a bold title bar */
static void scene_header(const char *title, vga_color_t title_fg) {
    vga_clear();
    vga_set_color(VGA_COLOR_BLACK, title_fg);
    /* fill 80 chars */
    vga_puts("                                                                                ");
    /* rewind to start of that row by clearing and reprinting -- easier to
       just put text over it */
    vga_set_color(VGA_COLOR_BLACK, title_fg);
    /* We can't rewind easily; print title on next line and separator */
    vga_set_color(VGA_COLOR_BLACK, title_fg);
    vga_puts("  ");
    vga_puts(title);
    vga_puts("  \n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    sep();
}

/* Wait for any key */
static void any_key(void) {
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_puts("\n  [ Press any key... ]  ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    keyboard_getchar();
    vga_putchar('\n');
}

/* Read a valid choice from '1'..'0'+max (ignores arrow keys / special) */
static char get_choice(int max) {
    vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
    vga_puts("\n  Your choice > ");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    for (;;) {
        int raw = keyboard_getchar();
        if (raw == 'q' || raw == 'Q') {
            vga_putchar('Q');
            vga_putchar('\n');
            return 'Q';
        }
        if (raw >= '1' && raw <= '0' + max) {
            vga_putchar((char)raw);
            vga_putchar('\n');
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return (char)raw;
        }
    }
}

/* Show the player's current inventory */
static void show_inventory(void) {
    sep();
    color(VGA_COLOR_LIGHT_CYAN, "  INVENTORY: ");
    if (g_items == 0) {
        color(VGA_COLOR_DARK_GREY, "(empty)\n");
        return;
    }
    vga_putchar('\n');
    if (g_items & ITEM_CLOCK)  color(VGA_COLOR_BROWN,       "    [*] CPU Clock Crystal\n");
    if (g_items & ITEM_RAM)    color(VGA_COLOR_LIGHT_GREEN,   "    [*] RAM Chip Fragment\n");
    if (g_items & ITEM_BIOS)   color(VGA_COLOR_LIGHT_MAGENTA, "    [*] BIOS Master Key\n");
    if (g_items & ITEM_BOOT)   color(VGA_COLOR_LIGHT_CYAN,    "    [*] Boot Sector Map\n");
    if (g_items & ITEM_PIXEL)  color(VGA_COLOR_LIGHT_RED,     "    [*] Pixel Brush\n");
    if (g_items & ITEM_IRQ)    color(VGA_COLOR_LIGHT_BLUE,    "    [*] IRQ-1 Ticket\n");
}

/* ------------------------------------------------------------------ */
/* Scenes                                                              */
/* ------------------------------------------------------------------ */

/* ---- INTRO ---- */
static scene_t do_intro(void) {
    vga_clear();
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    vga_puts("                                                                                ");
    vga_puts("   A TOUR OF INTEILIDNOS AND YOUR COMPUTER   -- An Interactive Adventure --   \n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    sep();

    color(VGA_COLOR_WHITE,
    "\n"
    "  The lab smells of ozone and burnt coffee. Professor Kleinberg looks up\n"
    "  from his keyboard, eyes wide behind his thick glasses.\n"
    "\n"
    "  \"The digitizer works!\" he shouts.  But his elbow catches the power\n"
    "  strip -- CLICK -- and the reversal beam goes dark.\n"
    "\n"
    "  The last thing you see before the white light swallows you:\n"
    "  the professor scrambling for a screwdriver.\n");

    any_key();

    color(VGA_COLOR_BROWN,
    "\n"
    "  You regain consciousness in a deafening roar of fans.\n"
    "  You are the size of an ant.  You are INSIDE a computer.\n"
    "\n");
    color(VGA_COLOR_LIGHT_CYAN,
    "  Your mission: explore the machine, learn its secrets, and find\n"
    "  the ONE key that will send you home -- the ESCAPE key on the\n"
    "  physical keyboard.\n\n");
    color(VGA_COLOR_DARK_GREY,
    "  Controls: type the NUMBER next to a choice and press Enter.\n"
    "            Type Q at any prompt to quit.\n\n");

    any_key();
    return S_PSU;
}

/* ---- POWER SUPPLY UNIT ---- */
static scene_t do_psu(void) {
    scene_header("  POWER SUPPLY UNIT  (PSU)  ", VGA_COLOR_DARK_GREY);

    color(VGA_COLOR_WHITE,
    "\n"
    "  You materialize inside a metal box the size of a warehouse.\n"
    "  Rows of cylindrical CAPACITORS tower over you like redwood trees,\n"
    "  their tops striped in brown, black, and gold bands.\n"
    "\n"
    "  A transformer hums at a frequency you can feel in your teeth.\n"
    "  Three rivers of glowing energy snake along the floor:\n");
    color(VGA_COLOR_BROWN,        "    - The 12V rail (yellow) -- powers drives and fans\n");
    color(VGA_COLOR_LIGHT_RED,     "    - The  5V rail (red)    -- feeds most logic chips\n");
    color(VGA_COLOR_LIGHT_GREY,    "    - The 3.3V rail (grey)  -- feeds RAM and PCIe slots\n");
    color(VGA_COLOR_WHITE,
    "\n"
    "  The ATX connector -- a 24-pin plastic cliff -- looms to the north.\n"
    "  Beyond it you can see the vast green plain of the MOTHERBOARD.\n"
    "\n"
    "  A faded warning label reads: 240V INSIDE. NO USER-SERVICEABLE PARTS.\n"
    "  (The irony of your situation is not lost on you.)\n");

    sep();
    color(VGA_COLOR_LIGHT_GREEN, "  What do you do?\n\n");
    color(VGA_COLOR_WHITE,       "  1. Touch the 12V rail (it looks interesting)\n");
    color(VGA_COLOR_WHITE,       "  2. Head through the ATX connector to the Motherboard\n");

    char c = get_choice(2);
    if (c == 'Q') return S_QUIT;

    if (c == '1') {
        color(VGA_COLOR_LIGHT_RED,
        "\n  Bad idea.  12 volts at 18 amps launches you across the PSU like\n"
        "  a pinball.  You bounce off a heatsink, ricochet off a fan blade,\n"
        "  and land, singed but alive, right where you started.\n"
        "\n  The capacitors seem to be laughing.\n");
        any_key();
        return S_PSU;
    }

    color(VGA_COLOR_LIGHT_CYAN,
    "\n  You squeeze through the ATX connector, sliding past 24 pins\n"
    "  humming with regulated DC.  Beyond the plastic housing...\n"
    "  the Motherboard stretches to the horizon.\n");
    any_key();
    return S_MOTHERBOARD;
}

/* ---- MOTHERBOARD ---- */
static scene_t do_motherboard(void) {
    scene_header("  MOTHERBOARD  ", VGA_COLOR_GREEN);

    color(VGA_COLOR_WHITE,
    "\n"
    "  The motherboard is a city seen from above.\n"
    "  Copper TRACE highways carry signals at near light-speed.\n"
    "  VIA holes pierce the board like manhole covers.\n"
    "  Surface-mount resistors and capacitors dot the landscape\n"
    "  like tiny apartment blocks.\n"
    "\n"
    "  You can see four destinations from here:\n");

    int choices = 0;
    /* Always available */
    if (!(g_flags & FLAG_CPU_DONE)) {
        choices++;
        color(VGA_COLOR_BROWN,
        "  1. The CPU socket -- a massive Land Grid Array to the north.\n"
        "     Heat rises from it in shimmering waves.\n");
    } else {
        color(VGA_COLOR_DARK_GREY,
        "  1. [CPU -- already explored]\n");
    }

    if (!(g_flags & FLAG_RAM_DONE)) {
        choices++;
        color(VGA_COLOR_LIGHT_GREEN,
        "  2. The RAM slots -- four long trenches filled with green PCBs.\n");
    } else {
        color(VGA_COLOR_DARK_GREY,
        "  2. [RAM -- already explored]\n");
    }

    if (!(g_flags & FLAG_BIOS_DONE)) {
        choices++;
        color(VGA_COLOR_LIGHT_MAGENTA,
        "  3. The BIOS chip -- a small black DIP package near the coin battery.\n");
    } else {
        color(VGA_COLOR_DARK_GREY,
        "  3. [BIOS -- already explored]\n");
    }

    /* MBR requires BIOS key */
    if (g_items & ITEM_BIOS) {
        choices++;
        color(VGA_COLOR_LIGHT_CYAN,
        "  4. The SATA trace leading to the boot drive -- and the MBR.\n"
        "     (Your BIOS key glows, granting access.)\n");
    } else {
        color(VGA_COLOR_DARK_GREY,
        "  4. [MBR corridor -- LOCKED.  You need the BIOS Master Key.]\n");
    }

    show_inventory();

    sep();
    color(VGA_COLOR_LIGHT_GREEN, "  Where do you go?\n");

    char c = get_choice(4);
    if (c == 'Q') return S_QUIT;

    if (c == '1') return S_CPU;
    if (c == '2') return S_RAM;
    if (c == '3') return S_BIOS;
    if (c == '4') {
        if (g_items & ITEM_BIOS) return S_MBR;
        color(VGA_COLOR_LIGHT_RED,
        "\n  The trace is sealed behind a firmware lock.  You need the BIOS\n"
        "  Master Key to pass.  Try the BIOS chip first.\n");
        any_key();
        return S_MOTHERBOARD;
    }
    return S_MOTHERBOARD;
}

/* ---- CPU ---- */
static scene_t do_cpu(void) {
    scene_header("  CPU  --  Intel 80386 Core  ", VGA_COLOR_BROWN);

    color(VGA_COLOR_WHITE,
    "\n"
    "  The CPU die is a silicon continent.  You walk across its surface\n"
    "  and beneath your feet, 275,000 transistors switch on and off\n"
    "  at 33 MHz -- 33 MILLION times per second.  The air crackles.\n"
    "\n"
    "  Each transistor is a gate: it opens and closes to pass or block\n"
    "  a tiny electric current.  ONE or ZERO.  TRUE or FALSE.\n"
    "  Everything the computer does -- every letter, every number,\n"
    "  every decision -- reduces to those two states.\n"
    "\n"
    "  The ALU (Arithmetic Logic Unit) glows orange ahead: a district\n"
    "  of adders, shifters, and comparators performing billions of\n"
    "  operations per second.\n"
    "\n"
    "  Near the edge of the die you spot something extraordinary:\n");
    color(VGA_COLOR_BROWN,
    "  a CRYSTAL OSCILLATOR, vibrating at exactly 14.318 MHz.\n"
    "  This is the CLOCK -- the heartbeat of the entire machine.\n"
    "  The PLL multiplies it up to the CPU's operating frequency.\n"
    "\n");
    color(VGA_COLOR_LIGHT_CYAN,
    "  A tiny shard of the crystal has worked itself loose.\n"
    "  It pulses with a faint blue light.\n");

    sep();
    color(VGA_COLOR_LIGHT_GREEN, "  What do you do?\n\n");
    color(VGA_COLOR_WHITE,       "  1. Pick up the clock crystal shard\n");
    color(VGA_COLOR_WHITE,       "  2. Venture deeper -- into the L1 Cache\n");
    color(VGA_COLOR_WHITE,       "  3. Return to the Motherboard\n");

    char c = get_choice(3);
    if (c == 'Q') return S_QUIT;

    if (c == '1') {
        g_items |= ITEM_CLOCK;
        color(VGA_COLOR_BROWN,
        "\n  You pocket the clock crystal shard.  It vibrates at 14.318 MHz\n"
        "  against your palm -- a perfect metronome for your escape.\n");
        color(VGA_COLOR_LIGHT_GREEN, "\n  [Got: CPU Clock Crystal]\n");
        g_flags |= FLAG_CPU_DONE;
        any_key();
        return S_MOTHERBOARD;
    }
    if (c == '2') return S_CPU_CACHE;
    return S_MOTHERBOARD;
}

/* ---- CPU CACHE ---- */
static scene_t do_cpu_cache(void) {
    scene_header("  L1 / L2 CACHE  ", VGA_COLOR_BROWN);

    color(VGA_COLOR_WHITE,
    "\n"
    "  The cache is a maze of ultra-fast SRAM cells -- six times faster\n"
    "  than main RAM, but tiny: only 8 KB of L1 data cache.\n"
    "\n"
    "  The corridors here shift.  A cache LINE is 32 bytes wide;\n"
    "  if the CPU needs data that isn't here it triggers a CACHE MISS\n"
    "  and the whole line is fetched from RAM -- painfully slow.\n"
    "\n");

    if (!(g_flags & FLAG_CACHE_LOST)) {
        color(VGA_COLOR_LIGHT_RED,
        "  You take a wrong turn at the associativity boundary and\n"
        "  the cache evicts your spatial position.  You spin for what\n"
        "  feels like 200 clock cycles -- an eternity at 33 MHz.\n"
        "\n"
        "  Eventually a context switch reschedules your location and\n"
        "  you pop back out onto the CPU die, disoriented but unharmed.\n");
        g_flags |= FLAG_CACHE_LOST;
    } else {
        color(VGA_COLOR_WHITE,
        "  This time you map the cache lines carefully.  The FIFO\n"
        "  replacement policy is predictable once you know the pattern.\n"
        "  You find the exit before the next eviction cycle.\n");
    }

    color(VGA_COLOR_DARK_GREY,
    "\n  (There's nothing to collect here -- just the hard-won knowledge\n"
    "  that cache-friendly code matters.)\n");

    any_key();
    return S_CPU;
}

/* ---- RAM ---- */
static scene_t do_ram(void) {
    scene_header("  RAM  --  Dynamic RAM Banks  ", VGA_COLOR_LIGHT_GREEN);

    color(VGA_COLOR_WHITE,
    "\n"
    "  The RAM DIMM is a skyscraper district.\n"
    "  Each cell is a capacitor paired with a transistor:\n"
    "  charged = 1, discharged = 0.\n"
    "\n"
    "  The smell of electricity is stronger here.  Every few milliseconds\n"
    "  a REFRESH pulse sweeps through and recharges every cell --\n"
    "  otherwise the capacitors would drain and FORGET EVERYTHING.\n"
    "  This is why RAM is called VOLATILE memory.\n"
    "\n"
    "  You can see the memory map laid out on the floor like city blocks:\n");
    color(VGA_COLOR_DARK_GREY,    "    0x00000 - 0x9FFFF  Conventional memory (640 KB)\n");
    color(VGA_COLOR_LIGHT_RED,    "    0xA0000 - 0xBFFFF  VGA framebuffer (mapped here!)\n");
    color(VGA_COLOR_LIGHT_MAGENTA,"    0xC0000 - 0xFFFFF  BIOS ROM / expansion ROMs\n");
    color(VGA_COLOR_LIGHT_GREEN,  "    0x100000 +         Extended memory (your kernel lives here)\n");
    color(VGA_COLOR_WHITE,
    "\n"
    "  Wedged between two capacitor rows you find a loose chip:\n");
    color(VGA_COLOR_LIGHT_GREEN,
    "  a DRAM chip fragment, still warm, address pins intact.\n");

    sep();
    color(VGA_COLOR_LIGHT_GREEN, "  What do you do?\n\n");
    color(VGA_COLOR_WHITE,       "  1. Pick up the RAM chip fragment\n");
    color(VGA_COLOR_WHITE,       "  2. Peer into the Extended Memory region\n");
    color(VGA_COLOR_WHITE,       "  3. Return to the Motherboard\n");

    char c = get_choice(3);
    if (c == 'Q') return S_QUIT;

    if (c == '1') {
        g_items |= ITEM_RAM;
        color(VGA_COLOR_LIGHT_GREEN,
        "\n  You slip the chip into your pocket.  It hums faintly as\n"
        "  it keeps refreshing its own internal state.  You understand\n"
        "  now why it needs power to remember anything.\n");
        color(VGA_COLOR_LIGHT_GREEN, "\n  [Got: RAM Chip Fragment]\n");
        g_flags |= FLAG_RAM_DONE;
        any_key();
        return S_MOTHERBOARD;
    }
    if (c == '2') {
        color(VGA_COLOR_LIGHT_CYAN,
        "\n  You peer into extended memory.  In the distance, glowing like\n"
        "  a city at night, is a block of code:\n"
        "\n");
        color(VGA_COLOR_DARK_GREY,
        "    0x100000:  [inteiliDOS kernel image]\n"
        "    Stack frames, ISR tables, the VGA buffer pointer,\n"
        "    the shell's command dispatch table...\n"
        "\n");
        color(VGA_COLOR_WHITE,
        "  Somewhere in those bytes is the TOUR command.\n"
        "  Somewhere in those bytes is YOU.\n"
        "  The thought makes your head spin.\n");
        any_key();
        return S_RAM;
    }
    return S_MOTHERBOARD;
}

/* ---- BIOS ---- */
static scene_t do_bios(void) {
    scene_header("  BIOS  --  Basic Input/Output System  ", VGA_COLOR_LIGHT_MAGENTA);

    color(VGA_COLOR_WHITE,
    "\n"
    "  The BIOS chip is ancient by silicon standards.\n"
    "  Etched into its ROM are the very first instructions the CPU\n"
    "  runs when power is applied: the Power-On Self Test (POST).\n"
    "\n"
    "  Hieroglyphs cover the walls -- but on closer inspection they are\n"
    "  interrupt vectors, carved in stone (well, ROM):\n");
    color(VGA_COLOR_DARK_GREY,
    "\n"
    "    INT 0x10  --  Video services (print characters, set mode)\n"
    "    INT 0x13  --  Disk services  (read sectors from the drive)\n"
    "    INT 0x15  --  Memory map     (tell the OS where RAM is)\n"
    "    INT 0x16  --  Keyboard       (read a key from the BIOS buffer)\n"
    "\n");
    color(VGA_COLOR_WHITE,
    "  A CMOS coin battery the size of a moon glows in the corner,\n"
    "  keeping the real-time clock and BIOS settings alive even when\n"
    "  the machine is powered off.\n"
    "\n"
    "  On a stone altar in the centre of the chamber sits a glowing key.\n"
    "  A plaque beneath it reads:\n");
    color(VGA_COLOR_LIGHT_MAGENTA,
    "    BIOS MASTER KEY -- grants authority to load the bootloader.\n"
    "    The CPU presents this to the MBR to begin the boot sequence.\n");

    sep();
    color(VGA_COLOR_LIGHT_GREEN, "  What do you do?\n\n");
    color(VGA_COLOR_WHITE,       "  1. Take the BIOS Master Key from the altar\n");
    color(VGA_COLOR_WHITE,       "  2. Read the POST log scrolling on the south wall\n");
    color(VGA_COLOR_WHITE,       "  3. Return to the Motherboard\n");

    char c = get_choice(3);
    if (c == 'Q') return S_QUIT;

    if (c == '1') {
        g_items |= ITEM_BIOS;
        color(VGA_COLOR_LIGHT_MAGENTA,
        "\n  You lift the key.  It is lighter than expected.\n"
        "  It carries a faint smell of burnt silicon and decades of\n"
        "  POST beep codes.  The MBR corridor will open for you now.\n");
        color(VGA_COLOR_LIGHT_GREEN, "\n  [Got: BIOS Master Key]\n");
        g_flags |= FLAG_BIOS_DONE;
        any_key();
        return S_MOTHERBOARD;
    }
    if (c == '2') {
        color(VGA_COLOR_DARK_GREY,
        "\n"
        "  BIOS POST LOG (read-only, carved into ROM):\n"
        "\n"
        "    Testing CPU...      OK\n"
        "    Testing FPU...      OK\n"
        "    Testing RAM...      OK (128 MB found)\n"
        "    Initialising PIC... OK (IRQs remapped to 0x20/0x28)\n"
        "    Initialising PIT... OK (IRQ0 @ 100 Hz)\n"
        "    Testing keyboard... OK (IRQ1)\n"
        "    Detecting drives... OK (1 virtual IDE drive)\n"
        "    Loading bootloader  OK (MBR at LBA 0)\n"
        "\n");
        color(VGA_COLOR_WHITE,
        "  At the bottom, in smaller letters:\n");
        color(VGA_COLOR_DARK_GREY,
        "    'If you can read this, you are either the BIOS or\n"
        "     very, very small.'\n");
        any_key();
        return S_BIOS;
    }
    return S_MOTHERBOARD;
}

/* ---- MASTER BOOT RECORD ---- */
static scene_t do_mbr(void) {
    scene_header("  MBR  --  Master Boot Record  ", VGA_COLOR_LIGHT_CYAN);

    color(VGA_COLOR_WHITE,
    "\n"
    "  You slide down a SATA cable into the drive and emerge in a\n"
    "  cramped 512-byte corridor.  This is the MASTER BOOT RECORD --\n"
    "  the very first sector of the disk.\n"
    "\n"
    "  The walls glow with machine code.  You can read a plaque:\n");
    color(VGA_COLOR_DARK_GREY,
    "\n"
    "    Bytes   0 - 445  : Bootstrap code  (GRUB Stage 1)\n"
    "    Bytes 446 - 461  : Partition entry 1\n"
    "    Bytes 462 - 477  : Partition entry 2\n"
    "    Bytes 478 - 493  : Partition entry 3\n"
    "    Bytes 494 - 509  : Partition entry 4\n"
    "    Bytes 510 - 511  : Boot signature  0x55 0xAA\n"
    "\n");
    color(VGA_COLOR_WHITE,
    "  At the far end of the corridor, the bytes 0x55 and 0xAA blaze\n"
    "  like neon signs -- the magic number that tells the BIOS\n"
    "  'this sector is bootable.'  Without them, nothing loads.\n"
    "\n"
    "  GRUB Stage 1 code scurries around you, loading Stage 1.5 from\n"
    "  the sectors immediately after the MBR.  It moves fast --\n"
    "  it has only 446 bytes to work with.\n"
    "\n");
    color(VGA_COLOR_LIGHT_CYAN,
    "  Pinned to the wall by a tiny GRUB logo is a hand-drawn map.\n"
    "  It shows the exact path Stage 2 takes to load the kernel,\n"
    "  including the address where inteiliDOS is placed in memory\n"
    "  and the layout of the entire filesystem.\n"
    "  This could be useful.\n");

    sep();
    color(VGA_COLOR_LIGHT_GREEN, "  What do you do?\n\n");
    color(VGA_COLOR_WHITE,       "  1. Take the Boot Sector Map\n");
    color(VGA_COLOR_WHITE,       "  2. Follow GRUB Stage 2 into the kernel\n");
    color(VGA_COLOR_WHITE,       "  3. Go back to the Motherboard\n");

    char c = get_choice(3);
    if (c == 'Q') return S_QUIT;

    if (c == '1') {
        g_items |= ITEM_BOOT;
        color(VGA_COLOR_LIGHT_CYAN,
        "\n  You carefully unpeel the map.  It details every byte offset,\n"
        "  every memory address.  You fold it up and tuck it away.\n"
        "  Now you know the way through this machine.\n");
        color(VGA_COLOR_LIGHT_GREEN, "\n  [Got: Boot Sector Map]\n");
        any_key();
        return S_MBR;
    }
    if (c == '2') {
        color(VGA_COLOR_LIGHT_CYAN,
        "\n  You grab onto a passing Stage 2 loader instruction and ride\n"
        "  it through the filesystem driver, past the ELF header,\n"
        "  and into the kernel...\n");
        any_key();
        return S_KERNEL;
    }
    return S_MOTHERBOARD;
}

/* ---- KERNEL ---- */
static scene_t do_kernel(void) {
    scene_header("  INTEILIDNOS KERNEL  -- v0.1  ", VGA_COLOR_LIGHT_BLUE);

    color(VGA_COLOR_WHITE,
    "\n"
    "  You land inside the kernel's text segment.\n"
    "  This is inteiliDOS -- the operating system you are running\n"
    "  right now, from the inside.\n"
    "\n"
    "  The code towers are organised by subsystem:\n");
    color(VGA_COLOR_LIGHT_RED,     "    ISR tower   -- 32 interrupt stubs, a GDT, an IDT\n");
    color(VGA_COLOR_LIGHT_GREEN,   "    VGA tower   -- framebuffer at 0xB8000, 80x25 text mode\n");
    color(VGA_COLOR_LIGHT_MAGENTA, "    Keyboard    -- circular buffer, scancode translation\n");
    color(VGA_COLOR_BROWN,        "    PIC tower   -- 8259A remapped to IRQs 0x20 / 0x28\n");
    color(VGA_COLOR_LIGHT_CYAN,    "    Memory      -- free-list allocator, kmalloc / kfree\n");
    color(VGA_COLOR_WHITE,
    "\n"
    "  A figure materialises -- tall, translucent, made entirely of\n"
    "  function pointers and linked lists.  It speaks:\n\n");
    color(VGA_COLOR_LIGHT_CYAN,
    "  \"I am Kernel K, guardian of the kernel space.\n"
    "   You should not be here.  But since you are...\n"
    "   the keyboard is the way out.  You'll need a clock and a map.\n"
    "   The VGA, PIC, and Shell may have things worth seeing first.\"\n\n");
    color(VGA_COLOR_WHITE,
    "  Kernel K dissolves into a cascade of interrupt return addresses.\n");

    show_inventory();
    sep();
    color(VGA_COLOR_LIGHT_GREEN, "  Where do you go?\n\n");
    color(VGA_COLOR_WHITE,       "  1. The VGA controller\n");
    color(VGA_COLOR_WHITE,       "  2. The PIC (interrupt controller)\n");
    color(VGA_COLOR_WHITE,       "  3. The Shell\n");
    color(VGA_COLOR_WHITE,       "  4. Head straight for the Keyboard\n");

    char c = get_choice(4);
    if (c == 'Q') return S_QUIT;

    if (c == '1') return S_VGA;
    if (c == '2') return S_PIC;
    if (c == '3') return S_SHELL_SCENE;
    if (c == '4') {
        if ((g_items & ITEM_CLOCK) && (g_items & ITEM_BOOT)) {
            return S_KEYBOARD;
        }
        color(VGA_COLOR_LIGHT_RED,
        "\n  Kernel K's voice echoes from everywhere at once:\n"
        "  \"You are not ready.  You need the Clock Crystal to TIME\n"
        "   your escape, and the Boot Map to FIND the exit.\"\n");
        any_key();
        return S_KERNEL;
    }
    return S_KERNEL;
}

/* ---- VGA ---- */
static scene_t do_vga(void) {
    scene_header("  VGA CONTROLLER  --  80x25 Text Mode  ", VGA_COLOR_LIGHT_RED);

    color(VGA_COLOR_WHITE,
    "\n"
    "  You step into a landscape of pure colour.\n"
    "  The VGA framebuffer begins at physical address 0xB8000.\n"
    "  From here it looks like an enormous grid of illuminated tiles:\n"
    "  80 columns wide, 25 rows tall -- 2000 character cells in total.\n"
    "\n"
    "  Each cell is TWO bytes:\n");
    color(VGA_COLOR_LIGHT_GREEN,   "    Low byte  -- the ASCII character to display\n");
    color(VGA_COLOR_LIGHT_MAGENTA, "    High byte -- the attribute (foreground | background colour)\n");
    color(VGA_COLOR_WHITE,
    "\n"
    "  You walk across the framebuffer surface.  Each tile you step on\n"
    "  briefly lights up, showing its contents.  You can read parts of\n"
    "  the current screen: the shell prompt, earlier output, this very\n"
    "  sentence you are reading right now.\n"
    "\n"
    "  (There is something profoundly recursive about this.)\n"
    "\n");

    if (!(g_flags & FLAG_VGA_DONE)) {
        color(VGA_COLOR_LIGHT_RED,
        "  Between two colour attribute tiles you find a fine-tipped\n"
        "  PIXEL BRUSH, its bristles soaked in attribute byte 0x0F\n"
        "  (bright white on black).  It writes directly to the framebuffer.\n");
        sep();
        color(VGA_COLOR_LIGHT_GREEN, "  What do you do?\n\n");
        color(VGA_COLOR_WHITE,       "  1. Take the Pixel Brush\n");
        color(VGA_COLOR_WHITE,       "  2. Return to the Kernel\n");

        char c = get_choice(2);
        if (c == 'Q') return S_QUIT;

        if (c == '1') {
            g_items |= ITEM_PIXEL;
            g_flags |= FLAG_VGA_DONE;
            color(VGA_COLOR_LIGHT_GREEN,
            "\n  You pick up the brush.  Immediately you feel the urge to\n"
            "  draw colourful ASCII art all over the framebuffer.\n"
            "  You resist.  Mostly.\n");
            color(VGA_COLOR_LIGHT_GREEN, "\n  [Got: Pixel Brush]\n");
            any_key();
            return S_KERNEL;
        }
        return S_KERNEL;
    } else {
        color(VGA_COLOR_DARK_GREY, "  You've already collected the Pixel Brush from here.\n");
        any_key();
        return S_KERNEL;
    }
}

/* ---- PIC ---- */
static scene_t do_pic(void) {
    scene_header("  8259A PIC  --  Programmable Interrupt Controller  ", VGA_COLOR_BROWN);

    color(VGA_COLOR_WHITE,
    "\n"
    "  The PIC looks like a giant highway interchange.\n"
    "  Sixteen roads converge here -- the IRQ lines.\n"
    "  Every piece of hardware that needs the CPU's attention\n"
    "  must file a request through this junction.\n"
    "\n"
    "  Signs above each on-ramp:\n");
    color(VGA_COLOR_DARK_GREY,
    "    IRQ  0  -- System Timer (fires 100x per second)\n"
    "    IRQ  1  -- Keyboard     <<< THIS ONE GOES TO THE KEYBOARD >>>\n"
    "    IRQ  2  -- Cascade to Slave PIC (IRQs 8-15)\n"
    "    IRQ  3  -- COM2 serial port\n"
    "    IRQ  4  -- COM1 serial port\n"
    "    IRQ  5  -- LPT2 / Sound Card\n"
    "    IRQ  6  -- Floppy disk controller\n"
    "    IRQ  7  -- LPT1 printer\n"
    "    IRQ  8  -- Real-Time Clock\n"
    "   IRQ 12  -- PS/2 Mouse\n"
    "   IRQ 14  -- IDE Primary (hard drive)\n"
    "\n");
    color(VGA_COLOR_WHITE,
    "  A MASTER PIC handles IRQs 0-7 and maps them to CPU interrupts\n"
    "  0x20-0x27.  A SLAVE PIC handles IRQs 8-15, mapped to 0x28-0x2F.\n"
    "  (inteiliDOS remapped them here to avoid conflict with CPU exceptions\n"
    "  which live at 0x00-0x1F.  The BIOS left them at 0x08 -- a mess.)\n"
    "\n");

    if (!(g_flags & FLAG_PIC_DONE)) {
        color(VGA_COLOR_BROWN,
        "  A booth attendant -- a tiny chip labeled '8259A' -- hands you\n"
        "  a laminated IRQ-1 TICKET.  'Keyboard express,' he says.\n"
        "  'One-way trip.  No refunds.  Mind the scancode translation.'\n");
        sep();
        color(VGA_COLOR_LIGHT_GREEN, "  What do you do?\n\n");
        color(VGA_COLOR_WHITE,       "  1. Take the IRQ-1 Ticket\n");
        color(VGA_COLOR_WHITE,       "  2. Ride the IRQ-0 timer pulse (just to feel it)\n");
        color(VGA_COLOR_WHITE,       "  3. Return to the Kernel\n");

        char c = get_choice(3);
        if (c == 'Q') return S_QUIT;

        if (c == '1') {
            g_items |= ITEM_IRQ;
            g_flags |= FLAG_PIC_DONE;
            color(VGA_COLOR_LIGHT_GREEN,
            "\n  You pocket the ticket.  IRQ-1 fires every time a key is\n"
            "  pressed -- you could ride it straight to the keyboard.\n");
            color(VGA_COLOR_LIGHT_GREEN, "\n  [Got: IRQ-1 Ticket]\n");
            any_key();
            return S_KERNEL;
        }
        if (c == '2') {
            color(VGA_COLOR_BROWN,
            "\n  You jump onto the IRQ-0 timer line.  BANG -- 100 times per\n"
            "  second the pulse fires and the kernel's timer handler runs.\n"
            "  It's updating a tick counter: one tick, two ticks, three--\n"
            "  The 200th impact tosses you back onto the PIC floor.\n"
            "  That was only two seconds.\n");
            any_key();
            return S_PIC;
        }
        return S_KERNEL;
    } else {
        color(VGA_COLOR_DARK_GREY, "  You've already picked up the IRQ-1 Ticket.\n");
        any_key();
        return S_KERNEL;
    }
}

/* ---- SHELL ---- */
static scene_t do_shell_scene(void) {
    scene_header("  INTEILIDNOS SHELL  ", VGA_COLOR_LIGHT_GREY);

    color(VGA_COLOR_WHITE,
    "\n"
    "  You step into a vast command-line amphitheatre.\n"
    "  The walls are lined with enormous letter blocks -- the commands\n"
    "  of the inteiliDOS shell, carved in stone:\n\n");
    color(VGA_COLOR_LIGHT_CYAN,    "    DIR    CLS    HELP   MEM    SYSINFO  ABOUT\n");
    color(VGA_COLOR_LIGHT_GREEN,   "    HELLO  TREE   TIME   DATE   HISTORY\n");
    color(VGA_COLOR_BROWN,        "    IEDIT  BASIC  SCRIPT RUN\n");
    color(VGA_COLOR_LIGHT_MAGENTA, "    SHUTDOWN  RESTART  FORMAT\n");
    color(VGA_COLOR_LIGHT_RED,     "    TOUR\n\n");
    color(VGA_COLOR_WHITE,
    "  You stop in front of the TOUR block.\n"
    "  You read the description carved beneath it:\n\n");
    color(VGA_COLOR_DARK_GREY,
    "    TOUR -- 'A Tour of inteiliDOS and Your Computer'\n"
    "    You have been shrunk to the size of an ant and digitized\n"
    "    into the computer.  Explore to escape.\n\n");
    color(VGA_COLOR_LIGHT_CYAN,
    "  You re-read that last sentence three times.\n"
    "  The game you are playing DESCRIBES YOU.\n"
    "  You are the player.  You are also the character.\n"
    "  The computer is running inteiliDOS.\n"
    "  inteiliDOS is running the TOUR command.\n"
    "  The TOUR command is running YOU.\n\n");
    color(VGA_COLOR_WHITE,
    "  You sit down on a nearby semicolon and take a moment.\n");

    if (!(g_flags & FLAG_SHELL_DONE)) {
        color(VGA_COLOR_BROWN,
        "\n  The shell's readline loop drops a SHELL_BADGE at your feet:\n"
        "  a small proof that you were here, inside the system that\n"
        "  typed the command that started all of this.\n");
        g_flags |= FLAG_SHELL_DONE;
    }

    sep();
    color(VGA_COLOR_LIGHT_GREEN, "  What do you do?\n\n");
    color(VGA_COLOR_WHITE,       "  1. Type IEDIT at the stone prompt\n");
    color(VGA_COLOR_WHITE,       "  2. Type HELP\n");
    color(VGA_COLOR_WHITE,       "  3. Return to the Kernel\n");

    char c = get_choice(3);
    if (c == 'Q') return S_QUIT;

    if (c == '1') {
        color(VGA_COLOR_LIGHT_GREEN,
        "\n  A miniature iEdit window opens in the stone floor.\n"
        "  You type:\n\n"
        "    Dear Professor Kleinberg,\n"
        "    I am inside the computer.  The VGA buffer is beautiful.\n"
        "    The PIC is exactly as chaotic as the textbooks suggest.\n"
        "    I am heading for the Escape key.  Wish me luck.\n"
        "                              -- Your digitized student\n\n"
        "  You press Ctrl+Q to quit iEdit.  The stone floor closes.\n");
        any_key();
        return S_SHELL_SCENE;
    }
    if (c == '2') {
        color(VGA_COLOR_DARK_GREY,
        "\n  The HELP command runs.  You read commands you have typed\n"
        "  a hundred times from the outside.  From in here each one\n"
        "  looks like a door into a different universe.\n"
        "  You feel a new appreciation for every keystroke.\n");
        any_key();
        return S_SHELL_SCENE;
    }
    return S_KERNEL;
}

/* ---- KEYBOARD ---- */
static scene_t do_keyboard(void) {
    scene_header("  KEYBOARD CONTROLLER  --  PS/2 Interface  ", VGA_COLOR_LIGHT_GREEN);

    color(VGA_COLOR_WHITE,
    "\n"
    "  The keyboard controller is an HP Vectra keyboard — a solid,\n"
    "  no-nonsense unit from 1998.  Every key is a plaza the size\n"
    "  of a football field, its switch mechanism a hydraulic press.\n"
    "\n"
    "  The clacking is deafening.  Someone outside is typing.\n"
    "  Each keystroke sends a SCANCODE down the PS/2 wire to the\n"
    "  8042 controller chip, which raises IRQ-1 to get the CPU's\n"
    "  attention.  You are standing inside that process.\n"
    "\n"
    "  You walk through rows of alphanumeric keys, function keys,\n"
    "  modifier keys.  Then you see it.\n\n");
    color(VGA_COLOR_LIGHT_CYAN,
    "  In the far top-left corner of the keyboard, lit up in cold\n"
    "  blue light, is the ESCAPE KEY.\n"
    "\n"
    "  Scancode 0x01.  The first key defined.  The original escape.\n"
    "  The key that says: STOP.  GET OUT.  RETURN TO NORMALCY.\n"
    "\n"
    "  It is enormous -- six metres of moulded plastic and spring\n"
    "  mechanism -- and it is vibrating with potential energy.\n"
    "  Pressing it will send a pulse up through the matrix, through\n"
    "  the controller, through the IRQ line, into the CPU, and...\n"
    "  if timed correctly by the clock crystal... digitally reassemble\n"
    "  you on the other side.\n\n");

    show_inventory();
    sep();

    int can_escape = (g_items & ITEM_CLOCK) && (g_items & ITEM_BOOT);

    color(VGA_COLOR_LIGHT_GREEN, "  What do you do?\n\n");
    if (can_escape) {
        color(VGA_COLOR_WHITE, "  1. Press the ESCAPE key  (you have everything you need)\n");
    } else {
        color(VGA_COLOR_DARK_GREY,
        "  1. Press the ESCAPE key  [LOCKED -- you need:\n");
        if (!(g_items & ITEM_CLOCK))
            color(VGA_COLOR_DARK_GREY,
            "     - the CPU Clock Crystal (from the CPU)\n");
        if (!(g_items & ITEM_BOOT))
            color(VGA_COLOR_DARK_GREY,
            "     - the Boot Sector Map   (from the MBR)\n");
        color(VGA_COLOR_DARK_GREY, "  ]\n");
    }
    color(VGA_COLOR_WHITE, "  2. Examine the other keys\n");
    color(VGA_COLOR_WHITE, "  3. Return to the Kernel\n");

    char c = get_choice(3);
    if (c == 'Q') return S_QUIT;

    if (c == '1') {
        if (can_escape) return S_WIN;
        color(VGA_COLOR_LIGHT_RED,
        "\n  You try to push the Escape key but without the clock crystal\n"
        "  to time the digital pulse and the boot map to route the signal,\n"
        "  the reassembly matrix collapses.  You bounce off the spring\n"
        "  mechanism and land back on the keyboard floor.\n"
        "  You'll need those items first.\n");
        any_key();
        return S_KEYBOARD;
    }
    if (c == '2') {
        color(VGA_COLOR_WHITE,
        "\n"
        "  You wander through the key districts:\n\n");
        color(VGA_COLOR_LIGHT_GREEN,
        "  ENTER -- a lever as tall as a building.  Each press commits\n"
        "           a command.  You feel the weight of every line of\n"
        "           code ever run on this machine.\n\n");
        color(VGA_COLOR_BROWN,
        "  CTRL  -- short for Control.  It does nothing alone,\n"
        "           but in combination it changes everything.\n"
        "           Just like a transistor -- useless in isolation,\n"
        "           powerful in a circuit.\n\n");
        color(VGA_COLOR_LIGHT_MAGENTA,
        "  BACKSPACE -- the great eraser.  Undoer of mistakes.\n"
        "              You have a deep personal fondness for this key.\n\n");
        color(VGA_COLOR_LIGHT_RED,
        "  DELETE -- the other eraser.  More aggressive.  No regrets.\n\n");
        color(VGA_COLOR_LIGHT_CYAN,
        "  SPACE -- the most-pressed key on any keyboard, and yet\n"
        "           it produces nothing visible.  Pure potential.\n");
        any_key();
        return S_KEYBOARD;
    }
    return S_KERNEL;
}

/* ---- WIN ---- */
static scene_t do_win(void) {
    vga_clear();
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREEN);
    vga_puts("                                                                                ");
    vga_puts("                          YOU ESCAPED!                                         \n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    sep();

    color(VGA_COLOR_WHITE,
    "\n"
    "  You place the Clock Crystal against the Escape key mechanism.\n"
    "  The crystal oscillates at exactly 14.318 MHz, synchronising\n"
    "  with the keyboard controller's timing circuit.\n"
    "\n"
    "  You unfold the Boot Sector Map.  Address 0x7C00.  The route\n"
    "  back runs BACKWARD through the boot chain: kernel -> bootloader\n"
    "  -> BIOS -> and then OUT, through the signal path, through the\n"
    "  PS/2 port, and into the physical world.\n"
    "\n"
    "  You take a breath.\n"
    "  You press the Escape key.\n\n");

    color(VGA_COLOR_BROWN,
    "  0x01  0x81  --  press, release.  Scancode transmitted.\n"
    "  The 8042 controller fires IRQ-1.\n"
    "  The PIC routes it to vector 0x21.\n"
    "  The kernel's ISR stub saves all registers.\n"
    "  The keyboard handler reads the port.\n"
    "  And then--\n\n");

    color(VGA_COLOR_LIGHT_CYAN,
    "  WHITE LIGHT.\n\n");

    color(VGA_COLOR_WHITE,
    "  You are full-sized again.  You are sitting at the keyboard.\n"
    "  Professor Kleinberg is staring at you from across the lab,\n"
    "  screwdriver in hand, jaw on the floor.\n\n"
    "  'You were in there for eleven minutes,' he says quietly.\n"
    "  'Felt longer,' you reply.\n\n");

    sep();
    color(VGA_COLOR_LIGHT_GREEN,  "  WHAT YOU LEARNED ON YOUR TOUR:\n\n");
    color(VGA_COLOR_BROWN,       "  * The PSU     converts AC to regulated DC (3.3V / 5V / 12V)\n");
    color(VGA_COLOR_LIGHT_GREEN,  "  * The CPU     switches transistors billions of times/sec\n");
    color(VGA_COLOR_LIGHT_CYAN,   "  * RAM         is volatile: capacitors that forget on power-off\n");
    color(VGA_COLOR_LIGHT_MAGENTA,"  * The BIOS    is the firmware that wakes the machine\n");
    color(VGA_COLOR_WHITE,        "  * The MBR     is a 512-byte corridor that starts everything\n");
    color(VGA_COLOR_LIGHT_BLUE,   "  * The kernel  manages hardware so your programs don't have to\n");
    color(VGA_COLOR_LIGHT_RED,    "  * VGA memory  lives at 0xB8000: each cell is char + attribute\n");
    color(VGA_COLOR_BROWN,       "  * The PIC     routes hardware signals to the CPU via IRQs\n");
    color(VGA_COLOR_LIGHT_GREEN,  "  * The shell   turns your words into system calls\n");
    color(VGA_COLOR_LIGHT_CYAN,   "  * The Escape key is scancode 0x01. It lives up to its name.\n");

    sep();
    color(VGA_COLOR_WHITE,
    "\n"
    "  The professor scribbles something on his notepad.\n"
    "  'Next time,' he says, 'we try the GPU.'\n\n");
    color(VGA_COLOR_DARK_GREY, "  [ Thanks for playing.  Press any key to return to the shell. ]\n");

    keyboard_getchar();
    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    return S_QUIT;
}

/* ------------------------------------------------------------------ */
/* Main entry                                                          */
/* ------------------------------------------------------------------ */
void tour_run(void) {
    g_items = 0;
    g_flags = 0;

    scene_t scene = S_INTRO;

    while (scene != S_QUIT) {
        switch (scene) {
            case S_INTRO:        scene = do_intro();        break;
            case S_PSU:          scene = do_psu();          break;
            case S_MOTHERBOARD:  scene = do_motherboard();  break;
            case S_CPU:          scene = do_cpu();          break;
            case S_CPU_CACHE:    scene = do_cpu_cache();    break;
            case S_RAM:          scene = do_ram();          break;
            case S_BIOS:         scene = do_bios();         break;
            case S_MBR:          scene = do_mbr();          break;
            case S_KERNEL:       scene = do_kernel();       break;
            case S_VGA:          scene = do_vga();          break;
            case S_PIC:          scene = do_pic();          break;
            case S_SHELL_SCENE:  scene = do_shell_scene();  break;
            case S_KEYBOARD:     scene = do_keyboard();     break;
            case S_WIN:          scene = do_win();          break;
            default:             scene = S_QUIT;            break;
        }
    }

    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}
