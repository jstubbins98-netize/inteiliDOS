#!/usr/bin/env python3
"""Real 486 v86 smoke tests from ISO9660 CD and FAT12 floppy media."""
from pathlib import Path
import json
import shutil
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def fat12_image(files):
    """Create a 1.44 MB FAT12 image without requiring a host mount or mtools."""
    disk = bytearray(1440 * 1024)
    disk[:3] = b"\xeb\x3c\x90"
    disk[3:11] = b"VDOSTEST"
    struct.pack_into("<HBHBHHBHHH", disk, 11, 512, 1, 1, 2, 224, 2880, 0xF0, 9, 18, 2)
    disk[510:512] = b"\x55\xaa"
    fat = bytearray(9 * 512)
    fat[:3] = b"\xf0\xff\xff"
    cluster = 2
    for entry, (name, content) in enumerate(files.items()):
        stem, ext = name.split(".")
        at = 19 * 512 + entry * 32
        disk[at:at+11] = (stem.ljust(8)+ext.ljust(3)).encode()
        disk[at+11] = 0x20
        struct.pack_into("<HI", disk, at+26, cluster, len(content))
        chunks = (len(content)+511)//512
        for c in range(cluster, cluster+chunks):
            value = c+1 if c+1 < cluster+chunks else 0xFFF
            offset = c+c//2
            old = fat[offset] | (fat[offset+1]<<8)
            new = (old&0xF) | (value<<4) if c&1 else (old&0xF000)|value
            fat[offset:offset+2] = struct.pack("<H", new)
            data_at = (33+c-2)*512
            part = content[(c-cluster)*512:(c-cluster+1)*512]
            disk[data_at:data_at+len(part)] = part
        cluster += chunks
    disk[512:10*512] = fat
    disk[10*512:19*512] = fat
    return disk


def abort_guest(proc, qmp, log):
    deadline = time.monotonic()+30
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise AssertionError("Guest exited before F8 test")
        if log.exists() and ": RUN\n" in log.read_text():
            break
        time.sleep(0.05)
    else:
        raise AssertionError("Guest never entered DOS")
    time.sleep(0.3)
    with socket.socket(socket.AF_UNIX) as s:
        s.connect(str(qmp))
        io = s.makefile("rwb", buffering=0)
        json.loads(io.readline())
        def command(name, arguments=None):
            io.write((json.dumps({"execute": name, "arguments": arguments or {}})+"\n").encode())
            while True:
                response = json.loads(io.readline())
                if "return" in response: return response
                if "error" in response: raise AssertionError(response)
        command("qmp_capabilities")
        command("send-key", {"keys": [{"type": "qcode", "data": "f8"}], "hold-time": 100})


