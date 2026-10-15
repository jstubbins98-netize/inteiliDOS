#!/usr/bin/env python3
"""Run a supplied 360K Zork Trilogy disk through physical and CD image paths."""
from pathlib import Path
import argparse
import hashlib
import tempfile
import time
import socket
from run_msdos_external_media_test import native_disk, protected, make_cd, session
from run_msdos622_test import shell, clear_dos, type_text

def command(guest, text):
    for ch in text + "\n":
        type_text(guest, ch)
        time.sleep(0.12)


def play(guest, output, name):
    command(guest, "a:")
    guest.wait("A:\\>")
    command(guest, "dir")
    guest.wait("ZORK1")
    guest.wait("STORY")
    assert "45,672,296" not in guest.screen()
    command(guest, "zork1")
    guest.wait("West of House", timeout=40)
    command(guest, "look")
    guest.wait("mailbox")
    command(guest, "open mailbox")
    guest.wait("leaflet")
    guest.command("screendump", {"filename": str(output / (name + ".ppm"))})
    guest.key("f8")
    guest.wait("Returned from MS-DOS")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("kernel", type=Path)
    p.add_argument("game", type=Path)
    p.add_argument("--media", type=Path, default=Path("MS-DOS_disks/Disk1.img"))
    a = p.parse_args()
    kernel, game = a.kernel.resolve(), a.game.resolve()
    original = hashlib.sha256(game.read_bytes()).digest()
    assert game.stat().st_size == 368640
    output = Path("generated-artifacts/msdos-zork").resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="dos-zork-") as folder:
        t = Path(folder)
        disk = t / "native.img"
        native_disk(disk)
        before = protected(disk)
        cd = make_cd(t / "dos-cd", a.media.resolve(), "Disk1.img")
        guest = session(kernel, disk, t / "seed", cd=cd)
        try:
            shell(guest, "ms-dos")
            guest.wait("MS-DOS 6.22 -", timeout=150)
            guest.wait("C:\\>")
            guest.key("f8")
            guest.wait("Returned from MS-DOS")
        finally:
            guest.close()
        # Compare the ROM/filesystem path independently of the native FDC.
        cd = make_cd(t / "game-cd", game, "ZORK.IMG")
        guest = session(kernel, disk, t / "virtual", cd=cd)
        try:
            shell(guest, "ms-dos /image zork.img")
            guest.wait("MS-DOS 6.22 -")
            clear_dos(guest)
            play(guest, output, "virtual")
            print("PASS: supplied 360K CD-hosted image and gameplay", flush=True)
        finally:
            guest.close()
        # No DOS installation CD present: boot saved C: with the user's disk.
        guest = session(kernel, disk, t / "physical", floppy=game,
                        extra_args=("-global","isa-fdc.fdtypeA=120"))
        try:
            # QEMU's auto profile gives 360K media 40 tracks/two heads, but
            # labels that model as a 1.2M drive without emulating double-step.
            # Declare its actual 40-track drive in CMOS, not an 80-track HD
            # mechanism. This is a test-only correction, never guest code.
            guest.command("stop")
            with socket.socket(socket.AF_UNIX) as sock:
                sock.connect(str(t/"physical"/"fdc.sock"))
                stream=sock.makefile("rwb",buffering=0)
                for port,value in ((0x70,0x10),(0x71,0x10)):
                    stream.write(f"outb {port:#x} {value:#x}\n".encode())
                    assert stream.readline().strip()==b"OK"
            guest.command("cont")
            shell(guest, "ms-dos /floppy")
            guest.wait("MS-DOS 6.22 -")
            clear_dos(guest)
            play(guest, output, "physical")
            print("PASS: supplied 360K physical Zork disk, directory, batch launch and gameplay", flush=True)
        finally:
            guest.close()
        assert protected(disk) == before
    assert hashlib.sha256(game.read_bytes()).digest() == original
    print("PASS: native files and supplied game image unchanged", flush=True)


if __name__ == "__main__":
    main()
