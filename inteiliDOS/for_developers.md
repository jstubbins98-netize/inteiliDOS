# Developing inteiliDOS

## Newer hardware and directory name

The source directory is now `inteiliDOS`. Hardware profiles cover 1990s,
2000s and early-2010s PCs; [MODERN_HARDWARE.md](MODERN_HARDWARE.md) is the
current specification for newer hardware and its limitations. x86-64 support
means running the existing IA-32 kernel on 64-bit-capable CPUs. Native program
ABI, LaunchPad ELF32 loading and the DOS emulator remain 32-bit.

USB boot-keyboard parsing/report translation is shared by UHCI and xHCI.
xHCI uses identity-mapped, aligned DMA below 4 GiB, BIOS ownership handoff,
command/transfer/event rings and bounded polling. EHCI high-speed BOT storage
uses a polled asynchronous QH/qTD schedule and shares BOT/FAT code with xHCI.
Initialize EHCI port routing before the UHCI keyboard, not during rescans.
No hubs, OHCI, or EHCI HID input are implemented. Failed controller DMA must
be quiesced before reusing buffers.

AHCI uses single-sector DMA bounce buffers, bounds-checked LBA48 commands and
advertised cache-flush commands. A timeout disables that descriptor slot;
unknown durability is not permission to report a successful write. The public
32-bit sector API limits addressable capacity to just under 2 TiB. Native 4Kn,
AHCI optical media, RAID and NVMe remain unsupported. Do not modify the native
disk layout just to add another controller.

This guide describes the universal 32-bit x86 edition. Its lowest supported
instruction set is Intel 80386, and its required hardware interfaces are the
standard PC-compatible BIOS, VGA text mode, dual 8259 PIC, 8253/8254 PIT, and
8042/PS/2 keyboard controller.

## Design rules

New code must preserve these constraints:

1. Do not emit instructions newer than the Intel 80386 ISA.
2. Do not execute CPUID unless availability has first been detected safely.
3. Do not require an FPU, MMX, SSE, CMOV, MSRs, SYSENTER, or APIC.
4. Keep the dual 8259 PIC and PIT paths operational.
5. Keep PS/2 keyboard input independent of PCI and USB.
6. Keep VGA text mode as the display fallback.
7. Prefer BIOS boot services and ATA compatibility-mode PIO over
   chipset-specific DMA during startup.
8. Treat PCI, USB, ACPI, and controller-specific audio as optional.
9. Avoid assuming that CD boot, LBA extensions, or a PCI bus exists.
10. Preserve the raw CHS floppy boot path.

## Generated hardware configuration

`build.sh` creates `configurations/config.h` before CMake is configured. CMake
stops with a clear error when the generated header is missing, ensuring that a
kernel cannot be built without a selected hardware profile.

The generated macros include:

```c
CONFIG_PROFILE_NAME
CONFIG_CPU_CLASS
CONFIG_RAM_MB
CONFIG_BOOT_MEDIA
CONFIG_ENABLE_PS2_KEYBOARD
CONFIG_ENABLE_USB_KEYBOARD
CONFIG_ENABLE_PCI
CONFIG_ENABLE_PCI_IDE
CONFIG_ENABLE_AHCI
CONFIG_ENABLE_ATA
CONFIG_ENABLE_ATAPI_CDROM
CONFIG_ENABLE_FLOPPY
CONFIG_AUDIO_MODE
CONFIG_TIMER_HZ
```

CMake force-includes the header in every C translation unit. Code may use the
macros with `#if`, but must retain a conservative fallback. A generated profile
must never raise the compiler ISA above i386.

Current runtime uses include:

- `kernel/kernel.c`: profile display, timer rate, keyboard drivers, ATA, ATAPI
- `kernel/ata.c`: ATA availability and guarded sector access
- `shell/launchpad.c`: floppy-controller availability
- `shell/basic.c`: AC'97, HDA, automatic, or disabled CLOAD capture

Use `./build.sh --config-only` to exercise the questionnaire without requiring
the cross-toolchain. Use `./build.sh --defaults` for unattended builds.

## Build architecture

The project uses an `i686-elf` cross-toolchain because it provides freestanding
32-bit ELF tools. The compiler target is still restricted to i386:

```cmake
-m32
-march=i386
-mtune=i386
-O1
-msoft-float
-mno-mmx
-mno-sse
-mno-sse2
-nostdlib
-ffreestanding
-fno-builtin
-fno-stack-protector
-fno-pic
-fno-pie
```

The cross-toolchain prefix does not define the minimum processor. The
`-march=i386` option does.

Build with:

```bash
./build.sh
```

This normally runs Willburd's interactive questionnaire before CMake. Every
question provides an `H` help choice that explains how to identify the target
hardware.

The timer remains fixed at 1000 Hz because existing sleep, USB polling, and
audio code use one PIT tick as one millisecond. PS/2 remains enabled in every
Willburd profile as a fallback; direct UHCI/xHCI boot keyboards are optional.
The wizard also offers PCI-native IDE/SATA compatibility and SATA/AHCI HDD
profiles. SCSI, RAID and NVMe are not implemented.

The script always configures:

```text
BUILD_TARGET=universal
BUILD_DIR=build_1990s
```

Main products:

```text
build_1990s/inteilidOS.elf
build_1990s/inteilidOS.bin
build_1990s/inteiliDOS_1990s.iso
build_1990s/inteiliDOS_1990s_floppy.img
configurations/config.h
```

## Source layout

### Boot code

| File | Role |
|---|---|
| `boot/boot.asm` | Multiboot1 header and protected-mode kernel entry |
| `boot/mbr.asm` | Standalone raw disk-image boot sector |
| `boot/hdd_mbr.asm` | Relocating installed-HDD partition chainloader |
| `boot/hdd_vbr.asm` | Streaming BIOS HDD kernel loader (4 KiB bounce buffer) |
| `boot/mbr_legacy.asm` | Universal CHS floppy boot sector |
| `boot/gdt_flush.asm` | Loads the GDT and reloads segment registers |
| `boot/idt_load.asm` | Loads the IDT |
| `boot/isr_stubs.asm` | Exception and IRQ assembly stubs |

