# Testing inteiliDOS for 1990s Computers

This guide lists the commands, environments, and physical machines that can be
used to test the universal Intel 80386-compatible edition.

Run host-side commands from:

```bash
cd "inteiliDOS for 1990s computers"
```

## 1. Test environments

### Host-side checks

Use a Linux, macOS, or Replit development environment to:

- build the kernel and boot images
- assemble and size-check boot sectors
- compile every C source file for i386
- inspect generated machine instructions
- inspect ELF headers, sections, and symbols

### QEMU

QEMU is the quickest place to test:

- GRUB ISO boot
- raw floppy boot
- direct Multiboot kernel boot
- IDE hard disks
- ATAPI CD-ROM drives
- PS/2 keyboard input
- low-memory configurations
- reboot and shutdown behavior
- GDB debugging

QEMU commonly provides a 486 CPU model rather than a 386 model. Testing with
`-cpu 486` checks old-CPU behavior, while instruction scanning confirms that
the binary remains limited to the i386 ISA.

### 86Box

86Box can test more realistic configurations:

- 386DX and 386SX processors
- 486 processors
- ISA VGA adapters
- period-correct BIOS implementations
- ISA and PCI IDE controllers
- floppy controllers and disk timing
- low RAM amounts

Create separate virtual machines for a 386DX, 486DX, Pentium, and late-1990s
PCI computer.

### PCem

PCem can test:

- 386 and 486 processor behavior
- chipset-specific BIOS behavior
- ISA VGA and Super VGA adapters
- physical-style floppy and IDE timing
- early CD-ROM boot compatibility

### VirtualBox and VMware

VirtualBox and VMware are useful for general ISO boot and application testing.
They are not substitutes for a 386/486 emulator because their virtual CPUs and
hardware are much newer.

Suggested VM configuration:

```text
Operating system type: Other/DOS or Other 32-bit
RAM:                   16–64 MB
Boot device:           Optical drive
Input:                 PS/2 keyboard
Storage:               IDE controller
```

### Physical computers

Test on as many of these categories as possible:

1. 386DX with ISA VGA, PS/2 or AT keyboard compatibility, and a floppy drive
2. 386SX with at least 4 MB RAM
3. 486DX or 486DX2 with ISA IDE
4. Early Pentium with IDE and BIOS CD boot
5. Pentium MMX with PCI IDE
6. Pentium II or Pentium III with IDE compatibility mode
7. One ISA-only system
8. One PCI system
9. One machine that boots from floppy only
10. One machine that boots from CD-ROM

Record the following for each physical test:

```text
Computer or motherboard:
CPU:
RAM:
BIOS vendor and version:
Graphics adapter:
Storage controller:
Boot medium:
Keyboard:
Boot result:
Failed command or subsystem:
Notes:
```

## 2. Build tests

### Test Willburd's interactive questionnaire

```bash
./build.sh --config-only
```

For every question:

1. Enter `H`.
2. Confirm Willburd explains how to identify the hardware.
3. Enter a valid answer.
4. Inspect `configurations/config.h`.

Confirm that the profile name and all selected values appear as `CONFIG_*`
macros.

Confirm that:

- PS/2 remains enabled even when optional USB support is selected
- the PIT rate remains 1000 Hz
- unsupported storage disables ATA and ATAPI together
- choosing floppy builds the floppy target
- choosing CD-ROM builds the ISO target
- choosing both builds both targets

### Generate safe defaults without prompts

```bash
./build.sh --defaults --config-only
```

### Clean universal build

```bash
./build.sh --clean
```

### Incremental universal build

```bash
./build.sh
```

The script runs Willburd's questionnaire, generates
`configurations/config.h`, checks the required tools, and configures
`BUILD_TARGET=universal`.

For an unattended clean build:

```bash
./build.sh --defaults --clean
```

Expected files:

```text
build_1990s/inteilidOS.elf
build_1990s/inteilidOS.bin
build_1990s/inteiliDOS_1990s.iso
build_1990s/inteiliDOS_1990s_floppy.img
```

Check that they exist:

```bash
test -s build_1990s/inteilidOS.elf
test -s build_1990s/inteilidOS.bin
test -s build_1990s/inteiliDOS_1990s.iso
test -s build_1990s/inteiliDOS_1990s_floppy.img
```

Check the floppy image size:

```bash
stat -c '%s' build_1990s/inteiliDOS_1990s_floppy.img
```

Expected size:

```text
1474560
```

On macOS, use:

```bash
stat -f '%z' build_1990s/inteiliDOS_1990s_floppy.img
```

## 3. CMake target tests

Configure manually:

