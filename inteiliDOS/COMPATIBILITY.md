# Hardware compatibility strategy

This edition favors standardized legacy PC interfaces over machine-specific
drivers. Willburd the Setup Wizard generates `configurations/config.h` so each
build enables only the compatible optional paths selected for its target.

## Required baseline

| Area | Baseline |
|---|---|
| CPU | Intel 80386-compatible, 32-bit protected mode |
| RAM | 4 MB minimum; 8 MB recommended |
| Firmware | PC-compatible BIOS |
| Video | VGA-compatible color text mode at `0xB8000` |
| Interrupts | Dual 8259 PIC |
| Timer | 8253/8254 PIT |
| Keyboard | 8042/PS/2 compatible controller |
| Boot storage | BIOS CD boot through GRUB, or CHS floppy boot |

## Compatibility fallbacks

1. **CPU:** all kernel C code is compiled for i386. CPUID is not required.
2. **Memory:** GRUB supplies an E820-derived Multiboot memory map. The raw
   floppy boot sector uses BIOS `INT 12h` and `INT 15h AH=88h` basic totals.
3. **Disk boot:** the universal ISO lets GRUB use BIOS disk services. The raw
   floppy image uses `INT 13h AH=02h` CHS reads and detects geometry with
   `INT 13h AH=08h`.
4. **Kernel storage:** the wizard enables PATA only for standard compatibility
   ports `1F0h/3F6h` and `170h/376h`. Unsupported storage can be disabled so
   later filesystem and setup calls do not probe ATA hardware.
5. **Video:** the kernel keeps the firmware-established VGA text mode and
   writes through the standard text buffer.
6. **Input:** PS/2 remains active on all builds. UHCI USB can be added by the
   generated profile but is never the only keyboard path.
7. **Timing:** the PIT remains fixed at 1000 Hz because the kernel's sleep and
   polling APIs use one timer tick as one millisecond.

## Known boundaries

- 8086, 80186, 80286, and non-x86 machines are unsupported.
- A 386SX is instruction-compatible, but low RAM and a 16-bit external bus can
  make the full shell impractical.
- Proprietary laptop display, keyboard, and disk interfaces may not expose
  standard PC-compatible behavior.
- Native SCSI storage is not implemented. Such systems can still boot the ISO
  if their BIOS and GRUB expose the boot device, but the kernel cannot browse
  an unsupported SCSI disk afterward.
- High-resolution VBE graphics are intentionally not required; the OS remains
  in VGA text mode for maximum compatibility.
- Optional AC'97 or HDA audio capture works only when matching hardware is
  selected and detected.

## Recommended test matrix

- QEMU `486`, 16 MB, IDE disk, PS/2 keyboard
- QEMU Pentium, 32 MB, CD-ROM boot
- 86Box/PCem 386DX and 486 profiles with ISA VGA
- late-1990s Pentium II/III with PCI IDE in compatibility mode
- one real ISA-only system and one PCI system before calling a release universal