Installed HDDs use the versioned contract in `kernel/disk_layout.h`. New
installations reserve 8192 sectors (4 MiB) for the kernel (layout version 2);
the configuration descriptor is at partition offset 8193, the directory at
8201, and file data starts at 8202. The `IDL2` VBR marker at offset 0x25 selects
this descriptor location. Version 1 disks keep their original 1024-sector
reserve, descriptor at 1025, directory at 1033, and data at 1034. The runtime
validates either layout without migrating existing files. Boot repair preserves
the selected layout and only updates bootstrap sectors after backing them up.

The installed loader uses INT 13h AH=42h to read eight sectors at a time into
physical 0x8000, then copies that chunk into the 1–5 MiB kernel window in
protected mode. It returns to real mode for each next read. The 16-bit count
at VBR offset 0x36 allows up to 1024 chunks. Do not increase only a disk-size
constant or stage the whole kernel below 1 MiB: larger kernels would overwrite
the conventional-memory stack, EBDA or VGA regions. The native i386 baseline
and 0x500000 LaunchPad staging boundary are unchanged.

Before disk loading, the VBR enumerates BIOS INT 15h E820 into a zeroed
0x5000–0x57ff handoff area. It accepts 20/24-byte records, skips disabled ACPI
entries, and limits enumeration to 64 calls. Only a complete valid enumeration
sets `MULTIBOOT_FLAG_MMAP`; failures never manufacture RAM from `CONFIG_RAM_MB`.
It passes EAX=0x2BADB002 and EBX=0x5000 to the existing kernel entry. The map
begins at 0x5100 and remains below the boot stack and 0x8000 disk bounce buffer.

The physical allocator validates the complete map before freeing pages, clips
it to IA-32 addressability, counts duplicate RAM ranges once and gives reserved
ranges priority even when they overlap RAM. Guest allocations stay above
16 MiB, separate from the heap, native programs and LaunchPad staging.
`BOOTREPAIR` from updated installation media can install this VBR on recognized
existing volumes without replacing their kernel, moving files or changing
partition sizes; it saves and verifies the original boot sectors first.

### Kernel

| File | Role |
|---|---|
| `kernel/kernel.c` | Initialization order and transition to the shell |
| `kernel/memory.c` | Physical-page bitmap and embedded heap |
| `kernel/gdt.c` | Flat protected-mode GDT |
| `kernel/idt.c` | Interrupt descriptor table |
| `kernel/isr.c` | Exception dispatch and 8259 PIC handling |
| `kernel/timer.c` | 8253/8254 PIT ticks and PC-speaker timing |
| `kernel/keyboard.c` | PS/2 keyboard IRQ and input buffer |
| `kernel/vga.c` | VGA color text console |
| `kernel/ata.c` | ATA compatibility and PCI-native IDE probing |
| `kernel/cdrom.c` | ATAPI PIO CD-ROM operations |
| `kernel/fdc.c` | Floppy disk controller |
| `kernel/fat12.c` | FAT12 floppy reader |
| `kernel/iso9660.c` | ISO 9660 reader |
| `kernel/loader.c` | IPGM and ELF32 application loading |
| `kernel/msdos/` | 486SX-compatible PC and MS-DOS 6.22 platform adapter |
| `kernel/pci.c` | Optional PCI configuration-space scanner |
| `kernel/usb.c`, `kernel/usb_xhci.c` | Optional UHCI/xHCI boot keyboards; enabled in newer profiles |
| `kernel/ac97.c` | Optional PCI audio support |
| `kernel/hda.c` | Optional later audio support |

### Shell and applications

| Path | Role |
|---|---|
| `shell/shell.c` | Command line, history, and dispatch |
| `shell/commands.c` | Built-in commands and plain-English aliases |
| `shell/iedit.c` | Text editor |
| `shell/basic.c` | BASIC interpreter and cassette functions |
| `shell/sheets.c` | Spreadsheet |
| `shell/talk.c`, `shell/sam/` | Speech synthesis |
| `shell/launchpad.c` | Removable-media program browser |
| `shell/filemanager.c` | Filesystem browser |
| `shell/setup.c` | Disk installation utility |
| `shell/tour.c` | Text adventure |
| `tetris/` | Tetris game |

## Boot paths

### GRUB and Multiboot1

GRUB loads the ELF kernel at its linked address, passes `0x2BADB002` in EAX,
places the Multiboot1 information pointer in EBX, and jumps to `_start`.

`boot/boot.asm`:

1. Establishes the kernel stack.
2. Preserves EAX and EBX as C arguments.
3. Calls `kernel_main(magic, info_phys)`.

The kernel uses the full Multiboot memory map when flag bit 6 is present.

### Raw CHS floppy

The raw image layout is:

```text
sector 0      mbr_legacy.bin
sector 1..N   inteilidOS.bin
remaining     zero-filled to 1.44 MB
```

The boot sector uses BIOS `INT 13h AH=08h` to query disk geometry and
`INT 13h AH=02h` to read one sector at a time. Each sector is read into a
low-memory bounce buffer and copied to `0x00100000` through a 4 GB unreal-mode
segment.

Before protected mode, the boot sector obtains:

- conventional memory with `INT 12h`
- extended memory with `INT 15h AH=88h`

It presents these values in the basic Multiboot1 memory fields. The kernel uses
that contiguous-memory fallback when no full memory map exists.

The boot sector must remain exactly 512 bytes:

```bash
nasm -f bin -o /tmp/mbr_legacy.bin boot/mbr_legacy.asm
stat -c %s /tmp/mbr_legacy.bin
od -An -tx1 -j510 -N2 /tmp/mbr_legacy.bin
```

Expected results:

```text
512
55 aa
```

## Kernel initialization

The universal startup order is intentionally conservative:

1. Establish VGA text output.
2. Initialize the GDT and IDT.
3. Remap and enable the dual 8259 PIC.
4. Initialize the PIT.
5. Initialize the PS/2 keyboard.
6. Initialize memory from Multiboot or BIOS totals.
7. Probe optional storage and filesystem support.
8. Start IntelliShell.

Do not make optional PCI or USB probes prerequisites for steps 1–6.

## Memory management

`memory_init()` begins with every physical page marked unavailable.

When a Multiboot memory map exists, entries marked available seed the page
bitmap. When only basic memory values exist, pages from 1 MB through the
reported extended-memory limit are made available. Low memory and pages
occupied by the kernel are then reserved again.

