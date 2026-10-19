#!/usr/bin/env python3
"""Real DOS EGA/VGA bus, visible raster, F8/font and native storage regression.

Usage: python3 tests/run_msdos_video_test.py [kernel] [DOS disk directory]
Only disposable test media are written. Requires locally supplied DOS disks.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from build_test_kernel import ROOT, build, run
from run_launchpad_dos_test import Guest, fat12_image
from run_msdos622_test import prepare, shell, clear_dos
from run_msdos_external_media_test import native_disk, protected


def ppm(path):
    data = path.read_bytes()
    # QEMU emits a P6 header without comments.
    magic, size, maximum, raster = data.split(b"\n", 3)
    assert magic == b"P6" and maximum == b"255"
    width, height = map(int, size.split())
    assert len(raster) == width * height * 3
    return width, height, raster


def screenshot(guest, path):
    guest.command("screendump", {"filename": str(path)})
    return ppm(path)


def frame(guest, path, expected):
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        width, height, pixels = screenshot(guest, path)
        at = ((height // 2) * width + width // 2) * 3
        # VGA's six-bit DAC can be expanded by shift or bit replication.
        if all(abs(a-b) <= 3 for a,b in zip(pixels[at:at+3], expected)):
            return
        if "DOS EGA VGA TEST FAILED" in guest.screen():
            raise AssertionError("Guest VGA bus/BIOS self-check failed")
        time.sleep(.1)
    raise AssertionError(f"Missing raster {expected}; see {path}\n{guest.screen()}")


def hello_glyph(guest, path):
    text = guest.screen().splitlines()
    row = max(i for i, line in enumerate(text) if "Hello!" in line)
    col = text[row].index("Hello!")
    w, h, image = screenshot(guest, path)
    cw, ch = w // 80, h // 25
    # Ignore foreground color, compare the actual rendered font bitmap.
    return bytes(
        any(image[(y*w+x)*3:(y*w+x)*3+3])
        for y in range(row*ch, (row+1)*ch)
        for x in range(col*cw, (col+6)*cw)
    )


def main():
    output = ROOT.parent / "generated-artifacts/msdos-video"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="msdos-video-") as folder:
        tmp = Path(folder)
        kernel = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(tmp/"build")
        media = Path(sys.argv[2]).resolve() if len(sys.argv)>2 else ROOT.parent/"MS-DOS_disks"
        prepare(media, tmp/"disc")
        run("nasm", "-f", "bin", ROOT/"tests/msdos_video.asm", "-o", tmp/"video.com")
        (tmp/"disc/VIDEO.IMG").write_bytes(fat12_image({
            "VIDEO.COM": (tmp/"video.com").read_bytes(),
            "PIXELS.BIN": bytes([0x12])*2048,
        }))
        run("xorriso", "-as", "mkisofs", "-quiet", "-iso-level", "1",
            "-o", tmp/"dos.iso", tmp/"disc")
        native = tmp/"native.img"
        native_disk(native)
        safe = protected(native)
        guest = Guest(kernel, tmp/"dos.iso", False, tmp,
                      ("-drive", f"file={native},if=ide,index=1,format=raw"))
        try:
            guest.wait("Welcome to inteiliDOS");guest.wait("> ")
            shell(guest, "hello");guest.wait("Hello!")
            before = hello_glyph(guest, output/"shell-before.ppm")
            shell(guest, "ms-dos")
            guest.wait("MS-DOS 6.22 -", timeout=180);guest.wait("C:\\>")
            guest.key("f8");guest.wait("Returned from MS-DOS")
            shell(guest, "ms-dos /image VIDEO.IMG")
            guest.wait("MS-DOS 6.22 -", timeout=90);guest.wait("C:\\>")
            clear_dos(guest);shell(guest, "a:\\video.com")
            for name, color in (
                ("vga13", (170, 0, 0)), ("ega0d", (0, 170, 0)),
                ("vga12", (0, 0, 170)), ("modex", (255, 255, 85)),
                ("dac", (255, 0, 255)),
            ):
                frame(guest, output/(name+".ppm"), color)
                print(f"PASS: visible {name} raster, CPU bus and BIOS path", flush=True)
                guest.key("spc")
            guest.wait("DOS EGA VGA BUS AND BIOS OK");guest.wait("C:\\>")
            assert "DOS EGA VGA TEST FAILED" not in guest.screen()
            guest.key("f8");guest.wait("Returned from MS-DOS")
            shell(guest, "hello");guest.wait("Hello!")
            assert hello_glyph(guest, output/"shell-after.ppm") == before, "Native font corrupted"
            # Exit directly from graphics, without a guest text-mode reset.
            shell(guest, "ms-dos /image VIDEO.IMG")
            guest.wait("MS-DOS 6.22 -", timeout=90);guest.wait("C:\\>")
            clear_dos(guest);shell(guest, "a:\\video.com")
            frame(guest, output/"f8-graphics.ppm", (170, 0, 0))
            guest.key("f8");guest.wait("Returned from MS-DOS")
            shell(guest, "hello");guest.wait("Hello!")
            assert hello_glyph(guest, output/"shell-f8.ppm") == before, "F8 font corrupted"
        finally:
            guest.close()
        assert protected(native) == safe, "Native storage changed"
        print("PASS: real DOS, planar/chain-4/string/latch/word-port operations, text return, "
              "F8 in graphics, rendered shell font and protected native storage", flush=True)


if __name__ == "__main__":
    main()
