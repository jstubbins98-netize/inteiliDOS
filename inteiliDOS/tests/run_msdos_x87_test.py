#!/usr/bin/env python3
"""Run an original x87 DOS program in the native kernel on a no-FPU host.
Uses separately supplied DOS disks and disposable native/DOS storage only.
"""
from pathlib import Path
import argparse
import tempfile
import time
from build_test_kernel import ROOT,build,run
from run_launchpad_dos_test import Guest,fat12_image
from run_msdos622_test import prepare,shell
from run_msdos_external_media_test import native_disk,protected
from run_msdos_video_test import hello_glyph

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel",type=Path,nargs="?")
    parser.add_argument("--media",type=Path,default=ROOT.parent/"MS-DOS_disks")
    options=parser.parse_args()
    out=ROOT.parent/"generated-artifacts/msdos-x87"
    out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="inteilidos-x87-dos-") as folder:
        tmp=Path(folder)
        kernel=options.kernel.resolve() if options.kernel else build(out/"legacy")
        prepare(options.media.resolve(),tmp/"disc")
        run("nasm","-f","bin","-DDOS=1",ROOT/"tests/msdos_x87.asm","-o",tmp/"X87TEST.COM")
        (tmp/"disc/X87.IMG").write_bytes(fat12_image({"X87TEST.COM":(tmp/"X87TEST.COM").read_bytes()}))
        run("xorriso","-as","mkisofs","-quiet","-iso-level","1","-o",tmp/"dos.iso",tmp/"disc")
        native=tmp/"native.img";native_disk(native);safe=protected(native)
        guest=Guest(kernel,tmp/"dos.iso",False,tmp,("-m","64","-cpu","486,-fpu",
                    "-drive",f"file={native},if=ide,index=1,format=raw"))
        def prompt(timeout=60):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                if guest.screen().rstrip().endswith("C:\\>"): return
                if "MS-DOS stopped:" in guest.screen():
                    raise AssertionError(guest.screen())
                time.sleep(.1)
            raise AssertionError("Missing DOS prompt:\n"+guest.screen())
        try:
            guest.wait("Welcome to inteiliDOS");guest.wait("> ")
            shell(guest,"hello")
            font=hello_glyph(guest,out/"font-before.ppm")
            shell(guest,"ms-dos /image X87.IMG")
            guest.wait("MS-DOS 6.22 -",timeout=120)
            shell(guest,"copy a:\\x87test.com c:\\x87test.com");prompt()
            shell(guest,"c:\\x87test")
            guest.wait("X87 ARITHMETIC STACK EXCEPTIONS STATE PASS",timeout=90)
            prompt()
            guest.command("screendump",{"filename":str(out/"dos-x87-pass.ppm")})
            guest.key("f8");guest.wait("Returned from MS-DOS")
            shell(guest,"hello")
            assert hello_glyph(guest,out/"font-after.ppm")==font,"Native text font changed"
            # Re-entry initializes a fresh CPU/FPU, while installed DOS files persist.
            shell(guest,"ms-dos");guest.wait("MS-DOS 6.22 -",timeout=120)
            shell(guest,"c:\\x87test")
            guest.wait("X87 ARITHMETIC STACK EXCEPTIONS STATE PASS",timeout=90)
            prompt();guest.key("f8");guest.wait("Returned from MS-DOS")
            assert protected(native)==safe,"Native files changed"
            print("PASS: real DOS x87 quadratic/arithmetic/stack/exceptions/state on a 486 host with FPU disabled; F8/font restoration, re-entry, persistent C:, native-file preservation")
        finally:guest.close()

if __name__=="__main__":main()