The embedded heap is 2 MB and is independent of page allocation. It uses a
first-fit free list with block splitting and adjacent-block merging.

Important rules:

- `mem_upper` starts at 1 MB; it is not added to conventional memory.
- Keep low memory reserved for BIOS data, the IVT, VGA, and boot structures.
- Do not allocate over memory-map holes.
- Avoid 64-bit division because the kernel does not link a hosted runtime.
- Check multiplication overflow before allocating `count * element_size`.

## Interrupts and timing

The kernel uses the two cascaded 8259 PICs:

```text
IRQ0..IRQ7    vectors 32..39
IRQ8..IRQ15   vectors 40..47
```

IRQ0 is the PIT timer. IRQ1 is the PS/2 keyboard. Drivers must acknowledge the
PIC correctly and keep interrupt handlers short.

Do not add an APIC requirement. A future APIC implementation must be optional
and retain the PIC path.

## Input

PS/2 is the universal input source. `keyboard.c` owns the shared keyboard ring
buffer and supports blocking and non-blocking reads.

Optional input drivers may inject translated keys into that buffer, but shell
and application code must not depend on the optional driver being present.

UHCI code remains in the tree for later systems, but universal startup does not
call its initialization routine.

## Storage

### Boot storage

Legacy IDE drives may report no LBA capacity (including small CHS-only 86Box
disks). Validate IDENTIFY capabilities before using LBA counts; otherwise use
valid current CHS geometry, falling back to valid default geometry. The native
sector API still takes zero-based sector indices, but the PATA task file must
use CHS mode, the correct master/slave and head bits, a one-based sector number,
and the cylinder bytes. Apply this to reads, writes and bulk zeroing. Never
invent a capacity or ignore bounds to make installation proceed.

`tests/ata_chs_test.c` covers these task files, geometry validation and LBA
compatibility. `tests/run_small_hdd_test.py <kernel.elf>` exercises a 10 MiB
IDE slave through clean reinstall and BIOS-only cold boot in QEMU, then mounts
that same image as primary master to check persistent native saves. Native
applications currently assume ATA slot 0 for their internal C: drive; use an
HDD as primary master and CD-ROM as primary slave until that mapping supports
other connectors. QEMU advertises LBA, so the CHS fixture remains necessary.

GRUB and the raw boot sector use BIOS disk access before protected mode. This
lets firmware handle controller-specific boot details.

### Kernel storage

The kernel uses ATA/ATAPI PIO. Start with the standard compatibility ports:

```text
Primary:    0x1F0, control 0x3F6
Secondary:  0x170, control 0x376
```

PCI BARs may be used only when a detected IDE controller explicitly operates in
native mode. Do not require bus mastering or DMA.

FAT12 and ISO 9660 are currently used for removable media. FAT16 and FAT32 are
the preferred directions for broadly exchangeable hard-disk storage.

## Display

The baseline display is the standard VGA text buffer at `0xB8000`. Preserve an
80×25 text fallback even if graphics or VBE support is added later.

Do not require:

- a vendor-specific graphics driver
- a linear framebuffer
- VBE
- PCI VGA enumeration
- hardware acceleration

## Adding code safely

### C rules

- Use `stdint.h` fixed-width integer types for hardware values.
- Use `size_t` for object sizes and counts.
- Avoid floating point.
- Avoid compiler built-ins that assume a hosted runtime.
- Mark memory-mapped and port-observed state `volatile` where required.
- Bound all hardware polling loops with timeouts.
- Return explicit errors when optional hardware is absent.
- Keep files focused and add new source files to `CMakeLists.txt`.

### Assembly rules

- Use only 386-safe instructions in universal startup code.
- Preserve registers according to the calling convention.
- Use explicit `[BITS 16]` and `[BITS 32]` sections.
- Keep interrupt state transitions clear.
- Never execute CPUID without first checking whether EFLAGS.ID can be toggled.
- Reassemble and size-check a boot sector after every change.

### Optional hardware

An optional driver must:

1. Detect its controller without hanging on absent hardware.
2. Leave existing universal drivers operational.
3. Time out instead of polling forever.
4. Report unsupported hardware clearly when invoked.
5. Avoid changing the minimum CPU requirement.

## Adding a shell command

1. Implement the handler in `shell/commands.c` or a focused source file.
2. Parse arguments without a hosted C library.
3. Add dispatch entries and help text.
4. If interactive, support Escape or `QUIT` consistently.
5. Add new source files to `SHELL_C_SOURCES` in `CMakeLists.txt`.

Example:

```c
static int cmd_example(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    println("Example command");
    return 0;
}
```

## inteiliWriter

The built-in `WRITER`/`INTEILIWRITER`/`DOCS` app separates keyboard/storage/VGA
integration in `shell/writer.c` from bounded text editing and PDF generation
in `shell/writer_document.c`. It uses the existing logical-volume and USB
APIs and shared storage dialogs. F8/F9 independently toggle the style of new
characters; per-character style bytes follow insertion, deletion and wrapping.
Workspace is allocated only while the app
is open and released on exit. iEdit's scratch I/O buffer also uses on-demand
allocation to preserve the fixed kernel/LaunchPad staging boundary.

`shell/writer_fonts.c` snapshots VGA font-plane data and builds bold, italic
and bold-italic glyphs in two text font banks. It restores the original plane,
font-map selection and changed palette entry on exit. The QEMU test compares
actual glyph pixels and shell rendering before/after, not just text VRAM.

Markdown is the styled editable interchange format: emphasis is nested,
literal stars/backslashes are escaped, and loads validate decoded capacity
before replacing the open document. TXT deliberately strips formatting and
editable TXT saves warn before doing so.

The PDF serializer emits uncompressed PDF 1.4 with exact stream lengths,
escaped literal strings, page objects and byte-offset cross references.
It switches among Courier's four base fonts for character style runs.
It uses no host renderer or external runtime. TXT/Markdown are reloadable;
PDF is export-only. All output must fit before a storage write is attempted.

Validation:

```sh
python3 inteiliDOS/tests/test_writer.py
python3 inteiliDOS/tests/run_writer_test.py
```

The host suite tests cursor movement, transactional loads, buffer limits,
key-driven save/load/delete, error paths and independent PDF parsing.
The QEMU test boots the complete kernel, writes disposable FAT USB and
native files, compares their bytes, parses the PDF and checks iEdit.

