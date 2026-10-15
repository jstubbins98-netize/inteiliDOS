#!/usr/bin/env python3
"""Visible full-kernel DOS storage refusals; disposable native HDDs only.
The host msdos_space_test.c additionally injects real ATA backend failures,
checks cancellation, and exercises successful seeding/publication.
"""
from pathlib import Path
import struct
import sys
import tempfile
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell


def fixture(mib, files):
    total = mib * 2048
    start, directory, data = 2048, 2048 + 1033, 2048 + 1034
    disk = bytearray(mib * 1024 * 1024)
    disk[510:512] = b"\x55\xaa"
    disk[446], disk[450] = 0x80, 0x99
    struct.pack_into("<II", disk, 454, start, total - start)
    fields = (0x314c4449, 1, start, total - start, 1024, 8, 1033)
    checksum = 0x9e3779b9
    for value in fields:
        checksum ^= value
    disk[start*512:(start+1025)*512] = b"\xa5" * (1025*512)
    struct.pack_into("<8I", disk, (start+1025)*512, *fields, checksum)
    struct.pack_into("<I", disk, directory*512, 0x46530001)
    for i in range(files):
        struct.pack_into("<24sII", disk, directory*512+4+i*32,
                         f"KEEP{i}.TXT".encode(), data+i, 4)
        disk[(data+i)*512:(data+i)*512+4] = b"KEEP"
    return disk, (total-data-files)//2


def main():
    kernel = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="msdos-space-qemu-") as folder:
        tmp = Path(folder)
        for mib, files, message in (
            (10, 1, "insufficient disk space"),
            (32, 15, "native file directory is full"),
            (32, 1, "DOS boot disk missing"),
        ):
            before, available = fixture(mib, files)
            path = tmp/"native.img"
            path.write_bytes(before)
            guest = Guest(kernel, None, False, tmp, (
                "-drive", f"file={path},if=ide,index=1,format=raw",
            ))
            try:
                guest.wait("Welcome to inteiliDOS")
                guest.wait("> ")
                shell(guest, "ms-dos")
                text = guest.wait(message)
                assert "requires 16380 KiB" in text
                assert f"has {available} KiB safely available" in text
                assert "Creating private" not in text
                if mib == 10:
                    assert "32 MiB or larger" in text and "do not erase" in text
                if files == 15:
                    assert "insufficient disk space" not in text
                guest.wait("> ")
                output = Path(__file__).resolve().parents[2]/"generated-artifacts"/"msdos-space"
                output.mkdir(parents=True, exist_ok=True)
                guest.command("screendump", {
                    "filename": str(output/f"{mib}mib-{files}files.ppm"),
                })
                shell(guest, "help")
                guest.wait("MS-DOS")
            finally:
                guest.close()
            assert path.read_bytes() == before, "Refusal modified the native disk"
            print(f"PASS: full kernel {mib}MiB/{files} files: {message}; disk unchanged")


if __name__ == "__main__":
    main()
