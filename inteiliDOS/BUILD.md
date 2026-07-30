# inteiliDOS — Build Guide

## Overview

inteiliDOS is a bare-metal x86 OS.  It requires a **cross-compiler toolchain**
that targets `i686-elf` (bare-metal, no host OS ABI).  You cannot compile it
with the standard GCC/Clang that ships with your Linux or macOS installation.

---

## 1. Install prerequisites

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    nasm \
    xorriso \
    grub-pc-bin \
    grub-common \
    qemu-system-x86 \
    mtools
```

### macOS (Homebrew)

```bash
brew install cmake nasm xorriso qemu
brew install x86_64-elf-binutils   # for objcopy only
```

---

## 2. Build the i686-elf cross-compiler

The cross-compiler is not available via apt.  Build it from source using the
OSDev cross-compiler guide or the script below.

### Quick script (Ubuntu)

```bash
#!/usr/bin/env bash
set -e

export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

BINUTILS_VER=2.41
GCC_VER=13.2.0

mkdir -p "$HOME/src" && cd "$HOME/src"

# --- binutils ---
wget -qO binutils.tar.xz \
    "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.xz"
tar -xf binutils.tar.xz
mkdir build-binutils && cd build-binutils
../binutils-${BINUTILS_VER}/configure \
    --target=$TARGET --prefix="$PREFIX" \
    --with-sysroot --disable-nls --disable-werror
make -j$(nproc) && make install
cd ..

# --- GCC (C only) ---
wget -qO gcc.tar.xz \
    "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz"
tar -xf gcc.tar.xz
cd gcc-${GCC_VER} && contrib/download_prerequisites && cd ..
mkdir build-gcc && cd build-gcc
../gcc-${GCC_VER}/configure \
    --target=$TARGET --prefix="$PREFIX" \
    --disable-nls --enable-languages=c,c++ --without-headers
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

After building, add the toolchain to your PATH:

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
```

---

## 3. Build inteilidOS

```bash
cd inteilidOS
chmod +x build.sh
./build.sh
```

The script:
1. Checks all dependencies
2. Runs `cmake` with the `cmake/i686-elf.cmake` toolchain file
3. Builds the kernel ELF and flat binary
4. Creates a bootable GRUB2 ISO (if grub-mkrescue is available)

---

## 4. Run in QEMU

```bash
qemu-system-i386 -cdrom build/inteilidOS.iso -m 128 -serial stdio -no-reboot
```

Or use the CMake target:

```bash
cmake --build build --target run
```

### Debug with GDB

```bash
# Terminal 1
cmake --build build --target run-debug   # QEMU pauses waiting for GDB

# Terminal 2
i686-elf-gdb build/inteilidOS.elf
(gdb) target remote :1234
(gdb) continue
```

---

## 5. Burn to real hardware

```bash
sudo dd if=build/inteilidOS.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Replace `/dev/sdX` with your USB drive (check with `lsblk`).

---

## Project structure

```
inteilidOS/
├── boot/                 Assembly stubs (entry, GDT/IDT flush, ISR stubs)
│   ├── boot.asm          Multiboot2 header + _start
│   ├── gdt_flush.asm     lgdt + far jump to reload CS
│   ├── idt_load.asm      lidt helper
│   └── isr_stubs.asm     ISR/IRQ trampolines (32 exceptions + 16 IRQs)
├── kernel/               Core kernel
│   ├── kernel.c          kernel_main() — boot sequence + launch shell
│   ├── vga.c/h           VGA text-mode driver (80×25, colour)
│   ├── gdt.c/h           Global Descriptor Table (flat 32-bit + TSS)
│   ├── idt.c/h           Interrupt Descriptor Table
│   ├── isr.c/h           ISR/IRQ dispatch, PIC remapping
│   ├── timer.c/h         PIT 8254 driver (1 kHz tick)
│   ├── keyboard.c/h      PS/2 keyboard driver (US QWERTY, scancode set 1)
│   ├── memory.c/h        Bitmap physical allocator + heap (kmalloc/kfree)
│   └── multiboot.h       Multiboot2 structure definitions
├── shell/                IntelliShell
│   ├── shell.c/h         REPL: readline, history, NLP dispatch
│   └── commands.c/h      All built-in commands + NLP translator
├── grub/
│   └── grub.cfg          GRUB2 menu (normal + recovery entries)
├── cmake/
│   └── i686-elf.cmake    CMake toolchain file for cross-compilation
├── linker.ld             Kernel linker script (load at 1 MB)
├── CMakeLists.txt        CMake build system
├── build.sh              One-shot build script
└── BUILD.md              This file
```

---

## Feature status

| Feature                        | Status                                  |
|--------------------------------|-----------------------------------------|
| Boot sequence (Multiboot2)     | ✅ Implemented                          |
| VGA text output (80×25)        | ✅ Implemented                          |
| GDT / IDT                      | ✅ Implemented                          |
| ISR / IRQ / PIC remapping      | ✅ Implemented                          |
| PIT timer (1 kHz)              | ✅ Implemented                          |
| PS/2 keyboard driver           | ✅ Implemented                          |
| Physical memory manager        | ✅ Implemented (bitmap + Multiboot map) |
| Heap (kmalloc/kfree)           | ✅ Implemented (2 MB embedded heap)     |
| IntelliShell                   | ✅ Implemented                          |
| NLP translator                 | ✅ Implemented                          |
| Command history (↑/↓)          | ✅ Implemented                          |
| Built-in commands (25+)        | ✅ Implemented                          |
| IFS file system driver         | 🔧 Stub (requires ATA driver)           |
| ATA/IDE disk driver            | 🔧 Not yet implemented                  |
| Multitasking scheduler         | 🔧 Not yet implemented                  |
| Network (TCP/IP)               | 🔧 Stub (requires NIC driver)           |
| InteiliBASIC interpreter       | 🔧 Stub entry point                     |
| iEdit text editor              | 🔧 Stub entry point                     |
| User accounts / permissions    | 🔧 Not yet implemented                  |