## MS-DOS 6.22 emulator

`MS-DOS /CDROM` adds an opt-in, read-only D: for game files on an ISO 9660
SATA/IDE optical disc. `kernel/msdos/cdrom.c` synthesizes bounded FAT16 metadata
and streams file sectors through the native CD reader. The checked ROM INT13
entry bridge presents BIOS drive 81h, leaving the persistent private DOS
HDD at 80h. Guest writes to the optical view are refused; native AHCI engines
remain owned by the shared SATA driver. This is a file bridge, not MSCDEX or
CD-audio emulation. Keep its limits and DOS 8.3 aliases documented in
`DOS_COMPATIBILITY.md`.

Run `python3 tests/test_msdos_cdrom.py` for host filesystem regressions and
`python3 tests/run_msdos_cdrom_test.py` for real DOS game/resource access,
read-only refusal, private C: saves and optical/HDD coexistence on AHCI.

`MS-DOS` in inteiliShell enters a software-interpreted 486SX-compatible PC running
the user's supplied DOS 6.22 system. F8 returns to the native shell without
rebooting the host. DOS COM/MZ programs now run inside that environment;
LaunchPad detects DOS files and directs users to the command instead of
executing them through the old partial INT 21h/VM86 monitor. Native IPGM/ELF
loading is unchanged. `RUN /DOS` likewise redirects to `MS-DOS`.

For build setup, controls, installation examples and troubleshooting, see
[DOS_COMPATIBILITY.md](DOS_COMPATIBILITY.md).

### Implementation map

| File | Responsibility |
|---|---|
| `shell/commands.c` | Dispatch `MS-DOS` to `msdos_run` and advertise it in HELP |
| `kernel/msdos/msdos.h` | Command entry point and CPU/platform/device hooks |
| `kernel/msdos/tiny.c` | Adapted 8086tiny interpreter, private CPU/port state, virtual interrupts and bounded BIOS transfers |
| `kernel/msdos/real286.c`, `real286.h` | Real-mode 286 FLAGS, shift/rotate semantics and MSW/descriptor-table state |
| `kernel/msdos/bios.h` | Embedded companion BIOS ROM, not a native BIOS thunk |
| `kernel/msdos/platform.c` | Persistent image discovery/creation, DOS seeding, media reads, keyboard, RTC and ANSI text bridge |
| `kernel/msdos/devices.c` | Guest raster presentation, speaker adapter and graphics/font/palette restoration |
| `kernel/msdos/sound.c`, `sound.h` | Private 8-bit DSP, DMA1 and IRQ5/PIC model |
| `kernel/pcm.c`, `pcm.h` | Bounded polled Intel ICH AC'97 / analog HDA native playback |
| `kernel/msdos/video.c`, `video.h` | Private EGA/VGA planes, registers, latches, DAC, BIOS services and pixel scanout |
| `kernel/msdos/mbr.asm`, `mbr.h` | Virtual disk bootstrap source and generated bytes |
| `kernel/fs.c`, `fs.h` | Validated image extent reservation, publication and bounded sector I/O |
| `kernel/ata.c`, `ata.h` | Native sector I/O and batched zero initialization |
| `cmake/prepare_msdos622.py` | Validate and copy the four supplied images into the ISO |
| `tests/run_msdos622_test.py`, `msdos622_fixture.asm` | Complete-OS integration test and 80186/CGA/speaker COM fixture |

The CPU, platform and device sources are included in `KERNEL_C_SOURCES`.
The OS ISO build never includes DOS disk images, even from an old cached
`MSDOS_DISK_DIR`. Old automatically staged `D622*.IMG` files are removed.
Users supply DOS separately on a data CD or a physical/emulated 1.44 MB floppy.
The optional `cmake/create_msdos_cd.py` utility validates four user-owned
images and makes a separate DOS-only CD. The supplied reference images are
outside the OS folder in `../MS-DOS_disks`; no DOS binaries are linked into
the kernel or downloaded by the OS build.

### Guest execution and BIOS interface

The native kernel stays in protected mode and retains its IRQ handlers.
`msdos_cpu_run` allocates and zeroes 16 MiB of private guest RAM,
initializes the guest registers/port array, and enters the companion BIOS at
F000:0100 with DL=80h for hard-disk boot. The guest sees 640 KiB conventional
memory and extended RAM within its 16 MiB address space. Guest RAM is discarded
on exit; installed files persist on disk.

Instructions are interpreted, not executed in host VM86 mode. The companion
ROM provides instruction decode tables and BIOS services; its private
`0F 00` through `0F 03` extensions bridge text output, RTC, disk reads and disk
writes to the native adapter. Transfer addresses/lengths are checked against
guest RAM and virtual disk bounds before any native I/O.

Keep the ROM decode tables and interpreter semantics consistent. The
adaptation explicitly implements the ROM's 80186 ENTER/LEAVE, PUSHA/POPA,
immediate PUSH/IMUL entries and their operand/instruction lengths. Do not
assume an upstream ROM/core combination provides every advertised decode
entry. Unsupported newer/FPU instructions return an error rather than
switching to native execution.

DOS itself supplies INT 21h, process loading, PSP/environment management,
file operations, batch commands, and nested EXEC. Do not extend the obsolete
LaunchPad DOS shim and claim that this adds services to the new emulator.
The legacy source/test files still exist, but are not the current user-facing
DOS launch path.

### Persistent HDD layout and safety

`MSDOS622.HDD` is one native filesystem file with 32,760 512-byte sectors.
Its private MBR describes a FAT16 partition at virtual LBA 1, with 4 sectors
per cluster, two 32-sector FATs, a 512-entry root directory, and data starting
at virtual LBA 98. The BIOS geometry is 520 cylinders, one head, 63 sectors
per track. Guest LBAs are translated relative to the reserved native extent.
No guest sector number may address a physical disk directly.

Startup searches for an existing image on valid native filesystems. If none
exists, it reserves space on an already installed filesystem, initializes the
extent with bounded native ATA writes, installs the boot/system files from
Disk 1, and writes a minimal `AUTOEXEC.BAT`. Microsoft's physical-machine
SETUP and DriveSpace startup are excluded. Publish the native directory entry
only after successful initialization.