```bash
cmake -S . \
  -B build_1990s \
  -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TARGET=universal
```

Build everything:

```bash
cmake --build build_1990s --parallel
```

Build individual products:

```bash
cmake --build build_1990s --target inteilidOS
cmake --build build_1990s --target iso
cmake --build build_1990s --target floppy
```

If `genisoimage` or `mkisofs` was detected:

```bash
cmake --build build_1990s --target floppy-iso
```

List available targets:

```bash
cmake --build build_1990s --target help
```

## 4. Boot-sector tests

### Assemble the CHS boot sector

```bash
nasm -f bin \
  -o /tmp/inteiliDOS_mbr_legacy.bin \
  boot/mbr_legacy.asm
```

### Confirm it is exactly 512 bytes

Linux:

```bash
stat -c '%s' /tmp/inteiliDOS_mbr_legacy.bin
```

macOS:

```bash
stat -f '%z' /tmp/inteiliDOS_mbr_legacy.bin
```

Expected:

```text
512
```

### Confirm the boot signature

```bash
od -An -tx1 -j510 -N2 /tmp/inteiliDOS_mbr_legacy.bin
```

Expected:

```text
55 aa
```

### Assemble the extended-read boot sector

```bash
nasm -f bin \
  -o /tmp/inteiliDOS_mbr.bin \
  boot/mbr.asm
```

Check its size and signature:

```bash
stat -c '%s' /tmp/inteiliDOS_mbr.bin
od -An -tx1 -j510 -N2 /tmp/inteiliDOS_mbr.bin
```

### Assemble protected-mode boot objects

```bash
mkdir -p /tmp/inteiliDOS-boot-test
nasm -f elf32 -o /tmp/inteiliDOS-boot-test/boot.o boot/boot.asm
nasm -f elf32 -o /tmp/inteiliDOS-boot-test/gdt_flush.o boot/gdt_flush.asm
nasm -f elf32 -o /tmp/inteiliDOS-boot-test/idt_load.o boot/idt_load.asm
nasm -f elf32 -o /tmp/inteiliDOS-boot-test/isr_stubs.o boot/isr_stubs.asm
file /tmp/inteiliDOS-boot-test/*.o
```

The objects should be reported as 32-bit Intel 80386 ELF relocatable files.

## 5. CPU compatibility tests

### Inspect the ELF architecture

```bash
i686-elf-readelf -h build_1990s/inteilidOS.elf
```

Confirm:

```text
Class:   ELF32
Machine: Intel 80386
```

### Search for post-386 instructions

```bash
i686-elf-objdump -d build_1990s/inteilidOS.elf |
  grep -Ei '\b(cmov|cpuid|rdtsc|rdmsr|wrmsr|sysenter|sysexit|xmm[0-9]|mm[0-7])\b'
```

The command should produce no output.

To turn a match into a failing test:

```bash
if i686-elf-objdump -d build_1990s/inteilidOS.elf |
   grep -Eiq '\b(cmov|cpuid|rdtsc|rdmsr|wrmsr|sysenter|sysexit|xmm[0-9]|mm[0-7])\b'
then
  echo "FAIL: post-i386 instruction found"
  exit 1
else
  echo "PASS: no forbidden instructions found"
fi
```

### Check unresolved symbols

```bash
i686-elf-nm -u build_1990s/inteilidOS.elf
```

No unresolved compiler-runtime functions such as `__udivdi3`,
`__udivmoddi4`, or floating-point helpers should appear.

### Inspect sections and size

```bash
i686-elf-size build_1990s/inteilidOS.elf
i686-elf-readelf -S build_1990s/inteilidOS.elf
i686-elf-nm -n build_1990s/inteilidOS.elf
```

## 6. QEMU ISO tests

### Standard old-CPU test

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

### CMake convenience target

```bash
cmake --build build_1990s --target run-legacy
```

### Direct Multiboot kernel test

```bash
qemu-system-i386 \
  -kernel build_1990s/inteilidOS.elf \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

Or:

```bash
cmake --build build_1990s --target run
```

### Low-memory tests

Minimum target:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 4 \
  -no-reboot
```

Recommended minimum for LaunchPad:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 8 \
  -no-reboot
```

Normal test:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

### Pentium-class test

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu pentium \
  -m 32 \
  -no-reboot
```

### Serial and curses output

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 16 \
  -serial stdio \
  -display curses \
  -no-reboot
```

## 7. QEMU floppy tests

### Boot the raw floppy image

```bash
qemu-system-i386 \
  -drive file=build_1990s/inteiliDOS_1990s_floppy.img,format=raw,if=floppy \
  -boot a \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

Or:

