#!/usr/bin/env python3
"""Real DOS first setup from a floppy or separate CD; game CDs need no DOS marker."""
from pathlib import Path
import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
import shutil
import socket
from build_test_kernel import ROOT, build
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell, clear_dos


def native_disk(path):
    with path.open("wb") as f:
        f.truncate(40 * 1024 * 1024)
    directory = bytearray(512)
    struct.pack_into("<I", directory, 0, 0x46530001)
    struct.pack_into("<24sII", directory, 4, b"NATIVE.TXT", 2049, 11)
    with path.open("r+b") as f:
        f.seek(2048 * 512); f.write(directory); f.write(b"KEEP-NATIVE")


def protected(path):
    with path.open("rb") as f:
        digest = hashlib.sha256(f.read(2048 * 512)).digest()
        f.seek(2049 * 512)
        assert f.read(11) == b"KEEP-NATIVE"
    return digest


def make_cd(folder, image, name):
    folder.mkdir()
    shutil.copyfile(image, folder / name)
    iso = folder.with_suffix(".iso")
    subprocess.run(["xorriso", "-as", "mkisofs", "-quiet", "-iso-level", "1",
                    "-o", str(iso), str(folder)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return iso


def session(kernel, disk, folder, cd=None, floppy=None, extra_args=()):
    folder.mkdir()
    args = ["-drive", f"file={disk},if=ide,index=1,format=raw"]
    if floppy:
        args += ["-drive", f"file={floppy},if=floppy,index=0,format=raw,readonly=on"]
        args += ["-qtest", f"unix:{folder / 'fdc.sock'},server=on,wait=off"]
    guest = Guest(kernel, cd, False, folder, tuple(args)+tuple(extra_args))
    guest.sock.settimeout(20)
    guest.wait("Welcome to inteiliDOS"); guest.wait("> ")
    return guest


def unconfigure_floppy(guest, folder):
    """Simulate incompatible FDC settings and disabled ISA DMA/cascade."""
    guest.command("stop")
    try:
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(10);sock.connect(str(folder / "fdc.sock"))
            stream=sock.makefile("rwb",buffering=0)
            for port,value in ((0x08,4),(0xd0,4),(0xd4,4),(0xd6,0),
                               (0x3f2,0),(0x3f2,0x0c),(0x3f7,2),
                               (0x3f5,3),(0x3f5,0xdf),(0x3f5,3)):
                stream.write(f"outb {port:#x} {value:#x}\n".encode())
                assert stream.readline().strip()==b"OK"
    finally:
        guest.command("cont")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", nargs="?", type=Path)
    parser.add_argument("--media", type=Path, default=ROOT.parent / "MS-DOS_disks")
    args = parser.parse_args()
    disk1 = args.media.resolve() / "Disk1.img"
    before = hashlib.sha256(disk1.read_bytes()).digest()
    with tempfile.TemporaryDirectory(prefix="dos-external-") as folder:
        tmp = Path(folder)
        kernel = args.kernel.resolve() if args.kernel else build(tmp / "build")
        for i, command in enumerate(("ms-dos /floppy", "ms-dos")):
            disk = tmp / f"native{i}.img"; native_disk(disk)
            safe = protected(disk)
            guest = session(kernel, disk, tmp / f"floppy{i}", floppy=disk1)
            try:
                unconfigure_floppy(guest,tmp / f"floppy{i}")
                shell(guest, command)
                guest.wait("MS-DOS 6.22 -", timeout=150)
                guest.wait("C:\\>"); clear_dos(guest)
                shell(guest, "ver"); guest.wait("MS-DOS Version 6.22")
                shell(guest, "echo FLOPPY-SAVED>c:\\saved.txt")
                # F8 is an immediate CPU stop, not a guest filesystem sync.
                # Wait for DOS to finish/close the save and verify readback.
                clear_dos(guest)
                shell(guest, "type c:\\saved.txt"); guest.wait("FLOPPY-SAVED")
                guest.key("f8"); guest.wait("Returned from MS-DOS")
            finally:
                guest.close()
            assert protected(disk) == safe
            print(f"PASS: floppy-only setup using {command}, real DOS prompt, save and F8", flush=True)

        # A game CD has only GAME.IMG, with no D6221/DISK1 marker or OS kernel.
        game = make_cd(tmp / "game-cd", disk1, "GAME.IMG")
        guest = session(kernel, disk, tmp / "game", cd=game)
        try:
            shell(guest, "ms-dos /image game.img")
            guest.wait("MS-DOS 6.22 -"); guest.wait("C:\\>"); clear_dos(guest)
            shell(guest, "type c:\\saved.txt"); guest.wait("FLOPPY-SAVED")
            clear_dos(guest)
            shell(guest, "a:\\command.com /c ver"); guest.wait("MS-DOS Version 6.22")
            guest.key("f8"); guest.wait("Returned from MS-DOS")
        finally:
            guest.close()
        print("PASS: saved HDD re-entry and /IMAGE on a separate game-only CD", flush=True)

        # A separate data CD with a standard Disk1.img name is enough to seed
        # a fresh DOS HDD; no OS files or additional DOS disks are on this CD.
        cd = make_cd(tmp / "dos-cd", disk1, "Disk1.img")
        fresh = tmp / "fresh.img"; native_disk(fresh)
        fresh_safe = protected(fresh)
        guest = session(kernel, fresh, tmp / "cd", cd=cd)
        try:
            shell(guest, "ms-dos"); guest.wait("MS-DOS 6.22 -", timeout=150)
            guest.wait("C:\\>"); clear_dos(guest)
            shell(guest, "ver"); guest.wait("MS-DOS Version 6.22")
            guest.key("f8"); guest.wait("Returned from MS-DOS")
        finally:
            guest.close()
        assert protected(fresh) == fresh_safe
        assert protected(disk) == safe
        assert hashlib.sha256(disk1.read_bytes()).digest() == before
        print("PASS: separate Disk1-only CD setup, native files and source floppy unchanged", flush=True)


if __name__ == "__main__":
    main()