`fs_image_open/reserve/publish` validate filesystem magic, terminated names,
extent bounds, directory capacity and overlapping allocations. Image-sector
operations enforce the private extent and native drive capacity. An existing
image with the wrong size is refused, not silently reformatted. Ordinary
`fs_write` must not truncate the large image as a native document.

ATA bulk zeroing batches sector writes and cache flushes. Preserve bounded
BSY/DRQ waits; sustained writes require time-based deadlines rather than a
small CPU-dependent spin count. Do not bypass extent validation to accelerate
initialization. A setup failure/cancellation may leave data in unallocated
space, but must not publish an incomplete image or overwrite native files.

Completed sector writes survive F8 and host restarts. This is not a
transactional guest filesystem or application checkpoint: F8 can interrupt
guest-buffered writes or a multi-sector FAT operation. Document the need to
save and close applications before aborting.

### Removable media and memory budget

A: is read-only. ISO-backed images are read on demand through a cached 2048-byte
ISO sector; do not allocate an entire 1.44 MB image alongside guest RAM in the
native embedded heap. F6 cycles the supplied media and F7 selects the physical
floppy. `/FLOPPY` supports initial seeding directly from DOS Disk 1 in
the floppy drive; shared sector reads handle partial file tails as well as
BIOS transfers. The native floppy controller is initialized on first physical
read and reinitialized after F7 selects physical media; firmware state is not
assumed. Saved C: startup alone never initializes or requires a floppy drive.
Plain startup tries the floppy if no DOS Disk 1 CD is present.
`/IMAGE` selects a CD-root 360 KB/720 KB/1.2 MB/1.44 MB game image without requiring any
DOS installation images on that CD. CD installation images may use `D622*.IMG`
or `DISK1.IMG`/`DISK2.IMG`/`DISK3.IMG`/`SUPPDISK.IMG`.
The BIOS media-change notification invalidates
DOS's cached floppy directory after a swap.

First physical use probes the data rate and validates a standard 512-byte,
two-head DOS BPB. CHS conversion, EOT and capacity follow the detected format.
360 KB media uses double stepping only in a CMOS-declared 1.2 MB 5.25-inch
drive. Seek/recalibrate completion requires successful seek-end and the
requested physical cylinder; abnormal status is not cached as success.
The guest BIOS reports actual media parameters and change-line support.
Physical media changes currently require F7 to invalidate the cached geometry.

There is no guest ISO9660 or ATAPI/MSCDEX CD-ROM driver and no D: CD mapping.
Native CD/floppy reads supply the emulated floppy; failures become guest
disk errors. Neither physical nor image-backed A: permits writes.

### Device isolation and exit restoration

Host keyboard polling occurs every 2,000 interpreted instructions. Virtual
timer/keyboard service runs on an instruction budget rather than cycle-exact
hardware timing; RTC data is obtained through the platform adapter. F8 is
checked independently of guest IF, so guest CLI and HLT cannot disable the
native escape path. Native I/O operations still use their bounded driver waits.

Guest port reads/writes update private state; never forward raw PIC/PIT or
keyboard-controller programming to host hardware. Translate speaker channel-2
state through bounded native speaker calls, and stop audio on every exit.

Text is translated through an ANSI terminal buffer. CGA/Hercules graphics
and EGA/VGA modes 0Dh–13h are presented through native VGA mode 13h.
The complete guest raster is fitted to 320x200; higher resolutions are
downsampled. `video.c` models four 64 KiB planes, read/write modes, chain-4,
Mode X, indexed registers and the six-bit DAC. Route architectural memory
reads/writes through the CPU bus, not direct RAM accesses: writes that appear
unchanged in RAM can still change planes, and MOV must preserve source
latches rather than read its destination. The original INT 10h ROM entry is
baseline-checked and intercepted for both INT and chained far-call paths.
Before starting the guest, allocate video RAM on the heap and save the native font
plane and DAC palette. On return, leave chain-4 graphics **before** mapping
and rewriting the font plane, then restore text register mapping and palette.
Restore the saved native text cells and cursor. A correct B8000 character
buffer does not prove intact glyphs: inspect a rendered return-to-shell
screenshot as well.

### Testing, limits and attribution

From this source directory:

```sh
python3 tests/build_test_kernel.py /tmp/msdos622-build
python3 tests/run_msdos622_test.py /tmp/msdos622-build/inteilidOS.elf
python3 tests/run_msdos_video_test.py /tmp/msdos622-build/inteilidOS.elf
python3 tests/run_msdos_real286_test.py /tmp/msdos622-build/inteilidOS.elf
```

The test requires the four separately supplied disks in `../MS-DOS_disks`, QEMU,
xorriso, NASM, Python 3 and the GCC/binutils build tools. It verifies DOS 6.22,
COM and MZ execution, installation and persistence after re-entry/reboot,
F8 through CLI, 80186 instructions, graphics/speaker handling, CD-hosted game
images, physical floppy reads, media swaps and native data preservation.
Unformatted or too-small physical disks must remain byte-for-byte unchanged.
Screenshots go to the ignored repository-level `generated-artifacts/msdos622`.
The build helper also checks that the kernel does not overlap native program
staging at 0x500000.

Legacy VM86 integration tests are not acceptance tests for this command.
QEMU results do not certify 86Box, real hardware, arbitrary DOS software,
or audible sound quality. Extend the current regression fixture when changing
instruction decoding, BIOS hooks, media change handling or native restoration.

The guest targets 486SX compatibility, including protected mode, virtual-8086
mode and paging. DOS extenders/DPMI hosts and EMS/XMS managers are user-supplied
guest software, not built-in services. No x87, VESA/SVGA, FM/MIDI or network
stack is supplied by the CPU upgrade.
Protected-mode game compatibility remains experimental: the CPU fixture
passes, but the optional D386MYH/CWSDPR0 end-to-end game test does not reach VGA.
EGA/VGA graphics are emulated, but cycle-accurate retrace, arbitrary adapter BIOS
extensions and VGA state-save/restore are not. Test specific games rather than
assuming they work because their display mode is implemented.

