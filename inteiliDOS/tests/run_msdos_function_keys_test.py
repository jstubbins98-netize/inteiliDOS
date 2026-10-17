#!/usr/bin/env python3
"""Function keys through real DOS/INT16, with PS/2 and USB keyboards.

Only disposable images change. Licensed DOS media must be supplied locally.
"""
from pathlib import Path
import sys
import tempfile
from build_test_kernel import ROOT, build, run
from run_launchpad_dos_test import Guest, fat12_image
from run_msdos622_test import prepare, shell, clear_dos
from run_msdos_external_media_test import native_disk, protected


def main():
    output=ROOT.parent/"generated-artifacts/msdos-function-keys"
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="msdos-fkeys-") as folder:
        tmp=Path(folder)
        # The modern test profile deliberately disables PS/2 input.
        # Use separate input profiles, rather than sending PS/2 events to it.
        ps2_kernel=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(tmp/"ps2-build")
        usb_kernel=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else build(tmp/"usb-build",modern=True)
        media=Path(sys.argv[3]).resolve() if len(sys.argv)>3 else ROOT.parent/"MS-DOS_disks"
        prepare(media,tmp/"disc")
        run("nasm","-f","bin",ROOT/"tests/msdos_function_keys.asm","-o",tmp/"fkey.com")
        # Keep the shipped BIOS overlay reproducible from its NASM source.
        run("nasm","-f","bin",ROOT/"kernel/msdos/bios_keyboard.asm","-o",tmp/"keyboard.bin")
        run(sys.executable,ROOT/"cmake/bin2header.py",tmp/"keyboard.bin",
            "msdos_bios_keyboard",tmp/"bios_keyboard.h")
        assert (tmp/"bios_keyboard.h").read_bytes() == (
            ROOT/"kernel/msdos/bios_keyboard.h").read_bytes(), "Stale BIOS keyboard overlay"
        (tmp/"disc/KEYS.IMG").write_bytes(fat12_image({"FKEY.COM":(tmp/"fkey.com").read_bytes()}))
        run("xorriso","-as","mkisofs","-quiet","-iso-level","1",
            "-o",tmp/"keys.iso",tmp/"disc")
        native=tmp/"native.img"
        native_disk(native);safe=protected(native)
        for usb in (False,True):
            label="usb" if usb else "ps2"
            session=tmp/label;session.mkdir()
            kernel=usb_kernel if usb else ps2_kernel
            args=["-drive",f"file={native},if=ide,index=1,format=raw"]
            if usb:
                args+=["-cpu","core2duo","-m","256",
                       "-device","qemu-xhci,id=usb","-device","usb-kbd,bus=usb.0"]
            g=Guest(kernel,tmp/"keys.iso",False,session,tuple(args))
            try:
                g.wait("Welcome to inteiliDOS");g.wait("> ")
                shell(g,"ms-dos")
                g.wait("MS-DOS 6.22 -",timeout=180);g.wait("C:\\>")
                g.key("f8");g.wait("Returned from MS-DOS")
                shell(g,"ms-dos /image KEYS.IMG")
                g.wait("MS-DOS 6.22 -",timeout=90);g.wait("C:\\>")
                clear_dos(g)
                shell(g,"a:\\fkey.com")
                for key,prompt in (("f10","INSTALLER TEST: PRESS F10"),
                                   ("f9","PRESS F9"),("f1","PRESS F1"),("f5","PRESS F5"),
                                   ("f11","LEGACY: PRESS F11"),
                                   ("f12","LEGACY: PRESS F12"),
                                   ("f11","ENHANCED: PRESS F11"),
                                   ("f12","ENHANCED: PRESS F12")):
                    g.wait(prompt)
                    if key=="f10":
                        # Reserved media controls must not leak into INT 16h.
                        g.key("f6");g.key("f7")
                    g.key(key)
                g.wait("DOS FUNCTION KEYS OK");g.wait("C:\\>")
                assert "DOS FUNCTION KEY FAILED" not in g.screen()
                g.command("screendump",{"filename":str(output/(label+".ppm"))})
                g.key("f8");g.wait("Returned from MS-DOS")
                shell(g,"hello");g.wait("Hello!")
            finally:
                g.close()
            assert protected(native)==safe
            print(f"PASS {label}: real DOS INT16 F10/F9/F1/F5 and F11=8500/F12=8600, "
                  "legacy/enhanced peek+read+empty checks, reserved F6/F7/F8, "
                  "shell keyboard and native files preserved",flush=True)


if __name__=="__main__":
    main()
