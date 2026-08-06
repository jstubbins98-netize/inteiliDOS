/*
 * inteilidOS -- kernel/kernel.c
 * Main kernel entry point: hardware init → IntelliShell
 */

#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "timer.h"
#include "keyboard.h"
#include "usb.h"
#include "cdrom.h"
#include "ata.h"
#include "memory.h"
#include "multiboot.h"
#include "../shell/shell.h"
#include "../shell/setup.h"
#include <stdint.h>

/* Provided by the linker script */
extern uint32_t _kernel_end;

static void print_banner(size_t mem_kb) {
    vga_clear();

    /* Title bar */
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    for (int i = 0; i < 80; i++) vga_putchar(' ');
    vga_set_cursor(0, 0);
    vga_puts("  inteiliDOS  Version 1.0          Inteilix Software Corporation          ");

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_putchar('\n');
    vga_putchar('\n');

    /* Welcome message */
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("  *** Welcome to inteiliDOS! ***\n\n");

    /* Copyright + machine info */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_puts("  Copyright (C) Inteilix Software Corporation\n\n");
    vga_puts("  Machine:\n");
    vga_printf("    x86 Protected Mode CPU\n");
    vga_printf("    %u MB RAM\n", (unsigned)(mem_kb / 1024u));
    vga_putchar('\n');

    /* Tagline */
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("  \"The Classic Command Line, Reimagined.\"\n");
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_puts("  The command line never stopped evolving.\n");
    vga_putchar('\n');

    vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
    vga_puts("  Type HELP for a list of commands.\n\n");
}

/* ---- Kernel entry ---- */
void kernel_main(uint32_t mb_magic, uint32_t mb_info_phys) {
    /*
     * Boot order for MBR boots (no GRUB, no IDT on entry):
     *
     * 1. vga_init   — clears screen; sets vga_attr so ISR output is visible.
     * 2. gdt_init   — loads kernel's flat GDT.  CS reload (far jump) is
     *                  skipped in gdt_flush: MBR and kernel share an identical
     *                  CS=0x08 flat 4 GB descriptor, so the cached value is
     *                  already correct.
     * 3. idt_init / isr_init / irq_init — from here any CPU exception is
     *                  caught and printed instead of silently resetting.
     * 4. heap / memory — after the interrupt infrastructure is in place.
     * 5. Drivers, shell.
     */

    /* vga_set_mode3() is intentionally skipped on HDD-direct boot.
     *
     * The HP Vectra VEi8 (440BX / Award BIOS) hands off to the MBR with the
     * display already in VGA text mode 3: the error-handler path in the VBR
     * uses INT 10h AH=0Eh (teletype) which proves the BIOS text framebuffer
     * is live.  Our VBR never calls INT 10h itself (AH=00h/set-mode can hang
     * on this BIOS), so the display mode is unchanged when we enter PM.
     *
     * Calling vga_set_mode3() here reprograms raw VGA I/O ports.  On S3
     * ViRGE / Matrox / generic PCI VGA cards common in 1998 Vectra configs,
     * the VGA registers are writable, but if the VBIOS has already set a
     * compatible text mode the call is harmless — it reprograms to the same
     * state.  Skip it anyway: just write to 0xB8000 in the mode the BIOS left.
     *
     * When booted via GRUB (USB), GRUB calls INT 10h before the kernel so
     * text mode is guaranteed regardless — vga_set_mode3() was a no-op there
     * too.  Leave the call absent here; add it back only if a future target
     * machine genuinely leaves the display in a non-text mode. */

    /* Dead-reckoning diagnostic '4': kernel_main reached, before any init.
     * Visible as a bright-white '4' at col 3 if the kernel crashes here.
     * vga_init() will overwrite the entire screen, making this disappear. */
    *(volatile uint16_t *)0xB8006u = 0x0F34u;   /* '4' bright-white on black */

    /* 1. VGA */
    vga_init();
    /* Clear CR0.TS (Task-Switch flag).  The BIOS can leave TS=1, which causes
     * a spurious #NM on the first implicit FPU-state check GCC emits at -O2.
     * CLTS is a privileged ring-0 instruction — always safe here.           */
    __asm__ volatile ("clts");

    /* 2. GDT */
    gdt_init();

    /* 3. IDT + ISRs + PIC remap */
    idt_init();
    isr_init();
    irq_init();

    /* 4. Memory */
    heap_init();
    int launch_setup = 0;
    if (mb_magic == MULTIBOOT1_LOADER_MAGIC) {
        memory_init(mb_info_phys);

        /* Easter-egg PCM data is embedded in the kernel binary at build time
         * by wav_to_pcm.py + cmake — no GRUB modules needed.              */

        /* Check multiboot command-line for the "setup" keyword.
         * GRUB passes it when the user selects "Install inteiliDOS to IDE HDD".
         * Bit 2 of flags (MULTIBOOT_FLAG_CMDLINE) must be set and cmdline
         * must be non-zero before we dereference the physical address.     */
        multiboot_info_t *mbi = (multiboot_info_t *)mb_info_phys;
        if ((mbi->flags & MULTIBOOT_FLAG_CMDLINE) && mbi->cmdline) {
            const char *cmdline = (const char *)(uintptr_t)mbi->cmdline;
            if (kstrstr(cmdline, "setup"))
                launch_setup = 1;
        }
    }

    /* 5. Boot sequence display */
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("\n  inteiliDOS Boot Sequence\n");
    vga_puts("  ========================\n\n");

    vga_puts("  Initialising GDT...       ");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK]\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    vga_puts("  Initialising IDT...       ");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK]\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    vga_puts("  Initialising CPU...       ");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK]\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    vga_puts("  Checking Memory...        ");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK]\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    vga_puts("  Loading Drivers...        ");
    timer_init(1000);
    keyboard_init();
    __asm__ volatile ("sti");
    speaker_boot_chime();
    usb_keyboard_init();   /* probe UHCI; silent no-op if absent */
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK]\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* Probe IDE bus: ATA hard disks then ATAPI CD-ROM drives */
    vga_puts("  Detecting Storage...      ");
    static ata_drive_t ata_drives[ATA_MAX_DRIVES];
    int ata_found = ata_detect(ata_drives);
    cdrom_init();   /* populates cdrom_drives(); prints nothing yet */
    int cd_found   = cdrom_count();
    if (ata_found > 0 || cd_found > 0) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_puts("[OK]");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_printf("  (%d HDD, %d CD-ROM)\n", ata_found, cd_found);
    } else {
        vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK);
        vga_puts("[NO DRIVES]\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    vga_puts("  Mounting File System...   ");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK]\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    vga_puts("  Loading IntelliShell...   ");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK]\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* Transition to shell (or installer if "setup" was on the kernel cmdline) */
    timer_sleep(800);
    print_banner(memory_total_kb());
    if (launch_setup)
        setup_run();
    shell_run();

    for (;;) __asm__ volatile ("cli; hlt");
}