`CONFIG_MSDOS_SOUND` defaults to zero, including non-interactive presets. The
interactive wizard offers it immediately after CPU classes 7–9 are chosen.
Enabling it also enables PCI probing; it is independent of CLOAD's
`CONFIG_AUDIO_MODE`. AC'97 and analog HDA output are verified by QEMU PCM
recordings. HDA keeps INTCTL zero while enabling the RIRB response-status
latch: with RINTCNT=1, QEMU stalls further verbs until RINTFL is acknowledged.
RIRB entries are volatile DMA data, consumed in order, excluding unsolicited
events and other-codec replies. Stop the output stream, disconnect the codec
while command DMA is live, then stop CORB/RIRB and restore PCI command state.
`sound.c` implements only 8-bit digital playback, with no
ADC, OPL/FM, MIDI or full SB16. Device I/O must remain private even though the
native renderer uses PCI audio DMA. Do not send guest DMA/PIC writes to hardware.
Keep AC'97/HDA DMA bounded to static native buffers, invalidate capture state
before reusing its controller, and stop output on every exit. Native output
support and codec topology must be verified separately from DSP/IRQ unit tests.

`tiny386/i386.c` supplies the generation-4 CPU. `core486.c` adapts it to the
existing devices and checked BIOS hooks in `tiny.c`. No guest instruction or
port access executes on native hardware. Page-table walks, cross-boundary RAM
accesses and VGA accesses must remain bounded to guest memory.
Unrecoverable CPU-core errors return to the command; they must not abort the OS.
Private ROM `0F` calls are recognized only at original checked firmware sites.
Long REP operations must remain restartable and yield to native F8/audio polling.
The CPU does not include an x87 FPU, exhaustive protection checks or hardware
task switching; test individual extenders rather than claiming universal compatibility.

The 16 MiB RAM region comes from tracked physical pages above 16 MiB, not the
2 MiB heap. Free it on every exit. CPU/TLB/port workspaces use the heap.
The page bitmap also uses the existing heap so the new core does not exceed
LaunchPad's unchanged kernel/staging boundary. See the CPU host and QEMU
fixtures in `tests/msdos_486*` and `tests/run_msdos_486_test.py`.

Retain the MIT license for Adrian Cable's adapted 8086tiny interpreter and
companion ROM in `kernel/msdos/LICENSE.txt`. Microsoft's supplied DOS system
files retain their original license; the emulator license does not license DOS.
Retain Chunhui He's BSD-3-Clause notice in `kernel/msdos/tiny386/LICENSE`.
See `kernel/msdos/tiny386/README.md` for the pinned source and port changes.
See `kernel/msdos/README.md` for upstream provenance.

## Validation

### Optional DOS mouse

`MS-DOS /MOUSE` enables the built-in INT 33h subset using a native PS/2
mouse/touchpad. Media selection is independent of the switch. Mouse packets
are routed by the shared 8042 status AUX bit, never through the keyboard
scancode decoder. Guest mouse services never access native ports.

The resident entry must be installed after DOS initializes reserved vectors,
at its first EXEC rather than BIOS boot. Guest vector replacement and
PUSHF/FAR CALL chaining remain possible. Event handlers are non-nested RETF
callbacks; restore interrupted registers and flags. Cursor rendering must
remain an overlay, leaving guest framebuffer contents untouched. Native
reporting is stopped on F8 and other emulator exits.

Tests (all created disk fixtures are disposable):

```sh
gcc -std=gnu11 -O1 -g -fsanitize=address,undefined \
  tests/msdos_mouse_test.c kernel/msdos/mouse.c -o /tmp/msdos-mouse-test
ASAN_OPTIONS=detect_leaks=0 /tmp/msdos-mouse-test
python3 tests/run_msdos_mouse_test.py /path/to/test-kernel.elf
# For a modern USB-keyboard/storage-enabled test kernel:
python3 tests/run_msdos_mouse_test.py /path/to/modern-test-kernel.elf --usb
```

The QEMU test needs separately supplied DOS media. It checks INT 33h
detection, far-call chaining, physical PS/2 movement/buttons, callbacks,
pointer rendering, native font restoration, F8, re-entry, and opt-in state.
The USB variant also checks combined USB game media/PS/2 mouse input and
bounded failure with the PS/2 controller absent. It does not certify native
USB mouse support or physical motherboard/touchpad compatibility.

### Compile all C for i386

Use the same flags as CMake and compile every translation unit. Warnings should
be reviewed even when they do not stop the build.

### Scan generated instructions

```bash
i686-elf-objdump -d build_1990s/inteilidOS.elf |
  grep -Ei '\b(cmov|cpuid|rdtsc|rdmsr|wrmsr|sysenter|sysexit|xmm[0-9]|mm[0-7])\b'
```

No match is expected for the universal kernel.

### Emulator matrix

At minimum, test:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 -m 16

qemu-system-i386 \
  -drive file=build_1990s/inteiliDOS_1990s_floppy.img,format=raw,if=floppy \
  -cpu 486 -m 16
