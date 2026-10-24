#!/usr/bin/env python3
"""Boot the complete kernel; write real FAT USB files and verify saved bytes.

Disposable QEMU disks only. Also checks the existing iEdit app after its scratch
buffer became on-demand storage. No real host disks are opened.
"""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from build_test_kernel import build
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell, type_text
from run_msdos_space_test import fixture
from run_usb_storage_test import fat_image, files_on_disk

ROOT = Path(__file__).resolve().parents[1]
BODY = "inteiliWriter\n\nThis document was written to USB.\nTXT Markdown PDF"

def crop_ppm(path, col, row, cols, rows, trim_bottom=0):
    magic, dimensions, maximum, pixels = path.read_bytes().split(b"\n", 3)
    assert magic == b"P6" and maximum == b"255"
    width, height = map(int, dimensions.split())
    cw, ch = width // 80, height // 25
    return b"".join(pixels[(y * width + col * cw) * 3:
                           (y * width + (col + cols) * cw) * 3]
                    for y in range(row * ch, (row + rows) * ch - trim_bottom))


def shell_snapshot(g, path):
    shell(g, "cls")
    shell(g, "qwertyuiopasdfghjklzxcvbnm")
    g.wait("is not recognized as a command.")
    g.command("screendump", {"filename": str(path)})


