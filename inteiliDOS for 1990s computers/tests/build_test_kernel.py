#!/usr/bin/env python3
"""Compile/link the complete OS with a temporary legacy test profile.

Does not run the setup wizard, overwrite configurations/config.h, or require
GRUB ISO tools. For emulator regression use only, not a hardware release.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def build(folder):
    tmp = Path(folder).resolve()
    tmp.mkdir(parents=True, exist_ok=True)
    cc = shutil.which("i686-elf-gcc") or shutil.which("gcc")
    ld = shutil.which("i686-elf-ld") or shutil.which("ld")
    config = tmp/"config.h"
    config.write_text("""
#define CONFIG_PROFILE_NAME "Temporary emulator test"
#define CONFIG_CPU_CLASS 4
#define CONFIG_RAM_MB 16
#define CONFIG_BOOT_MEDIA 3
#define CONFIG_ENABLE_PS2_KEYBOARD 1
#define CONFIG_ENABLE_USB_KEYBOARD 0
#define CONFIG_ENABLE_PCI 0
#define CONFIG_ENABLE_PCI_IDE 0
#define CONFIG_ENABLE_AHCI 0
#define CONFIG_ENABLE_ATA 1
#define CONFIG_ENABLE_ATAPI_CDROM 1
#define CONFIG_ENABLE_FLOPPY 1
#define CONFIG_AUDIO_MODE 0
#define CONFIG_TIMER_HZ 1000
#define CONFIG_AUDIO_NONE 0
#define CONFIG_AUDIO_AC97 1
#define CONFIG_AUDIO_HDA 2
#define CONFIG_AUDIO_AUTO 3
#define CONFIG_CPU_386 3
#define CONFIG_CPU_486 4
#define CONFIG_CPU_PENTIUM 5
#define CONFIG_CPU_P6 6
#define CONFIG_BOOT_FLOPPY 1
#define CONFIG_BOOT_CDROM 2
#define CONFIG_BOOT_BOTH 3
""")
    run("nasm", "-f", "bin", ROOT/"boot/mbr.asm", "-o", tmp/"mbr.bin")
    run(sys.executable, ROOT/"cmake/bin2header.py", tmp/"mbr.bin", "mbr_data", tmp/"mbr_data.h")
    cmake = (ROOT/"CMakeLists.txt").read_text()
    objects = []
    # Match CMake's link order: boot.asm embeds Multiboot in .text, so its
    # object must precede C code to keep the header in the first 8 KB.
    for name in ("BOOT_ASM_SOURCES", "KERNEL_C_SOURCES", "SHELL_C_SOURCES"):
        sources = re.search(r"set\("+name+r"\s+(.*?)\)", cmake, re.S).group(1).split()
        for number, source in enumerate(sources):
            obj = tmp/(name+str(number)+".o")
            if source.endswith(".asm"):
                run("nasm", "-f", "elf32", ROOT/source, "-o", obj)
            else:
                run(cc, "-m32", "-march=i386", "-mtune=i386", "-O1", "-msoft-float",
                    "-mno-mmx", "-mno-sse", "-mno-sse2", "-nostdlib", "-ffreestanding",
                    "-fno-builtin", "-fno-stack-protector", "-fno-pic", "-fno-pie",
                    "-std=gnu11", "-Wall", "-Wextra", "-include", config,
                    "-I"+str(tmp), "-I"+str(ROOT), "-I"+str(ROOT/"kernel"),
                    "-I"+str(ROOT/"shell"), "-c", ROOT/source, "-o", obj)
            objects.append(obj)
    output = tmp/"inteilidOS.elf"
    run(ld, "-m", "elf_i386", "-nostdlib", "-T", ROOT/"linker.ld", "-o", output, *objects)
    symbols = subprocess.check_output(["nm", "-n", str(output)], text=True)
    end = int(re.search(r"^([0-9a-fA-F]+) \w _kernel_end$", symbols, re.M).group(1), 16)
    if end > 0x500000:
        raise AssertionError(f"Kernel end {end:#x} overlaps LaunchPad staging")
    print(f"PASS: complete kernel linked; end={end:#x}, staging=0x500000")
    return output


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python3 tests/build_test_kernel.py /tmp/inteilidos-build")
    build(sys.argv[1])