```

Also test a Pentium-class profile and at least one ISA-focused emulator such as
86Box or PCem.

### Real hardware matrix

Before a compatibility release, test:

- one 386-class PC if available
- one 486-class PC
- one early Pentium PC
- one late-1990s PCI PC
- floppy boot
- BIOS CD boot
- PS/2 input
- IDE compatibility-mode storage

Record the firmware, RAM amount, display adapter, storage controller, boot
medium, and result.

## Known limitations

- No native SCSI stack
- No universal network driver
- No VBE graphics mode
- No APIC mode
- No USB startup guarantee
- No host BIOS thunk or native VM86 disk passthrough; MS-DOS uses software-emulated BIOS disk services
- Basic BIOS memory totals on raw floppy boots rather than a complete E820 map
- Controller-specific audio support is optional
- Persistent hard-disk filesystem support is still limited

## Contribution checklist

Before submitting a change:

- [ ] The universal build still uses `-march=i386`.
- [ ] No post-386 instruction is emitted.
- [ ] The CHS boot sector is still 512 bytes with a `55 AA` signature.
- [ ] PS/2, PIC, PIT, and VGA text paths remain independent.
- [ ] Optional hardware probes have timeouts.
- [ ] The ISO and floppy image both build.
- [ ] QEMU boots with `-cpu 486 -m 16`.
- [ ] Documentation uses `build_1990s` and current image names.
- [ ] Hardware-specific behavior is described as optional.

## Attribution

Redistributed source or binary builds must clearly credit Inteilix Software
Corporation as the original author of inteiliDOS.# Developing inteiliDOS

This guide describes the universal 32-bit x86 edition. Its lowest supported
instruction set is Intel 80386, and its required hardware interfaces are the
standard PC-compatible BIOS, VGA text mode, dual 8259 PIC, 8253/8254 PIT, and
8042/PS/2 keyboard controller.

## Design rules

New code must preserve these constraints:

1. Do not emit instructions newer than the Intel 80386 ISA.
2. Do not execute CPUID unless availability has first been detected safely.
3. Do not require an FPU, MMX, SSE, CMOV, MSRs, SYSENTER, or APIC.
4. Keep the dual 8259 PIC and PIT paths operational.
5. Keep PS/2 keyboard input independent of PCI and USB.
6. Keep VGA text mode as the display fallback.
7. Prefer BIOS boot services and ATA compatibility-mode PIO over
   chipset-specific DMA during startup.
8. Treat PCI, USB, ACPI, and controller-specific audio as optional.
9. Avoid assuming that CD boot, LBA extensions, or a PCI bus exists.
10. Preserve the raw CHS floppy boot path.

## Build architecture

The project uses an `i686-elf` cross-toolchain because it provides freestanding
32-bit ELF tools. The compiler target is still restricted to i386:

```cmake
-m32
-march=i386
-mtune=i386
-O1
-msoft-float
-mno-mmx
-mno-sse
-mno-sse2
-nostdlib
-ffreestanding
-fno-builtin
-fno-stack-protector
-fno-pic
-fno-pie
```

The cross-toolchain prefix does not define the minimum processor. The
`-march=i386` option does.

Build with:

```bash
./build.sh
```

The script always configures:

```text
BUILD_TARGET=universal
BUILD_DIR=build_1990s
```

Main products:

```text
build_1990s/inteilidOS.elf
build_1990s/inteilidOS.bin
build_1990s/inteiliDOS_1990s.iso
build_1990s/inteiliDOS_1990s_floppy.img
```

## Source layout

### Boot code

| File | Role |
|---|---|
| `boot/boot.asm` | Multiboot1 header and protected-mode kernel entry |
| `boot/mbr.asm` | BIOS extended-read boot sector used by disk installation |
| `boot/mbr_legacy.asm` | Universal CHS floppy boot sector |
| `boot/gdt_flush.asm` | Loads the GDT and reloads segment registers |
| `boot/idt_load.asm` | Loads the IDT |
| `boot/isr_stubs.asm` | Exception and IRQ assembly stubs |

### Kernel

| File | Role |
|---|---|
| `kernel/kernel.c` | Initialization order and transition to the shell |
| `kernel/memory.c` | Physical-page bitmap and embedded heap |
| `kernel/gdt.c` | Flat protected-mode GDT |
| `kernel/idt.c` | Interrupt descriptor table |
| `kernel/isr.c` | Exception dispatch and 8259 PIC handling |
| `kernel/timer.c` | 8253/8254 PIT ticks and PC-speaker timing |
| `kernel/keyboard.c` | PS/2 keyboard IRQ and input buffer |
| `kernel/vga.c` | VGA color text console |
| `kernel/ata.c` | ATA compatibility and PCI-native IDE probing |
| `kernel/cdrom.c` | ATAPI PIO CD-ROM operations |
| `kernel/fdc.c` | Floppy disk controller |
| `kernel/fat12.c` | FAT12 floppy reader |
| `kernel/iso9660.c` | ISO 9660 reader |
| `kernel/loader.c` | IPGM and ELF32 application loading |
| `kernel/pci.c` | Optional PCI configuration-space scanner |
| `kernel/usb.c`, `kernel/usb_xhci.c` | Optional UHCI/xHCI boot keyboards; enabled in newer profiles |
| `kernel/ac97.c` | Optional PCI audio support |
| `kernel/hda.c` | Optional later audio support |

### Shell and applications

| Path | Role |
|---|---|
| `shell/shell.c` | Command line, history, and dispatch |
| `shell/commands.c` | Built-in commands and plain-English aliases |
| `shell/iedit.c` | Text editor |
| `shell/basic.c` | BASIC interpreter and cassette functions |
| `shell/sheets.c` | Spreadsheet |
| `shell/talk.c`, `shell/sam/` | Speech synthesis |
| `shell/launchpad.c` | Removable-media program browser |
| `shell/filemanager.c` | Filesystem browser |
| `shell/setup.c` | Disk installation utility |
| `shell/tour.c` | Text adventure |
| `tetris/` | Tetris game |

## Boot paths

### GRUB and Multiboot1

GRUB loads the ELF kernel at its linked address, passes `0x2BADB002` in EAX,
places the Multiboot1 information pointer in EBX, and jumps to `_start`.

`boot/boot.asm`:

1. Establishes the kernel stack.
2. Preserves EAX and EBX as C arguments.
3. Calls `kernel_main(magic, info_phys)`.

The kernel uses the full Multiboot memory map when flag bit 6 is present.

### Raw CHS floppy

The raw image layout is:

```text
sector 0      mbr_legacy.bin
sector 1..N   inteilidOS.bin
remaining     zero-filled to 1.44 MB
```

The boot sector uses BIOS `INT 13h AH=08h` to query disk geometry and
`INT 13h AH=02h` to read one sector at a time. Each sector is read into a
low-memory bounce buffer and copied to `0x00100000` through a 4 GB unreal-mode
segment.

Before protected mode, the boot sector obtains:

- conventional memory with `INT 12h`
- extended memory with `INT 15h AH=88h`

It presents these values in the basic Multiboot1 memory fields. The kernel uses
that contiguous-memory fallback when no full memory map exists.

The boot sector must remain exactly 512 bytes:

```bash
nasm -f bin -o /tmp/mbr_legacy.bin boot/mbr_legacy.asm
stat -c %s /tmp/mbr_legacy.bin
od -An -tx1 -j510 -N2 /tmp/mbr_legacy.bin
```

Expected results:

```text
512
55 aa
```

## Kernel initialization

The universal startup order is intentionally conservative:

1. Establish VGA text output.
2. Initialize the GDT and IDT.
3. Remap and enable the dual 8259 PIC.
4. Initialize the PIT.
5. Initialize the PS/2 keyboard.
6. Initialize memory from Multiboot or BIOS totals.
7. Probe optional storage and filesystem support.
8. Start IntelliShell.

Do not make optional PCI or USB probes prerequisites for steps 1–6.

## Memory management

`memory_init()` begins with every physical page marked unavailable.

When a Multiboot memory map exists, entries marked available seed the page
bitmap. When only basic memory values exist, pages from 1 MB through the
reported extended-memory limit are made available. Low memory and pages
occupied by the kernel are then reserved again.

The embedded heap is 2 MB and is independent of page allocation. It uses a
first-fit free list with block splitting and adjacent-block merging.

Important rules:

- `mem_upper` starts at 1 MB; it is not added to conventional memory.
- Keep low memory reserved for BIOS data, the IVT, VGA, and boot structures.
- Do not allocate over memory-map holes.
- Avoid 64-bit division because the kernel does not link a hosted runtime.
- Check multiplication overflow before allocating `count * element_size`.

## Interrupts and timing

The kernel uses the two cascaded 8259 PICs:

```text
IRQ0..IRQ7    vectors 32..39
IRQ8..IRQ15   vectors 40..47
```

IRQ0 is the PIT timer. IRQ1 is the PS/2 keyboard. Drivers must acknowledge the
PIC correctly and keep interrupt handlers short.

Do not add an APIC requirement. A future APIC implementation must be optional
and retain the PIC path.

## Input

PS/2 is the universal input source. `keyboard.c` owns the shared keyboard ring
buffer and supports blocking and non-blocking reads.

Optional input drivers may inject translated keys into that buffer, but shell
and application code must not depend on the optional driver being present.

UHCI code remains in the tree for later systems, but universal startup does not
call its initialization routine.

## Storage

### Boot storage

GRUB and the raw boot sector use BIOS disk access before protected mode. This
lets firmware handle controller-specific boot details.

### Kernel storage

The kernel uses ATA/ATAPI PIO. Start with the standard compatibility ports:

```text
Primary:    0x1F0, control 0x3F6
Secondary:  0x170, control 0x376
```

PCI BARs may be used only when a detected IDE controller explicitly operates in
native mode. Do not require bus mastering or DMA.

FAT12 and ISO 9660 are currently used for removable media. FAT16 and FAT32 are
the preferred directions for broadly exchangeable hard-disk storage.

## Display

The baseline display is the standard VGA text buffer at `0xB8000`. Preserve an
80×25 text fallback even if graphics or VBE support is added later.

Do not require:

- a vendor-specific graphics driver
- a linear framebuffer
- VBE
- PCI VGA enumeration
- hardware acceleration

## Adding code safely

### C rules

- Use `stdint.h` fixed-width integer types for hardware values.
- Use `size_t` for object sizes and counts.
- Avoid floating point.
- Avoid compiler built-ins that assume a hosted runtime.
- Mark memory-mapped and port-observed state `volatile` where required.
- Bound all hardware polling loops with timeouts.
- Return explicit errors when optional hardware is absent.
- Keep files focused and add new source files to `CMakeLists.txt`.

### Assembly rules

- Use only 386-safe instructions in universal startup code.
- Preserve registers according to the calling convention.
- Use explicit `[BITS 16]` and `[BITS 32]` sections.
- Keep interrupt state transitions clear.
- Never execute CPUID without first checking whether EFLAGS.ID can be toggled.
- Reassemble and size-check a boot sector after every change.

### Optional hardware

An optional driver must:

1. Detect its controller without hanging on absent hardware.
2. Leave existing universal drivers operational.
3. Time out instead of polling forever.
4. Report unsupported hardware clearly when invoked.
5. Avoid changing the minimum CPU requirement.

## Adding a shell command

1. Implement the handler in `shell/commands.c` or a focused source file.
2. Parse arguments without a hosted C library.
3. Add dispatch entries and help text.
4. If interactive, support Escape or `QUIT` consistently.
5. Add new source files to `SHELL_C_SOURCES` in `CMakeLists.txt`.

Example:

```c
static int cmd_example(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    println("Example command");
    return 0;
}
```

## Validation

### Compile all C for i386

Use the same flags as CMake and compile every translation unit. Warnings should
be reviewed even when they do not stop the build.

### Scan generated instructions

```bash
i686-elf-objdump -d build_1990s/inteilidOS.elf |
  grep -Ei '\b(cmov|cpuid|rdtsc|rdmsr|wrmsr|sysenter|sysexit|xmm[0-9]|mm[0-7])\b'
