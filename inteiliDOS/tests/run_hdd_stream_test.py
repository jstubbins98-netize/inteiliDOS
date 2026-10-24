#!/usr/bin/env python3
"""Boot streaming VBR boundary-size fixtures on disposable BIOS HDDs."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from build_test_kernel import ROOT


def main():
    with tempfile.TemporaryDirectory(prefix="hdd-stream-") as folder:
        tmp = Path(folder)
        for name in ("hdd_mbr", "hdd_vbr"):
            subprocess.run(["nasm", "-f", "bin", str(ROOT/"boot"/(name+".asm")),
                            "-o", str(tmp/(name+".bin"))], check=True)
        mbr = bytearray((tmp/"hdd_mbr.bin").read_bytes())
        struct.pack_into("<B3sB3sII", mbr, 446, 0x80, b"\xfe\xff\xff",
                         0x99, b"\xfe\xff\xff", 2048, 16384-2048)
        # >512 KiB, >1 MiB (needs a 16-bit counter), and the 4 MiB limit.
        # Check the entire loaded tail, not merely that the entry point runs.
        for size in (600*1024, 1200*1024, 4*1024*1024):
            asm = tmp/"fixture.asm"
            asm.write_text(f"""
bits 32
org 0x100000
    cli
    cld
    mov esi,tail
    mov ecx,0x100000+{size}-tail
check:
    lodsb
    cmp al,0x5a
    jne fail
    loop check
    mov al,0x10
    jmp report
fail:
    mov al,0x11
report:
    out 0xf4,al
    hlt
    jmp $
tail:
times {size}-($-$$) db 0x5a
""")
            subprocess.run(["nasm", "-f", "bin", str(asm), "-o",
                            str(tmp/"fixture.bin")], check=True)
            vbr = bytearray((tmp/"hdd_vbr.bin").read_bytes())
            struct.pack_into("<I", vbr, 0x21, 2049)
            struct.pack_into("<H", vbr, 0x36, size//4096)
            struct.pack_into("<I", vbr, 0xDA, size//4)
            disk = tmp/"target.img"
            with disk.open("wb") as f:
                f.truncate(8*1024*1024)
                f.write(mbr)
                f.seek(2048*512); f.write(vbr)
                f.write((tmp/"fixture.bin").read_bytes())
            result = subprocess.run([
                shutil.which("qemu-system-i386") or "qemu-system-i386",
                "-machine", "pc", "-cpu", "486", "-m", "16",
                "-display", "none", "-serial", "none", "-monitor", "none",
                "-no-reboot", "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
                "-drive", f"file={disk},if=ide,format=raw",
                "-boot", "order=c,strict=on"], timeout=45,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert result.returncode == 33, (
                f"{size} bytes: exit {result.returncode}: {result.stderr.decode()}")
            print(f"PASS: i486 BIOS streaming boot, all {size} bytes verified",
                  flush=True)


if __name__ == "__main__":
    main()