def native_files(path):
    raw = path.read_bytes()
    start = struct.unpack_from("<I", raw, 454)[0]
    directory = (start + 1033) * 512
    result = {}
    for i in range(15):
        name, lba, size = struct.unpack_from("<24sII", raw, directory + 4 + i * 32)
        if name[0]:
            result[name.split(b"\0")[0].decode()] = raw[lba * 512:lba * 512 + size]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT.parent / "generated-artifacts/writer")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="writer-qemu-") as directory:
        tmp = Path(directory)
        kernel = args.kernel.resolve() if args.kernel else build(tmp / "build", modern=True)
        stick = tmp / "usb.img"
        original = fat_image(stick, 16, True)
        native = tmp / "native.img"
        native.write_bytes(fixture(40, 1)[0])
        disk_args = (
            "-cpu", "core2duo", "-m", "256", "-device", "qemu-xhci,id=usb",
            "-device", "usb-kbd,bus=usb.0,port=2",
            "-drive", f"if=none,id=stick,file={stick},format=raw",
            "-device", "usb-storage,drive=stick,bus=usb.0,port=1",
            "-drive", f"file={native},if=ide,index=0,format=raw")
        g = Guest(kernel, None, False, tmp, extra_args=disk_args)
        try:
            g.wait("Welcome to inteiliDOS", timeout=45)
            shell(g, "usb /mount"); g.wait("1 USB drive(s) mounted.")
            shell_snapshot(g, output / "shell-before.ppm")
            shell(g, "docs"); g.wait("inteiliWriter | Word processor")
            type_text(g, "hello world")
            for _ in range(5): g.key("left")
            type_text(g, "brave ")
            g.wait("hello brave world")
            g.key("f2"); g.wait("Save filename")
            type_text(g, "FIRST.TXT\n"); g.wait("Save to:")
            g.key("3"); g.wait("USB drive:"); g.key("0")
            g.wait("Document saved.")
            g.key("f3"); g.wait("Load TXT/Markdown")
            type_text(g, "USB0:\\FIRST.TXT\n"); g.wait("Document loaded.")
            g.key("right"); g.key("f4"); g.wait("Logical line deleted.")
            type_text(g, BODY)
            for key, filename in (("f5", "DOC.TXT"), ("f6", "DOC.MD"), ("f7", "DOC.PDF")):
                g.key(key); g.wait("USB export filename:")
                type_text(g, filename + "\n"); g.wait("USB drive:"); g.key("0")
                g.wait("USB export complete.")
            g.command("screendump", {"filename": str(output / "writer.ppm")})
            g.key("f2"); g.wait("Save filename")
            # Previous USB filename is prefilled; Backspace clears it.
            for _ in range(len("USB0:\\FIRST.TXT")): g.key("backspace")
            type_text(g, "NATIVE.TXT\n"); g.wait("Save to:"); g.key("1")
            g.wait("Document saved."); g.key("f10")
            shell(g, "inteiliwriter"); g.wait("inteiliWriter | Word processor")
            g.key("f3"); g.wait("Load TXT/Markdown")
            type_text(g, "NATIVE.TXT\n"); g.wait("Load from:"); g.key("1")
            g.wait("Document loaded."); g.wait("This document was written to USB.")
            g.key("f10")
            # Ensure the small iEdit allocation change preserves native behavior.
            shell(g, "iedit"); g.wait("iEdit")
            type_text(g, "legacy editor")
            g.key("f2"); g.wait("Save as")
            type_text(g, "LEGACY.TXT\n"); g.wait("Save to:"); g.key("3")
            g.wait("USB drive:"); g.key("0"); g.wait("Saved:")
            g.key("f10")
            shell(g, "docs"); g.wait("inteiliWriter | Word processor")
            type_text(g, "sample "); g.key("f8"); type_text(g, "sample")
            g.key("f8"); type_text(g, " "); g.key("f9"); type_text(g, "sample")
            g.key("f8"); type_text(g, " sample"); g.key("f9"); g.key("f8")
            g.wait("B:off"); g.wait("I:off")
            for key, name in (("f5", "STYLE.TXT"), ("f6", "STYLE.MD"), ("f7", "STYLE.PDF")):
                g.key(key); g.wait("USB export filename:")
                type_text(g, "USB0:\\" + name + "\n"); g.wait("USB export complete.")
            g.command("screendump", {"filename": str(output / "formatted.ppm")})
            # Four genuinely different glyph bitmaps, not just different colors.
            glyphs = [crop_ppm(output / "formatted.ppm", col, 3, 6, 1, 2)
                      for col in (2, 9, 16, 23)]
            assert len(set(glyphs)) == 4, "Missing native bold/italic font rendering"
            colors = [frozenset(glyph[i:i + 3] for i in range(0, len(glyph), 3))
                      for glyph in glyphs]
            assert len(set(colors)) == 1, "Font styles must not just change ink colors"
            g.key("f2"); g.wait("Save filename")
            type_text(g, "USB0:\\STYLE.MD\n"); g.wait("File exists.")
            g.key("y"); g.wait("Document saved."); g.key("f10")
            shell(g, "docs"); g.wait("inteiliWriter | Word processor")
            g.key("f3"); g.wait("Load TXT/Markdown")
            type_text(g, "USB0:\\STYLE.MD\n"); g.wait("Document loaded.")
            g.command("screendump", {"filename": str(output / "formatted-reloaded.ppm")})
            restored = [crop_ppm(output / "formatted-reloaded.ppm", col, 3, 6, 1, 2)
                        for col in (2, 9, 16, 23)]
            assert restored == glyphs, "Markdown reload lost character formatting"
            g.key("f6"); g.wait("USB export filename:")
            type_text(g, "USB0:\\COPY.MD\n"); g.wait("USB export complete."); g.key("f10")
            shell_snapshot(g, output / "shell-after.ppm")
            assert crop_ppm(output / "shell-before.ppm", 0, 0, 80, 2) == \
                crop_ppm(output / "shell-after.ppm", 0, 0, 80, 2), "Shell font/color restoration failed"
            shell(g, "usb /eject"); g.wait("USB storage eject complete.")
        finally:
            g.close()
        files = files_on_disk(stick)
        assert files["HELLO.TXT"] == original
        assert files["FIRST.TXT"] == b"hello brave world"
        assert files["DOC.TXT"] == BODY.replace("\n", "\r\n").encode()
        assert files["DOC.MD"] == BODY.encode()
        assert files["LEGACY.TXT"] == b"legacy editor\n"
        assert files["STYLE.TXT"] == b"sample sample sample sample"
        assert files["STYLE.MD"] == b"sample **sample** *sample* ***sample***"
        assert files["COPY.MD"] == files["STYLE.MD"]
        (output / "STYLE.PDF").write_bytes(files["STYLE.PDF"])
        if shutil.which("pdffonts"):
            fonts = subprocess.check_output(["pdffonts", str(output / "STYLE.PDF")])
            for font in (b"Courier-Bold", b"Courier-Oblique", b"Courier-BoldOblique"):
                assert font in fonts
        assert native_files(native)["NATIVE.TXT"] == files["DOC.TXT"]
        assert native_files(native)["KEEP0.TXT"] == b"KEEP"
        (output / "DOC.PDF").write_bytes(files["DOC.PDF"])
        if shutil.which("pdftotext"):
            text = subprocess.check_output(["pdftotext", str(output / "DOC.PDF"), "-"], text=True)
            assert "This document was written to USB." in text
            assert "TXT Markdown PDF" in text
        if shutil.which("magick"):
            for name in ("writer", "formatted", "formatted-reloaded", "shell-after"):
                subprocess.run(["magick", str(output / (name + ".ppm")),
                                str(output / (name + ".png"))], check=True)
        print("PASS: real keyboard navigation, save/load/delete, USB TXT/MD/PDF bytes, "
              "PDF fonts/reader, DOCS alias, four VGA styles, Markdown style reload, "
              "shell font restoration, native persistence, preserved files and iEdit", flush=True)


if __name__ == "__main__":
    main()
