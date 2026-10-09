#!/usr/bin/env python3
"""Run host-side ATAPI regression tests and real-driver QEMU IDE smoke tests."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    # The host model is a normal native executable, not a cross-compiled ELF.
    host_cc = shutil.which("gcc") or shutil.which("cc")
    if not host_cc:
        raise SystemExit("Need a native C compiler for the task-file tests.")
    with tempfile.TemporaryDirectory(prefix="inteilidos-cdrom-") as tmp:
        tmp = Path(tmp)
        run(host_cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-DCDROM_TEST_IO", "-I" + str(TESTS), TESTS / "cdrom_test.c",
            ROOT / "kernel/cdrom.c", "-o", tmp / "model")
        run(tmp / "model")
        dead_strip = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        run(host_cc, "-O1", "-ffunction-sections", "-fdata-sections",
            "-DCONFIG_ENABLE_PCI_IDE=1", "-DCONFIG_ENABLE_ATA=0",
            "-DCONFIG_ENABLE_AHCI=0", TESTS / "ide_ports_test.c",
            ROOT / "kernel/ata.c", dead_strip, "-o", tmp / "ports")
        run(tmp / "ports")

        gcc = shutil.which("i686-elf-gcc") or shutil.which("gcc")
        ld = shutil.which("i686-elf-ld") or shutil.which("ld")
        nasm = shutil.which("nasm")
        qemu = shutil.which("qemu-system-i386")
        xorriso = shutil.which("xorriso")
        if not all((gcc, ld, nasm, qemu, xorriso)):
            raise SystemExit("Model passed; full smoke tests need ELF GCC/binutils, "
                             "NASM, qemu-system-i386, and xorriso.")
        flags = ["-m32", "-march=i386", "-O1", "-msoft-float", "-mno-sse",
                 "-mno-sse2", "-ffreestanding", "-fno-builtin", "-fno-pic",
                 "-fno-pie", "-fno-stack-protector", "-fno-asynchronous-unwind-tables",
                 "-DCONFIG_ENABLE_ATA=1", "-DCONFIG_ENABLE_PCI_IDE=1",
                 "-DCONFIG_ENABLE_AHCI=0"]
        objects = []
        for name in ("ata", "pci", "cdrom"):
            obj = tmp / (name + ".o")
            run(gcc, *flags, "-c", ROOT / ("kernel/" + name + ".c"), "-o", obj)
            objects.append(obj)
        run(nasm, "-f", "elf32", TESTS / "cdrom_boot.asm", "-o", tmp / "boot.o")
        files = tmp / "disc"
        files.mkdir()
        (files / "README.TXT").write_text("inteiliDOS ATAPI regression disc\n")
        run(xorriso, "-as", "mkisofs", "-quiet", "-iso-level", "1",
            "-o", tmp / "test.iso", files)
        with (tmp / "hdd.img").open("wb") as f:
            f.truncate(2 * 1024 * 1024)

        for empty in (0, 1):
            for ata_first in (0, 1):
                for slot in range(4):
                    run(gcc, *flags, "-DCD_SLOT=" + str(slot),
                        "-DATA_FIRST=" + str(ata_first), "-DEMPTY_TRAY=" + str(empty),
                        "-c", TESTS / "cdrom_smoke.c", "-o", tmp / "smoke.o")
                    run(ld, "-m", "elf_i386", "-T", TESTS / "cdrom_host.ld",
                        "-o", tmp / "host.elf", tmp / "boot.o", tmp / "smoke.o", *objects)
                    log = tmp / "debug.log"
                    log.unlink(missing_ok=True)
                    cd_spec = ("file=" + str(tmp / "test.iso") + ",") if not empty else ""
                    command = [qemu, "-nodefaults", "-machine", "pc", "-cpu", "486", "-m", "16",
                               "-kernel", str(tmp / "host.elf"), "-display", "none",
                               "-no-reboot", "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
                               "-debugcon", "file:" + str(log), "-drive",
                               cd_spec + "if=ide,index=" + str(slot) + ",media=cdrom"]
                    if ata_first:
                        hdd_slot = 2 if slot == 0 else 0
                        command += ["-drive", "file=" + str(tmp / "hdd.img") +
                                    ",format=raw,if=ide,index=" + str(hdd_slot)]
                    result = subprocess.run(command, timeout=30, capture_output=True)
                    output = log.read_text() if log.exists() else ""
                    if result.returncode != 33 or output != "PASS\n":
                        raise AssertionError(f"slot={slot}, ATA-first={ata_first}, "
                                             f"empty={empty}: {output}\n"
                                             + result.stderr.decode())
                    print(f"PASS: QEMU 486 IDE slot {slot}, HDD probe={bool(ata_first)}, "
                          f"empty tray={bool(empty)}", flush=True)
        print("All ATAPI regression and real-driver IDE smoke tests passed.")


if __name__ == "__main__":
    main()