```bash
cmake --build build_1990s --target run-floppy
```

Verify:

- the BIOS starts from drive A
- `[1990s]` appears briefly
- `[K]` appears after loading the kernel
- the VGA screen is cleared by the kernel
- the displayed RAM total is not zero
- IntelliShell accepts PS/2 keyboard input

Test with 4 MB, 8 MB, and 16 MB:

```bash
for ram in 4 8 16; do
  qemu-system-i386 \
    -drive file=build_1990s/inteiliDOS_1990s_floppy.img,format=raw,if=floppy \
    -boot a -cpu 486 -m "$ram" -no-reboot
done
```

This loop opens QEMU once for each RAM size and waits for each instance to
close before starting the next.

## 8. QEMU storage tests

### IDE hard disk

Create a disposable disk:

```bash
qemu-img create -f raw /tmp/inteiliDOS-test-disk.img 64M
```

Boot with the disk attached:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -drive file=/tmp/inteiliDOS-test-disk.img,format=raw,if=ide \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

Inside inteiliDOS, test:

```text
DISKCHECK
CHKDSK
SETUP
```

`SETUP` can overwrite the attached disk. Use only a disposable image.

### ATAPI CD-ROM

```bash
qemu-system-i386 \
  -drive file=build_1990s/inteiliDOS_1990s.iso,media=cdrom,if=ide \
  -boot d \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

Inside inteiliDOS, use LaunchPad to test CD-ROM and ISO 9660 browsing:

```text
PROGRAM
LAUNCHPAD
```

## 9. GDB debugging

Start QEMU paused:

```bash
qemu-system-i386 \
  -kernel build_1990s/inteilidOS.elf \
  -cpu 486 \
  -m 16 \
  -s \
  -S \
  -no-reboot
```

In another terminal:

```bash
i686-elf-gdb build_1990s/inteilidOS.elf
```

Then:

```gdb
target remote :1234
break _start
break kernel_main
continue
info registers
backtrace
x/16wx 0x00100000
x/16hx 0x000b8000
```

Useful additional breakpoints:

```gdb
break memory_init
break gdt_init
break idt_init
break timer_init
break keyboard_init
break ata_init
break shell_run
```

## 10. Physical floppy tests

Writing an image destroys the previous contents of the destination. Verify the
device path before continuing.

Linux:

```bash
sudo dd \
  if=build_1990s/inteiliDOS_1990s_floppy.img \
  of=/dev/fd0 \
  bs=512 \
  conv=fsync
```

Verify the written image:

```bash
sudo cmp \
  -n 1474560 \
  build_1990s/inteiliDOS_1990s_floppy.img \
  /dev/fd0
