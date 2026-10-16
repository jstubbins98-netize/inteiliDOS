#!/usr/bin/env python3
"""Real DOS USB installation, streaming, swaps and media-free saved-HDD boot.

Uses licensed DOS images supplied locally; only disposable test disks change.
"""
from pathlib import Path
import argparse
import hashlib
import struct
import tempfile

from build_test_kernel import ROOT, build
from run_launchpad_dos_test import Guest
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


def start(kernel, native, folder, sticks=()):
    folder.mkdir()
    args = ["-cpu", "core2duo", "-m", "256", "-device", "qemu-xhci,id=usb",
            "-device", "usb-kbd,bus=usb.0,port=2",
            "-drive", f"file={native},if=ide,index=1,format=raw"]
    for i, stick in enumerate(sticks):
        args += ["-drive", f"if=none,id=stick{i},file={stick},format=raw",
                 "-device", f"usb-storage,id=usbdrive{i},drive=stick{i},bus=usb.0,port={1 if i == 0 else 3}"]
    g = Guest(kernel, None, False, folder, tuple(args), qemu_binary="qemu-system-x86_64")
    g.wait("Welcome to inteiliDOS", timeout=45)
    g.wait("> ")
    return g


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", nargs="?", type=Path)
    parser.add_argument("--media", type=Path, default=ROOT.parent / "MS-DOS_disks")
    parser.add_argument("--game", type=Path, default=ROOT.parent /
                        "attached_assets/0_Zork_Trilogy_1791773796909.img")
    args = parser.parse_args()
    names = ("Disk1.img", "Disk2.img", "Disk3.img", "Suppdisk.img")
    images = {name.upper(): (args.media / name).read_bytes() for name in names}
    original = {name: hashlib.sha256(value).digest() for name, value in images.items()}
    game=args.game.read_bytes()
    output = ROOT.parent / "generated-artifacts/msdos-usb"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="msdos-usb-") as folder:
        tmp = Path(folder)
        kernel = args.kernel.resolve() if args.kernel else build(tmp / "build", modern=True)
        native = tmp / "native.img"
        native_disk(native)
        safe = protected(native)
        empty, source = tmp / "empty.img", tmp / "source.img"
        fat_image(empty, 16, True)
        add_files(empty, {"ZORK.IMG": game, "BAD.IMG": bytes(512)})
        game_hash=hashlib.sha256(empty.read_bytes()).digest()
        fat_image(source, 32)
        add_files(source, images)
        source_hash = hashlib.sha256(source.read_bytes()).digest()
        g = start(kernel, native, tmp / "install", (empty, source))
        try:
            shell(g, "ms-dos /usb")
            g.wait("USB drive not mounted"); g.wait("C:\\>")
            shell(g, "usb /mount"); g.wait("USB1:")
            shell(g, "ms-dos /usb USB4:"); g.wait("Usage: MS-DOS")
            shell(g, "ms-dos /usb USB0:"); g.wait("USB Disk 1 missing")
            shell(g, "ms-dos /usb USB0:\\ZORK.IMG")
            g.wait("install DOS first");g.wait("C:\\>")
            shell(g, "ms-dos /usb")
            g.wait("MS-DOS 6.22 -", timeout=180); g.wait("C:\\>")
            clear_dos(g)
            shell(g, "ver"); g.wait("MS-DOS Version 6.22")
            clear_dos(g)
            shell(g, "dir a:"); g.wait("COMMAND"); g.wait("C:\\>")
            clear_dos(g)
            g.key("f6")
            shell(g, "dir a:")
            # Disk 2 contains EXPAND.EXE; Disk 1 does not.
            g.wait("EXPAND"); g.wait("C:\\>")
            clear_dos(g)
            shell(g, "echo USB-INSTALL-SAVED>c:\\usbtest.txt")
            g.wait("C:\\>"); clear_dos(g)
            shell(g, "type c:\\usbtest.txt"); g.wait("USB-INSTALL-SAVED")
            g.command("screendump", {"filename": str(output / "usb-dos.ppm")})
            g.key("f8"); g.wait("Returned from MS-DOS")
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
        g = start(kernel, native, tmp / "saved")
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
        g = start(kernel, native, tmp / "invalid", (bad,))
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
