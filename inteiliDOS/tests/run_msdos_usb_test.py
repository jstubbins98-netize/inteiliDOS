#!/usr/bin/env python3
"""Real DOS USB installation, streaming, swaps and media-free saved-HDD boot.

Uses licensed DOS images supplied locally; only disposable test disks change.
"""
from pathlib import Path
import argparse
import hashlib
import struct
import tempfile
import subprocess
import time

from build_test_kernel import ROOT, build
from run_launchpad_dos_test import Guest
from run_launchpad_dos_test import fat12_image
from build_test_kernel import run
from run_msdos622_test import shell, clear_dos
from run_msdos_external_media_test import native_disk, protected
from run_usb_storage_test import fat_image


def add_files(path, files):
    """Add multi-cluster image files to the existing small FAT test fixture."""
    b = bytearray(path.read_bytes())
    prefix = struct.unpack_from("<I", b, 454)[0] if b[450] in (6, 12) else 0
    v = memoryview(b)[prefix * 512:]
    spc = v[13]
    reserved = struct.unpack_from("<H", v, 14)[0]
    roots = struct.unpack_from("<H", v, 17)[0]
    fat = struct.unpack_from("<H", v, 22)[0] or struct.unpack_from("<I", v, 36)[0]
    bits = 16 if roots else 32
    data = reserved + 2 * fat + (roots * 32 + 511) // 512
    root = (reserved + 2 * fat) * 512
    # Fixture files use earlier clusters; cluster 64 onward is free.
    cluster = 64
    for entry, (name, payload) in enumerate(files.items(), 2):
        count = (len(payload) + spc * 512 - 1) // (spc * 512)
        offset = root + entry * 32
        stem, extension = name.split(".")
        v[offset:offset + 11] = (stem.ljust(8) + extension.ljust(3)).encode()
        v[offset + 11] = 0x20
        struct.pack_into("<H", v, offset + 26, cluster)
        struct.pack_into("<I", v, offset + 28, len(payload))
        for i in range(count):
            value = cluster + i + 1 if i + 1 < count else (0xFFFF if bits == 16 else 0x0FFFFFFF)
            struct.pack_into("<H" if bits == 16 else "<I", v,
                             reserved * 512 + (cluster + i) * (bits // 8), value)
        start = (data + (cluster - 2) * spc) * 512
        assert start + len(payload) <= len(v)
        v[start:start + len(payload)] = payload
        cluster += count
    v[(reserved + fat) * 512:(reserved + 2 * fat) * 512] = v[reserved * 512:(reserved + fat) * 512]
    path.write_bytes(b)


def start(kernel, native, folder, sticks=(), sata=False):
    folder.mkdir()
    args = ["-cpu", "core2duo", "-m", "256", "-device", "qemu-xhci,id=usb",
            "-device", "usb-kbd,bus=usb.0,port=2"]
    args += (["-device","ahci,id=sata",
              "-drive",f"file={native},if=none,id=native,format=raw",
              "-device","ide-hd,drive=native,bus=sata.0"] if sata else
             ["-drive",f"file={native},if=ide,index=1,format=raw"])
    for i, stick in enumerate(sticks):
        args += ["-drive", f"if=none,id=stick{i},file={stick},format=raw",
                 "-device", f"usb-storage,id=usbdrive{i},drive=stick{i},bus=usb.0,port={1 if i == 0 else 3}"]
    g = Guest(kernel, None, False, folder, tuple(args), qemu_binary="qemu-system-x86_64")
    g.wait("Welcome to inteiliDOS", timeout=45)
    g.wait("> ")
    return g


def wait_usb_image(g, kernel, folder, expected):
    """Wait for the swap itself, not a fixed USB-transfer/key-press delay."""
    symbols=subprocess.check_output(["nm","-n",str(kernel)],text=True)
    address=next(int(line.split()[0],16) for line in symbols.splitlines()
                 if line.split()[-1:] == ["usb_image"])
    snapshot=folder/"selected-usb-image.bin"
    deadline=time.monotonic()+15
    actual=""
    while time.monotonic()<deadline:
        g.command("pmemsave",{"val":address,"size":128,"filename":str(snapshot)})
        actual=snapshot.read_bytes().split(b"\0",1)[0].decode("ascii",errors="replace")
        if actual.upper()==expected.upper():
            return
        time.sleep(.05)
    raise AssertionError(f"USB swap did not select {expected!r}; active image={actual!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", nargs="?", type=Path)
    parser.add_argument("--sata", action="store_true", help="Use a native AHCI HDD")
    parser.add_argument("--media", type=Path, default=ROOT.parent / "MS-DOS_disks")
    parser.add_argument("--game", type=Path, default=ROOT.parent /
                        "attached_assets/0_Zork_Trilogy_1791773796909.img")
    args = parser.parse_args()
    names = ("Disk1.img", "Disk2.img", "Disk3.img", "Suppdisk.img")
    images = {name.upper(): (args.media / name).read_bytes() for name in names}
    original = {name: hashlib.sha256(value).digest() for name, value in images.items()}
    game=args.game.read_bytes()
    output = ROOT.parent / ("generated-artifacts/msdos-usb"+("-sata" if args.sata else ""))
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="msdos-usb-") as folder:
        tmp = Path(folder)
        kernel = args.kernel.resolve() if args.kernel else build(tmp / "build", modern=True)
        native = tmp / "native.img"
        native_disk(native)
        safe = protected(native)
        empty, source = tmp / "empty.img", tmp / "source.img"
        fat_image(empty, 16, True)
        run("nasm","-f","bin",ROOT/"tests/msdos_usb_swap.asm","-o",tmp/"swap.com")
        swap=(tmp/"swap.com").read_bytes()
        first=fat12_image({"SWAP.COM":swap,"DISKID.TXT":b"1"})
        second=fat12_image({"DISKID.TXT":b"2"})
        add_files(empty, {"ZORK.IMG": game, "BAD.IMG": bytes(512),
                          "FIRST.IMG":first,"SECOND.IMG":second})
        game_hash=hashlib.sha256(empty.read_bytes()).digest()
        fat_image(source, 32)
        add_files(source, images)
        source_hash = hashlib.sha256(source.read_bytes()).digest()
        g = start(kernel, native, tmp / "install", (empty, source), args.sata)
        try:
            shell(g, "ms-dos /usb")
            g.wait("USB drive not mounted"); g.wait("C:\\>")
            shell(g, "usb /mount"); g.wait("USB1:")
            shell(g, "ms-dos /usb USB4:"); g.wait("Usage: MS-DOS")
            shell(g, "ms-dos /usb USB0:"); g.wait("USB Disk 1 missing")
            shell(g, "ms-dos /usb USB0:\\ZORK.IMG")
            g.wait("install DOS first");g.wait("C:\\>")
            if args.sata:
                shell(g,"ms-dos /usb")
                g.wait("DOS HDD clear:",timeout=90)
                g.command("screendump",{"filename":str(output/"creating-disk.ppm")})
                g.key("f8");g.wait("cancelled with F8",timeout=30);g.wait("C:\\>")
                assert protected(native)==safe
                with native.open("rb") as f:
                    f.seek(2048*512)
                    assert b"MSDOS622.HDD" not in f.read(512)
            shell(g, "ms-dos /usb")
            g.wait("MS-DOS 6.22 -", timeout=180); g.wait("C:\\>")
            clear_dos(g)
            shell(g, "ver"); g.wait("MS-DOS Version 6.22")
            clear_dos(g)
            # Restrict listings so the target entry cannot scroll off-screen.
            shell(g, "dir a:\\command.com"); g.wait("COMMAND"); g.wait("C:\\>")
            clear_dos(g)
            g.key("f6")
            shell(g, "dir a:\\choice.com")
            # Disk 2 contains CHOICE.COM; EXPAND.EXE is on Disk 1.
            g.wait("CHOICE"); g.wait("C:\\>")
            clear_dos(g)
            shell(g, "echo USB-INSTALL-SAVED>c:\\usbtest.txt")
            g.wait("C:\\>"); clear_dos(g)
            shell(g, "type c:\\usbtest.txt"); g.wait("USB-INSTALL-SAVED")
            g.command("screendump", {"filename": str(output / "usb-dos.ppm")})
            g.key("f8"); g.wait("Returned from MS-DOS")
            # One running COM program reads both changed disks and wraps to
            # disk one. No guest reboot, host-shell visit, or media writes.
            shell(g,"ms-dos /usb USB0:\\FIRST.IMG USB0:\\SECOND.IMG")
            g.wait("MS-DOS 6.22 -",timeout=90);g.wait("C:\\>")
            clear_dos(g)
            shell(g,"a:\\swap.com")
            g.wait("USB SWAP: F6 NEXT, THEN SPACE")
            g.key("f6")
            wait_usb_image(g,kernel,tmp,"USB0:\\SECOND.IMG")
            g.key("spc")
            g.wait("USB SWAP: F6 BACK, THEN SPACE")
            g.key("f6")
            wait_usb_image(g,kernel,tmp,"USB0:\\FIRST.IMG")
            g.key("spc")
            g.wait("USB LIVE DISK SWAP OK");g.wait("C:\\>")
            assert "USB LIVE DISK SWAP FAILED" not in g.screen()
            g.command("screendump",{"filename":str(output/"live-usb-swap.ppm")})
            g.key("f8");g.wait("Returned from MS-DOS")
            # A bad next image must leave the working first disk mounted.
            shell(g,"ms-dos /usb USB0:\\FIRST.IMG USB0:\\BAD.IMG")
            g.wait("MS-DOS 6.22 -",timeout=90);g.wait("C:\\>")
            clear_dos(g);g.key("f6")
            shell(g,"type a:\\diskid.txt");g.wait("1")
            g.key("f8");g.wait("Returned from MS-DOS")
            shell(g, "ms-dos /usb USB1:")
            g.wait("MS-DOS 6.22 -", timeout=90); g.wait("C:\\>")
            clear_dos(g)
            shell(g, "type c:\\usbtest.txt"); g.wait("USB-INSTALL-SAVED")
            g.key("f8"); g.wait("Returned from MS-DOS")
            # The saved C: boots with a 360K game image on a different USB
            # volume, without requiring DOS installation images on that drive.
            shell(g, "ms-dos /usb USB0:\\MISSING.IMG")
            g.wait("cannot read USB game image");g.wait("C:\\>")
            shell(g, "ms-dos /usb USB0:\\BAD.IMG")
            g.wait("invalid USB floppy image");g.wait("C:\\>")
            shell(g, "ms-dos /usb USB0:\\ZORK.IMG")
            g.wait("MS-DOS 6.22 -", timeout=90);g.wait("C:\\>")
            clear_dos(g)
            from run_msdos_zork_test import play
            play(g, output, "usb-zork")
            shell(g, "usb /eject"); g.wait("safe to remove")
            shell(g, "hello"); g.wait("Hello!")
        finally:
            g.close()
        assert hashlib.sha256(source.read_bytes()).digest() == source_hash
        assert hashlib.sha256(empty.read_bytes()).digest() == game_hash
        assert protected(native) == safe

        # Saved DOS C: must still boot after all USB media is removed.
        g = start(kernel, native, tmp / "saved", sata=args.sata)
        try:
            shell(g, "ms-dos")
            g.wait("MS-DOS 6.22 -", timeout=90); g.wait("C:\\>")
            clear_dos(g)
            shell(g, "type c:\\usbtest.txt"); g.wait("USB-INSTALL-SAVED")
            g.key("f8"); g.wait("Returned from MS-DOS")
        finally:
            g.close()

        # Explicit corrupt USB media must not silently boot the existing HDD.
        bad = tmp / "bad.img"
        fat_image(bad, 16, True)
        add_files(bad, {"D6221.IMG": bytes(512)})
        bad_hash = hashlib.sha256(bad.read_bytes()).digest()
        g = start(kernel, native, tmp / "invalid", (bad,), args.sata)
        try:
            shell(g, "usb /mount"); g.wait("USB0:")
            shell(g, "ms-dos /usb USB0:")
            g.wait("not a valid 1.44 MB FAT12 DOS image"); g.wait("C:\\>")
            assert "MS-DOS 6.22 -" not in g.screen()
        finally:
            g.close()
        assert hashlib.sha256(bad.read_bytes()).digest() == bad_hash
        assert protected(native) == safe
    for name, digest in original.items():
        actual = next(p for p in args.media.iterdir() if p.name.upper() == name)
        assert hashlib.sha256(actual.read_bytes()).digest() == digest
    print("PASS: USB mount required, missing/invalid images and drive IDs rejected, "
          "FAT32 multi-cluster 1.44 MB streaming, USB1 auto/explicit selection, "
          "Disk 2 swap, real DOS execution, F8, keyboard after eject, native data "
          "and source images unchanged, saved HDD boot without USB")


if __name__ == "__main__":
    main()
