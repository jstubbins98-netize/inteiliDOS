# Developing inteiliDOS for 1990s Computers

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
profile because optional UHCI detection cannot guarantee a working USB
keyboard. The wizard offers legacy IDE/PATA or disabled storage; unsupported
native PCI IDE, AHCI, and SCSI modes are not presented as working choices.

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
| `kernel/usb.c` | Optional UHCI support; not initialized universally |
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
Corporation as the original author of inteiliDOS.# Developing inteiliDOS for 1990s Computers

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
| `kernel/usb.c` | Optional UHCI support; not initialized universally |
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