```

Test:

- cold boot
- warm reboot
- 386, 486, and Pentium systems when available
- memory display
- PS/2 keyboard
- repeated floppy reads
- boot after the machine has been powered off for several minutes

## 11. Physical CD-ROM tests

Burn:

```text
build_1990s/inteiliDOS_1990s.iso
```

Burn it as a disk image at a low speed. CD-R is generally more compatible with
older drives than CD-RW.

Test:

- BIOS detects the optical drive
- CD-ROM can be selected as the first boot device
- GRUB menu appears
- normal boot works
- recovery entry works
- installer entry works with a disposable target disk
- LaunchPad can browse the inserted disc
- eject works where supported

## 12. In-system command tests

At the `C:\>` prompt, run the following checklist.

### Help and shell parsing

```text
HELP
?
ABOUT
HELLO
HISTORY
CLS
CLEAR
```

Also test commands in lowercase and mixed case:

```text
help
MeM
sysinfo
```

### System information

```text
MEM
SYSINFO
TIME
DATE
```

Check that:

- memory is nonzero
- total memory is reasonable for the VM or physical machine
- timer values advance
- CPU information does not depend on CPUID

### Files and directories

```text
DIR
LS
TREE
MKDIR TEST
MD TEST2
CD TEST
CHDIR TEST2
TYPE README.TXT
COPY SOURCE.TXT COPY.TXT
MOVE COPY.TXT MOVED.TXT
DELETE MOVED.TXT
DEL MOVED.TXT
ERASE MOVED.TXT
```

Use file names that exist in the current filesystem. Confirm that errors are
clear when a source file does not exist or a directory name is invalid.

### Storage and media

```text
DISKCHECK
CHKDSK
FORMAT
LABEL
PROGRAM
LAUNCHPAD
SETUP
INSTALL-OS
```

Only use `FORMAT`, `SETUP`, or `INSTALL-OS` with disposable media. These
commands can destroy disk contents.

### File manager

```text
FM
FILEMAN
FILEMANAGER
```

Test:

- arrow-key navigation
- opening directories
- returning to the parent directory
- opening a file
- Escape or the documented exit command

### IEdit

```text
IEDIT
EDIT
IEDIT TEST.TXT
```

Test:

- typing
- cursor movement
- line insertion and deletion
- scrolling
- saving
- opening the saved file
- exiting without damaging the screen

### InteiliBASIC

```text
BASIC
IBASIC
```

Inside BASIC:

```basic
PRINT "HELLO"
LET A=2+3
PRINT A
FOR I=1 TO 5
PRINT I
NEXT I
```

Also test:

```text
CSAVE
CLOAD
QUIT
```

`CLOAD` depends on supported capture hardware. The absence of such hardware
should produce a clear error rather than a hang.

### InteiliSheets

```text
SHEETS
ISHEETS
```

Test:

- cell entry
- cursor movement
- scrolling
- integer values
- `=SUM(...)`
- `=AVG(...)`
- clearing a cell
- quitting

### Speech and sound

```text
TALK Hello world
ITALK Testing one two three
VOLUME
VOLUME 25
VOLUME 100
VOL 50
DAISY
ALIVE
```

Test at several volume levels. Confirm that PIT timing and keyboard input still
work after audio playback.

### Games and demonstrations

```text
DEMO
TETRIS
TOUR
```

Test all documented controls, pause behavior, sound, screen restoration, and
exit behavior.

### Script and package placeholders

```text
SCRIPT
INSTALL
REMOVE
UPDATE
SEARCH
BACKUP
RESTORE
RUN
```

Confirm that implemented operations work and unfinished operations report their
status clearly instead of hanging or corrupting memory.

### Restart and shutdown

Run these last:

```text
RESTART
REBOOT
SHUTDOWN
```

Verify both emulator and physical-machine behavior. Some old BIOSes do not
support software power-off, so a safe halt can be an acceptable shutdown
result.

## 13. Keyboard tests

Test:

- letters A–Z
- numbers 0–9
- Shift
- Caps Lock
- punctuation
- Backspace
- Enter
- Escape
- arrow keys
- repeated keys
- typing while PIT-driven audio is active
- command history with Up and Down

On physical systems, test both a native PS/2 keyboard and an AT keyboard through
a passive adapter when available.

USB keyboard support is optional and is not initialized by the universal
startup path.

## 14. Failure and edge-case tests

Test these cases with disposable images:

- 4 MB RAM
- no IDE hard disk
- no CD-ROM drive
- empty CD-ROM drive
- empty floppy drive
- invalid command
- missing file
- full in-memory filesystem
- unsupported audio controller
- disk read failure
- keyboard input during long operations
- repeated restart
- booting the floppy image as drive A and drive B
- IDE primary master, primary slave, secondary master, and secondary slave

The expected behavior is a clear error or unavailable status. The system should
not hang, reboot unexpectedly, corrupt unrelated memory, or silently claim
success.

## 15. Release test matrix

Complete this table before publishing a compatibility release:

| Environment | CPU | RAM | Boot | Input | Storage | Result |
|---|---:|---:|---|---|---|---|
| QEMU | 486 | 4 MB | ISO | PS/2 | none | |
| QEMU | 486 | 8 MB | floppy | PS/2 | floppy | |
| QEMU | 486 | 16 MB | ISO | PS/2 | IDE + ATAPI | |
| QEMU | Pentium | 32 MB | ISO | PS/2 | IDE + ATAPI | |
| 86Box or PCem | 386DX | 4–8 MB | floppy | PS/2/AT | ISA | |
| 86Box or PCem | 486DX | 8–16 MB | floppy/CD | PS/2 | ISA IDE | |
| Physical PC | 386-class | 4–8 MB | floppy | PS/2/AT | ISA | |
| Physical PC | 486-class | 8–16 MB | floppy/CD | PS/2 | IDE | |
| Physical PC | Pentium-class | 16–32 MB | CD | PS/2 | IDE | |
| Physical PC | late 1990s | 32–128 MB | CD | PS/2 | PCI IDE | |

For every row, test at least:

```text
Boot
HELP
MEM
SYSINFO
TIME
DIR
IEDIT
BASIC
SHEETS
TALK
TETRIS
PROGRAM
DISKCHECK
RESTART
SHUTDOWN
```

## 16. Pass criteria

A release passes when:

- the project builds from a clean directory
- both ISO and floppy images are produced
- both images boot
- the boot sector remains 512 bytes with a `55 AA` signature
- no post-i386 instruction is present
- the kernel starts with 4 MB RAM
- memory totals are valid on GRUB and raw floppy paths
- VGA text output remains readable
- PS/2 keyboard input works
- PIT ticks continue during normal use
- absent optional hardware does not block startup
- file, application, storage, audio, restart, and shutdown tests have recorded
  results
- at least one 386/486-focused emulator and one physical PC have been tested

Emulator testing alone is not enough to claim broad physical compatibility.# Testing inteiliDOS for 1990s Computers

This guide lists the commands, environments, and physical machines that can be
used to test the universal Intel 80386-compatible edition.

Run host-side commands from:

```bash
cd "inteiliDOS for 1990s computers"
```

## 1. Test environments

### Host-side checks

Use a Linux, macOS, or Replit development environment to:

- build the kernel and boot images
- assemble and size-check boot sectors
- compile every C source file for i386
- inspect generated machine instructions
- inspect ELF headers, sections, and symbols

### QEMU

QEMU is the quickest place to test:

- GRUB ISO boot
- raw floppy boot
- direct Multiboot kernel boot
- IDE hard disks
- ATAPI CD-ROM drives
- PS/2 keyboard input
- low-memory configurations
- reboot and shutdown behavior
- GDB debugging

QEMU commonly provides a 486 CPU model rather than a 386 model. Testing with
`-cpu 486` checks old-CPU behavior, while instruction scanning confirms that
the binary remains limited to the i386 ISA.

### 86Box

86Box can test more realistic configurations:

- 386DX and 386SX processors
- 486 processors
- ISA VGA adapters
- period-correct BIOS implementations
- ISA and PCI IDE controllers
- floppy controllers and disk timing
- low RAM amounts

Create separate virtual machines for a 386DX, 486DX, Pentium, and late-1990s
PCI computer.

### PCem

PCem can test:

- 386 and 486 processor behavior
- chipset-specific BIOS behavior
- ISA VGA and Super VGA adapters
- physical-style floppy and IDE timing
- early CD-ROM boot compatibility

### VirtualBox and VMware

VirtualBox and VMware are useful for general ISO boot and application testing.
They are not substitutes for a 386/486 emulator because their virtual CPUs and
hardware are much newer.

Suggested VM configuration:

```text
Operating system type: Other/DOS or Other 32-bit
RAM:                   16–64 MB
Boot device:           Optical drive
Input:                 PS/2 keyboard
Storage:               IDE controller
```

### Physical computers

Test on as many of these categories as possible:

1. 386DX with ISA VGA, PS/2 or AT keyboard compatibility, and a floppy drive
2. 386SX with at least 4 MB RAM
3. 486DX or 486DX2 with ISA IDE
4. Early Pentium with IDE and BIOS CD boot
5. Pentium MMX with PCI IDE
6. Pentium II or Pentium III with IDE compatibility mode
7. One ISA-only system
8. One PCI system
9. One machine that boots from floppy only
10. One machine that boots from CD-ROM

Record the following for each physical test:

```text
Computer or motherboard:
CPU:
RAM:
BIOS vendor and version:
Graphics adapter:
Storage controller:
Boot medium:
Keyboard:
Boot result:
Failed command or subsystem:
Notes:
```

## 2. Build tests

### Clean universal build

```bash
./build.sh --clean
```

### Incremental universal build

```bash
./build.sh
```

The script checks for the required tools and configures
`BUILD_TARGET=universal`.

Expected files:

```text
build_1990s/inteilidOS.elf
build_1990s/inteilidOS.bin
build_1990s/inteiliDOS_1990s.iso
build_1990s/inteiliDOS_1990s_floppy.img
```

Check that they exist:

```bash
test -s build_1990s/inteilidOS.elf
test -s build_1990s/inteilidOS.bin
test -s build_1990s/inteiliDOS_1990s.iso
test -s build_1990s/inteiliDOS_1990s_floppy.img
```

Check the floppy image size:

```bash
stat -c '%s' build_1990s/inteiliDOS_1990s_floppy.img
```

Expected size:

```text
1474560
```

On macOS, use:

```bash
stat -f '%z' build_1990s/inteiliDOS_1990s_floppy.img
```

## 3. CMake target tests

Configure manually:

```bash
cmake -S . \
  -B build_1990s \
  -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TARGET=universal
