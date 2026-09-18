# inteiliDOS for 1990s Computers

> “The future still has a blinking cursor.”

inteiliDOS is a freestanding, 32-bit x86 operating system written in C and NASM
assembly. This edition is designed for broad compatibility with PC-compatible
computers from the 1990s, beginning with the Intel 80386 instruction set.

It is not Linux, DOS, or a hosted application. The boot code, kernel, device
drivers, shell, applications, and games run directly on the computer.

## Compatibility baseline

The universal build favors standardized legacy interfaces:

| Area | Baseline |
|---|---|
| CPU | Intel 80386-compatible or newer |
| Memory | 4 MB minimum; 8 MB or more recommended |
| Firmware | PC-compatible BIOS |
| Display | VGA-compatible 80×25 color text mode |
| Interrupt controller | Dual 8259 PIC |
| Timer | 8253/8254 PIT |
| Keyboard | 8042/PS/2-compatible controller |
| Boot media | BIOS CD boot through GRUB or raw CHS floppy boot |
| Storage | ATA/ATAPI PIO compatibility interfaces |

Kernel C code is compiled with:

```text
-m32 -march=i386 -mtune=i386 -O1
-msoft-float -mno-mmx -mno-sse -mno-sse2
```

The operating system does not require CPUID, CMOV, MMX, SSE, APIC, USB, ACPI,
or PCI to start. Optional drivers can use later hardware when it is present.

See [COMPATIBILITY.md](inteiliDOS for 1990s computers/COMPATIBILITY.md) for detailed boundaries and testing
recommendations.

## Features

### Kernel and hardware

- Multiboot1 kernel entry for GRUB
- Raw 1.44 MB floppy boot image using BIOS CHS reads
- 32-bit protected mode with GDT and IDT
- All CPU exception vectors and 16 hardware IRQ lines
- Dual 8259 PIC and 8254 PIT
- VGA text console
- PS/2 keyboard input
- Physical page bitmap and 2 MB kernel heap
- ATA/IDE and ATAPI PIO detection
- CD-ROM reading and ejection
- Floppy disk controller and FAT12 reader
- ISO 9660 CD-ROM filesystem reader
- Optional PCI, UHCI, AC'97, and HDA-related code

The universal startup path uses PS/2 input. UHCI USB probing is not enabled
automatically because many early systems do not implement PCI configuration
space.

### Shell and applications

- IntelliShell command prompt with command history
- Plain-English command aliases
- IEdit text editor
- InteiliBASIC interpreter
- InteiliSheets spreadsheet
- InteiliTalk text-to-speech through the PC speaker
- LaunchPad program manager
- InteiliFile Manager
- SETUP disk installation utility
- Tetris
- TOUR text adventure
- Daisy Bell and Still Alive audio demonstrations
- KCS cassette `CSAVE` and `CLOAD` support

Hardware-dependent applications remain available but may report that their
required controller is absent.

## Building

### Required tools

- CMake 3.16 or newer
- NASM
- `i686-elf-gcc`
- `i686-elf-ld`
- `i686-elf-objcopy`
- GRUB BIOS utilities, including `grub-mkrescue`
- `xorriso`
- `dd`

`genisoimage` or `mkisofs` is optional and is used for the additional El Torito
image.

### Build command

```bash
cd "inteiliDOS for 1990s computers"
./build.sh
```

To remove the existing build directory first:

```bash
./build.sh --clean
```

The only supported build target is `universal`. The compatibility flags
`--modern` and `--legacy` are accepted by the script but both resolve to the
same universal i386 build.

### Outputs

Build products are written to `build_1990s/`:

| File | Purpose |
|---|---|
| `inteiliDOS_1990s.iso` | GRUB BIOS bootable CD image |
| `inteiliDOS_1990s_floppy.img` | Raw 1.44 MB CHS floppy image |
| `inteilidOS.elf` | ELF kernel with symbols |
| `inteilidOS.bin` | Flat kernel binary |

If `genisoimage` or `mkisofs` is installed, the build can also create an El
Torito image based on the floppy boot path.

## Running in QEMU

Build and boot the universal ISO:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 16
```

Boot the raw floppy image:

```bash
qemu-system-i386 \
  -drive file=build_1990s/inteiliDOS_1990s_floppy.img,format=raw,if=floppy \
  -cpu 486 \
  -m 16
```

CMake convenience targets are also available:

```bash
cmake --build build_1990s --target run
cmake --build build_1990s --target run-legacy
cmake --build build_1990s --target run-floppy
```

QEMU does not provide a 386 CPU model in every release, so the automated run
targets use its 486 model. The generated kernel remains restricted to i386
instructions.

## Installing on physical media

Writing a disk image destroys the previous contents of the destination.
Confirm the device name before running either command.

### Physical floppy

```bash
sudo dd \
  if=build_1990s/inteiliDOS_1990s_floppy.img \
  of=/dev/fd0 \
  bs=512 \
  conv=fsync
```

### CD-ROM

Burn `build_1990s/inteiliDOS_1990s.iso` as an image, not as a regular file.
Older optical drives are generally more reliable with CD-R media written at a
low speed.

The GRUB menu contains:

```text
inteiliDOS for 1990s Computers
inteiliDOS for 1990s Computers — Recovery
inteiliDOS for 1990s Computers — Install to IDE HDD
```

## Boot and memory behavior

### GRUB path

GRUB loads `inteilidOS.elf`, supplies the Multiboot1 loader magic and memory
information, and jumps to `_start`. The kernel uses the supplied memory map to
initialize its physical page bitmap.

### Raw floppy path

`mbr_legacy.asm`:

1. Initializes real-mode segments and the stack.
2. Requests A20 through BIOS services and Fast A20.
3. Reads conventional and extended memory totals from the BIOS.
4. Detects disk geometry with `INT 13h AH=08h`.
5. Loads the kernel one sector at a time with `INT 13h AH=02h`.
6. Copies sectors to physical address `0x00100000` through unreal mode.
7. Enters 32-bit protected mode and passes basic memory totals to the kernel.

The boot sector is exactly 512 bytes and ends with the `55 AA` signature.

## Project structure

```text
boot/                         Bootstrap assembly and BIOS boot sectors
kernel/                       Kernel, memory, interrupt, and device drivers
shell/                        IntelliShell and built-in applications
shell/sam/                    SAM speech synthesizer
tetris/                       Tetris game
daisy_bell_easter_egg/        Daisy Bell demonstration
still_alive_easter_egg/       Still Alive demonstration
grub/                         GRUB configurations
cmake/                        Toolchain and binary-header helpers
CMakeLists.txt                Universal i386 build definition
build.sh                      Build entry point
BUILD.md                      Extended toolchain instructions
COMPATIBILITY.md              Hardware strategy and known boundaries
for_developers.md             Architecture and contribution guide
```

## Known boundaries

- 80286 and older processors are unsupported.
- Non-PC-compatible firmware is unsupported.
- A VGA-compatible display and BIOS boot path are required.
- Native SCSI drivers are not included.
- High-resolution VBE graphics are not implemented.
- USB input is not a universal startup dependency.
- Controller-specific audio capture only works on matching hardware.
- Real hardware varies; emulator success does not replace testing on ISA-only,
  PCI, 386, 486, and Pentium-class computers.

## License and attribution

inteiliDOS is open-source software. You may study, modify, use, and distribute
the source and compiled images. Redistributed versions must clearly credit
Inteilix Software Corporation as the original author.

See [for_developers.md](for_developers.md) for development details.
