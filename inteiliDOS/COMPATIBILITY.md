# Hardware compatibility strategy

This edition favors standardized legacy PC interfaces over machine-specific
drivers. Willburd the Setup Wizard generates `configurations/config.h` so each
build enables only the compatible optional paths selected for its target.

The hardware scope includes 1990s, 2000s, and early-2010s PCs.
[MODERN_HARDWARE.md](MODERN_HARDWARE.md) describes the newer profiles, x86-64
compatibility mode, native UHCI/xHCI keyboards, and SATA/AHCI HDD support.
This is not a guarantee that every PC from those years has supported interfaces.

## 86Box CD-ROM configuration

Use an **IDE/ATAPI** CD-ROM on one of the primary or secondary IDE positions
(master or slave) and enable ATAPI CD-ROM support in Willburd's hardware
profile. For PCI-native IDE channels, also enable PCI IDE support. This driver
does not support SCSI, proprietary Panasonic/Mitsumi/Sony interfaces, or
tertiary/quaternary IDE channels.

Drive detection uses the returned IDENTIFY PACKET DEVICE data rather than
assuming the reset signature remains in the cylinder registers after BIOS CD
reads. Empty trays still count as detected drives; insert a disc and use
LaunchPad's F3 rescan to refresh its capacity.

After updating the driver, rebuild the OS and replace the old boot image in
the emulator. The driver regression tests are documented in
[`tests/README.md`](tests/README.md); their emulator coverage is QEMU, not a
claim of testing every 86Box machine/controller combination.

## Required baseline

| Area | Baseline |
|---|---|
| CPU | Intel 80386-compatible, 32-bit protected mode |
| RAM | 8 MiB minimum for the current full build; 16 MiB+ recommended |
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
4. **Kernel storage:** select legacy PATA, PCI-native IDE/SATA compatibility,
   or SATA/AHCI in Willburd. AHCI supports 512-byte logical-sector HDDs,
   including 512e, with LBA48 where advertised. Unsupported storage can be
   disabled so later filesystem and setup calls do not probe it.
5. **Video:** the kernel keeps the firmware-established VGA text mode and
   writes through the standard text buffer.
6. **Input:** Willburd keeps PS/2 available. Native UHCI and xHCI boot-protocol
   USB keyboards are optional. OHCI/EHCI-only ports and external USB hubs
   need another input path, such as working BIOS legacy USB emulation.
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