```

Build everything:

```bash
cmake --build build_1990s --parallel
```

Build individual products:

```bash
cmake --build build_1990s --target inteilidOS
cmake --build build_1990s --target iso
cmake --build build_1990s --target floppy
```

If `genisoimage` or `mkisofs` was detected:

```bash
cmake --build build_1990s --target floppy-iso
```

List available targets:

```bash
cmake --build build_1990s --target help
```

## 4. Boot-sector tests

### Assemble the CHS boot sector

```bash
nasm -f bin \
  -o /tmp/inteiliDOS_mbr_legacy.bin \
  boot/mbr_legacy.asm
```

### Confirm it is exactly 512 bytes

Linux:

```bash
stat -c '%s' /tmp/inteiliDOS_mbr_legacy.bin
```

macOS:

```bash
stat -f '%z' /tmp/inteiliDOS_mbr_legacy.bin
```

Expected:

```text
512
```

### Confirm the boot signature

```bash
od -An -tx1 -j510 -N2 /tmp/inteiliDOS_mbr_legacy.bin
```

Expected:

```text
55 aa
```

### Assemble the extended-read boot sector

```bash
nasm -f bin \
  -o /tmp/inteiliDOS_mbr.bin \
  boot/mbr.asm
```

Check its size and signature:

```bash
stat -c '%s' /tmp/inteiliDOS_mbr.bin
od -An -tx1 -j510 -N2 /tmp/inteiliDOS_mbr.bin
```

### Assemble protected-mode boot objects

```bash
mkdir -p /tmp/inteiliDOS-boot-test
nasm -f elf32 -o /tmp/inteiliDOS-boot-test/boot.o boot/boot.asm
nasm -f elf32 -o /tmp/inteiliDOS-boot-test/gdt_flush.o boot/gdt_flush.asm
nasm -f elf32 -o /tmp/inteiliDOS-boot-test/idt_load.o boot/idt_load.asm
nasm -f elf32 -o /tmp/inteiliDOS-boot-test/isr_stubs.o boot/isr_stubs.asm
file /tmp/inteiliDOS-boot-test/*.o
```

The objects should be reported as 32-bit Intel 80386 ELF relocatable files.

## 5. CPU compatibility tests

### Inspect the ELF architecture

```bash
i686-elf-readelf -h build_1990s/inteilidOS.elf
```

Confirm:

```text
Class:   ELF32
Machine: Intel 80386
```

### Search for post-386 instructions

```bash
i686-elf-objdump -d build_1990s/inteilidOS.elf |
  grep -Ei '\b(cmov|cpuid|rdtsc|rdmsr|wrmsr|sysenter|sysexit|xmm[0-9]|mm[0-7])\b'
```

The command should produce no output.

To turn a match into a failing test:

```bash
if i686-elf-objdump -d build_1990s/inteilidOS.elf |
   grep -Eiq '\b(cmov|cpuid|rdtsc|rdmsr|wrmsr|sysenter|sysexit|xmm[0-9]|mm[0-7])\b'
then
  echo "FAIL: post-i386 instruction found"
  exit 1
else
  echo "PASS: no forbidden instructions found"
fi
```

### Check unresolved symbols

```bash
i686-elf-nm -u build_1990s/inteilidOS.elf
```

No unresolved compiler-runtime functions such as `__udivdi3`,
`__udivmoddi4`, or floating-point helpers should appear.

### Inspect sections and size

```bash
i686-elf-size build_1990s/inteilidOS.elf
i686-elf-readelf -S build_1990s/inteilidOS.elf
i686-elf-nm -n build_1990s/inteilidOS.elf
```

## 6. QEMU ISO tests

### Standard old-CPU test

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

### CMake convenience target

```bash
cmake --build build_1990s --target run-legacy
```

### Direct Multiboot kernel test

```bash
qemu-system-i386 \
  -kernel build_1990s/inteilidOS.elf \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

Or:

```bash
cmake --build build_1990s --target run
```

### Low-memory tests

Minimum target:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 4 \
  -no-reboot
```

Recommended minimum for LaunchPad:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 8 \
  -no-reboot
```

Normal test:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

### Pentium-class test

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu pentium \
  -m 32 \
  -no-reboot
```

### Serial and curses output

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -cpu 486 \
  -m 16 \
  -serial stdio \
  -display curses \
  -no-reboot
```

## 7. QEMU floppy tests

### Boot the raw floppy image

```bash
qemu-system-i386 \
  -drive file=build_1990s/inteiliDOS_1990s_floppy.img,format=raw,if=floppy \
  -boot a \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

Or:

```bash
cmake --build build_1990s --target run-floppy
```

Verify:

- the BIOS starts from drive A
- `[1990s]` appears briefly
- `[K]` appears after loading the kernel
- the VGA screen is cleared by the kernel
- the displayed RAM total is not zero
- IntelliShell accepts PS/2 keyboard input

Test with 4 MB, 8 MB, and 16 MB:

```bash
for ram in 4 8 16; do
  qemu-system-i386 \
    -drive file=build_1990s/inteiliDOS_1990s_floppy.img,format=raw,if=floppy \
    -boot a -cpu 486 -m "$ram" -no-reboot
done
```

This loop opens QEMU once for each RAM size and waits for each instance to
close before starting the next.

## 8. QEMU storage tests

### IDE hard disk

Create a disposable disk:

```bash
qemu-img create -f raw /tmp/inteiliDOS-test-disk.img 64M
```

Boot with the disk attached:

```bash
qemu-system-i386 \
  -cdrom build_1990s/inteiliDOS_1990s.iso \
  -drive file=/tmp/inteiliDOS-test-disk.img,format=raw,if=ide \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

Inside inteiliDOS, test:

```text
DISKCHECK
CHKDSK
SETUP
```

`SETUP` can overwrite the attached disk. Use only a disposable image.

### ATAPI CD-ROM

```bash
qemu-system-i386 \
  -drive file=build_1990s/inteiliDOS_1990s.iso,media=cdrom,if=ide \
  -boot d \
  -cpu 486 \
  -m 16 \
  -no-reboot
```

Inside inteiliDOS, use LaunchPad to test CD-ROM and ISO 9660 browsing:

```text
PROGRAM
LAUNCHPAD
```

## 9. GDB debugging

Start QEMU paused:

```bash
qemu-system-i386 \
  -kernel build_1990s/inteilidOS.elf \
  -cpu 486 \
  -m 16 \
  -s \
  -S \
  -no-reboot
```

In another terminal:

```bash
i686-elf-gdb build_1990s/inteilidOS.elf
```

Then:

```gdb
target remote :1234
break _start
break kernel_main
continue
info registers
backtrace
x/16wx 0x00100000
x/16hx 0x000b8000
```

Useful additional breakpoints:

```gdb
break memory_init
break gdt_init
break idt_init
break timer_init
break keyboard_init
break ata_init
break shell_run
```

## 10. Physical floppy tests

Writing an image destroys the previous contents of the destination. Verify the
device path before continuing.

Linux:

```bash
sudo dd \
  if=build_1990s/inteiliDOS_1990s_floppy.img \
  of=/dev/fd0 \
  bs=512 \
  conv=fsync
```

Verify the written image:

```bash
sudo cmp \
  -n 1474560 \
  build_1990s/inteiliDOS_1990s_floppy.img \
  /dev/fd0
```

Test:

- cold boot
- warm reboot
- 386, 486, and Pentium systems when available
- memory display
- PS/2 keyboard
- repeated floppy reads
- boot after the machine has been powered off for several minutes

## 11. Physical CD-ROM tests

Burn:

```text
build_1990s/inteiliDOS_1990s.iso
```

Burn it as a disk image at a low speed. CD-R is generally more compatible with
older drives than CD-RW.

Test:

- BIOS detects the optical drive
- CD-ROM can be selected as the first boot device
- GRUB menu appears
- normal boot works
- recovery entry works
- installer entry works with a disposable target disk
- LaunchPad can browse the inserted disc
- eject works where supported

## 12. In-system command tests

At the `C:\>` prompt, run the following checklist.

### Help and shell parsing

```text
HELP
?
ABOUT
HELLO
HISTORY
CLS
CLEAR
```

Also test commands in lowercase and mixed case:

```text
help
MeM
sysinfo
```

### System information

```text
MEM
SYSINFO
TIME
DATE
```

Check that:

- memory is nonzero
- total memory is reasonable for the VM or physical machine
- timer values advance
- CPU information does not depend on CPUID

### Files and directories

```text
DIR
LS
TREE
MKDIR TEST
MD TEST2
CD TEST
CHDIR TEST2
TYPE README.TXT
COPY SOURCE.TXT COPY.TXT
MOVE COPY.TXT MOVED.TXT
DELETE MOVED.TXT
DEL MOVED.TXT
ERASE MOVED.TXT
```

Use file names that exist in the current filesystem. Confirm that errors are
clear when a source file does not exist or a directory name is invalid.

### Storage and media

```text
DISKCHECK
CHKDSK
FORMAT
LABEL
PROGRAM
LAUNCHPAD
SETUP
INSTALL-OS
```

Only use `FORMAT`, `SETUP`, or `INSTALL-OS` with disposable media. These
commands can destroy disk contents.

### File manager

```text
FM
FILEMAN
FILEMANAGER
```

Test:

- arrow-key navigation
- opening directories
- returning to the parent directory
- opening a file
- Escape or the documented exit command

### IEdit

```text
IEDIT
EDIT
IEDIT TEST.TXT
```

Test:

- typing
- cursor movement
- line insertion and deletion
- scrolling
- saving
- opening the saved file
- exiting without damaging the screen

### InteiliBASIC

```text
BASIC
IBASIC
```

Inside BASIC:

```basic
PRINT "HELLO"
LET A=2+3
PRINT A
FOR I=1 TO 5
PRINT I
NEXT I
```

Also test:

```text
CSAVE
CLOAD
QUIT
```

`CLOAD` depends on supported capture hardware. The absence of such hardware
should produce a clear error rather than a hang.

### InteiliSheets

```text
SHEETS
ISHEETS
```

Test:

- cell entry
- cursor movement
- scrolling
- integer values
- `=SUM(...)`
- `=AVG(...)`
- clearing a cell
- quitting

### Speech and sound

```text
TALK Hello world
ITALK Testing one two three
VOLUME
VOLUME 25
VOLUME 100
VOL 50
DAISY
ALIVE
```

Test at several volume levels. Confirm that PIT timing and keyboard input still
work after audio playback.

### Games and demonstrations

```text
DEMO
TETRIS
TOUR
```

Test all documented controls, pause behavior, sound, screen restoration, and
exit behavior.

### Script and package placeholders

```text
SCRIPT
INSTALL
REMOVE
UPDATE
SEARCH
BACKUP
RESTORE
RUN
```

Confirm that implemented operations work and unfinished operations report their
status clearly instead of hanging or corrupting memory.

### Restart and shutdown

Run these last:

```text
RESTART
REBOOT
SHUTDOWN
```

Verify both emulator and physical-machine behavior. Some old BIOSes do not
support software power-off, so a safe halt can be an acceptable shutdown
result.

## 13. Keyboard tests

Test:

- letters A–Z
- numbers 0–9
- Shift
- Caps Lock
- punctuation
- Backspace
- Enter
- Escape
- arrow keys
- repeated keys
- typing while PIT-driven audio is active
- command history with Up and Down

On physical systems, test both a native PS/2 keyboard and an AT keyboard through
a passive adapter when available.

USB keyboard support is optional and is not initialized by the universal
startup path.

## 14. Failure and edge-case tests

Test these cases with disposable images:

- 4 MB RAM
- no IDE hard disk
- no CD-ROM drive
- empty CD-ROM drive
- empty floppy drive
- invalid command
- missing file
- full in-memory filesystem
- unsupported audio controller
- disk read failure
- keyboard input during long operations
- repeated restart
- booting the floppy image as drive A and drive B
- IDE primary master, primary slave, secondary master, and secondary slave

The expected behavior is a clear error or unavailable status. The system should
not hang, reboot unexpectedly, corrupt unrelated memory, or silently claim
success.

## 15. Release test matrix

Complete this table before publishing a compatibility release:

| Environment | CPU | RAM | Boot | Input | Storage | Result |
|---|---:|---:|---|---|---|---|
| QEMU | 486 | 4 MB | ISO | PS/2 | none | |
| QEMU | 486 | 8 MB | floppy | PS/2 | floppy | |
| QEMU | 486 | 16 MB | ISO | PS/2 | IDE + ATAPI | |
| QEMU | Pentium | 32 MB | ISO | PS/2 | IDE + ATAPI | |
| 86Box or PCem | 386DX | 4–8 MB | floppy | PS/2/AT | ISA | |
| 86Box or PCem | 486DX | 8–16 MB | floppy/CD | PS/2 | ISA IDE | |
| Physical PC | 386-class | 4–8 MB | floppy | PS/2/AT | ISA | |
| Physical PC | 486-class | 8–16 MB | floppy/CD | PS/2 | IDE | |
| Physical PC | Pentium-class | 16–32 MB | CD | PS/2 | IDE | |
| Physical PC | late 1990s | 32–128 MB | CD | PS/2 | PCI IDE | |

For every row, test at least:

```text
Boot
HELP
MEM
SYSINFO
TIME
DIR
IEDIT
BASIC
SHEETS
TALK
TETRIS
PROGRAM
DISKCHECK
RESTART
SHUTDOWN
```

## 16. Pass criteria

A release passes when:

- the project builds from a clean directory
- both ISO and floppy images are produced
- both images boot
- the boot sector remains 512 bytes with a `55 AA` signature
- no post-i386 instruction is present
- the kernel starts with 4 MB RAM
- memory totals are valid on GRUB and raw floppy paths
- VGA text output remains readable
- PS/2 keyboard input works
- PIT ticks continue during normal use
- absent optional hardware does not block startup
- file, application, storage, audio, restart, and shutdown tests have recorded
  results
- at least one 386/486-focused emulator and one physical PC have been tested

Emulator testing alone is not enough to claim broad physical compatibility.