def main():
    tools = {t: shutil.which(t) for t in ("gcc", "ld", "nasm", "qemu-system-i386", "xorriso")}
    if not all(tools.values()):
        raise SystemExit("Need ELF GCC/binutils, NASM, QEMU i386, and xorriso.")
    with tempfile.TemporaryDirectory(prefix="inteilidos-dos-") as folder:
        tmp = Path(folder)
        disc = tmp/"disc"
        disc.mkdir()
        cases = ("TEST.COM", "TEST.EXE", "PORT.COM", "FAULT.COM", "EXEC.COM",
                 "VIDEO.COM", "RET.COM", "KEY.COM", "LOOP.COM", "KERNEL.COM", "BDA.COM",
                 "CRT.COM", "TIMING.COM", "VIRT.COM", "FAST.COM",
                 "INTO.COM", "INTOBAD.COM")
        files = {"DATA.TXT": b"WXYZ"}
        for case, name in enumerate(cases):
            run(tools["nasm"], "-f", "bin", "-DCASE="+str(case),
                TESTS/"dos_fixture.asm", "-o", disc/name)
            files[name] = (disc/name).read_bytes()
        (disc/"DATA.TXT").write_bytes(files["DATA.TXT"])
        run(tools["xorriso"], "-as", "mkisofs", "-quiet", "-iso-level", "1",
            "-o", tmp/"test.iso", disc)
        (tmp/"test.img").write_bytes(fat12_image(files))
        flags = ["-m32", "-march=i386", "-O1", "-msoft-float", "-mno-sse", "-mno-sse2",
                 "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
                 "-fno-stack-protector", "-fno-asynchronous-unwind-tables",
                 "-Wall", "-Wextra", "-Werror", "-DCONFIG_ENABLE_FLOPPY=1"]
        objects = []
        sources = ["kernel/"+n+".c" for n in
                   ("dos", "dos_services", "dos_hardware", "gdt", "idt", "isr", "timer", "keyboard",
                    "vga", "cdrom", "iso9660", "fdc", "fat12", "loader")]
        sources += ["shell/launchpad_dos.c"]
        for src in sources:
            obj = tmp/(Path(src).stem+".o")
            run(tools["gcc"], *flags, "-c", ROOT/src, "-o", obj)
            objects.append(obj)
        for src in ("boot/gdt_flush.asm", "boot/idt_load.asm", "boot/isr_stubs.asm",
                    "boot/v86.asm", "tests/cdrom_boot.asm"):
            obj = tmp/(Path(src).stem+".o")
            run(tools["nasm"], "-f", "elf32", ROOT/src, "-o", obj)
            objects.append(obj)
        # Also compile the actual LaunchPad UI with and without floppy support.
        for enabled in (0, 1):
            run(tools["gcc"], *flags[:-1], "-DCONFIG_ENABLE_FLOPPY="+str(enabled),
                "-c", ROOT/"shell/launchpad.c", "-o", tmp/"launchpad.o")
        for floppy in (0, 1):
            for interactive in (0, 1, 2):
                run(tools["gcc"], *flags, "-DDOS_FLOPPY="+str(floppy),
                    "-DDOS_INTERACTIVE="+str(interactive),
                    "-c", TESTS/"dos_smoke.c", "-o", tmp/"smoke.o")
                run(tools["ld"], "-m", "elf_i386", "-T", TESTS/"cdrom_host.ld",
                    "--wrap=dos_exception", "-o", tmp/"host.elf", tmp/"smoke.o", *objects)
                log, qmp = tmp/"debug.log", tmp/"qmp.sock"
                log.unlink(missing_ok=True)
                qmp.unlink(missing_ok=True)
                command = [tools["qemu-system-i386"], "-nodefaults", "-device", "VGA", "-machine", "pc",
                           "-cpu", "486", "-m", "16", "-kernel", str(tmp/"host.elf"),
                           "-display", "none", "-no-reboot",
                           "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
                           "-debugcon", "file:"+str(log),
                           "-qmp", "unix:"+str(qmp)+",server=on,wait=off"]
                if floppy:
                    command += ["-drive", "file="+str(tmp/"test.img")+",if=floppy,format=raw"]
                else:
                    command += ["-drive", "file="+str(tmp/"test.iso")+",if=ide,index=0,media=cdrom"]
                with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as proc:
                    try:
                        if interactive: abort_guest(proc, qmp, log)
                        stdout, stderr = proc.communicate(timeout=60)
                    except BaseException:
                        proc.kill()
                        proc.communicate()
                        print(log.read_text() if log.exists() else "No guest output")
                        raise
                    output = log.read_text() if log.exists() else ""
                    if proc.returncode != 33 or not output.endswith("PASS\n"):
                        raise AssertionError(f"floppy={floppy}, interactive={interactive}:\n"
                                             f"{output}\n{stderr.decode()}")
                    print(f"PASS: {'FAT12 floppy' if floppy else 'ISO9660 CD'}, "
                          f"{'COM/MZ, files, faults, restoration, native loader' if not interactive else 'F8 abort '+('input' if interactive==1 else 'CLI loop')}",
                          flush=True)


if __name__ == "__main__":
    main()