```

No match is expected for the universal kernel.

### Emulator matrix

At minimum, test:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 -m 16

qemu-system-i386 \
  -drive file=build_1990s/inteiliDOS_1990s_floppy.img,format=raw,if=floppy \
  -cpu 486 -m 16
```

Also test a Pentium-class profile and at least one ISA-focused emulator such as
86Box or PCem.

### Real hardware matrix

Before a compatibility release, test:

- one 386-class PC if available
- one 486-class PC
- one early Pentium PC
- one late-1990s PCI PC
- floppy boot
- BIOS CD boot
- PS/2 input
- IDE compatibility-mode storage

Record the firmware, RAM amount, display adapter, storage controller, boot
medium, and result.

## Known limitations

- No native SCSI stack
- No universal network driver
- No VBE graphics mode
- No APIC mode
- No USB startup guarantee
- No protected-mode BIOS thunk or VM86 disk service
- Basic BIOS memory totals on raw floppy boots rather than a complete E820 map
- Controller-specific audio support is optional
- Persistent hard-disk filesystem support is still limited

## Contribution checklist

Before submitting a change:

- [ ] The universal build still uses `-march=i386`.
- [ ] No post-386 instruction is emitted.
- [ ] The CHS boot sector is still 512 bytes with a `55 AA` signature.
- [ ] PS/2, PIC, PIT, and VGA text paths remain independent.
- [ ] Optional hardware probes have timeouts.
- [ ] The ISO and floppy image both build.
- [ ] QEMU boots with `-cpu 486 -m 16`.
- [ ] Documentation uses `build_1990s` and current image names.
- [ ] Hardware-specific behavior is described as optional.

## Attribution

Redistributed source or binary builds must clearly credit Inteilix Software
Corporation as the original author of inteiliDOS.