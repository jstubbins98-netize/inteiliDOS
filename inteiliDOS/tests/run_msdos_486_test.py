#!/usr/bin/env python3
"""486 protected/paged execution under real DOS; disposable disks only."""
from pathlib import Path
import argparse
import tempfile
import time
import zipfile
from build_test_kernel import ROOT, build, run
from run_launchpad_dos_test import Guest, fat12_image
from run_msdos622_test import prepare, shell
from run_msdos_external_media_test import native_disk, protected
from run_msdos_video_test import ppm, hello_glyph

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel",nargs="?",type=Path)
    parser.add_argument("media",nargs="?",type=Path,default=ROOT.parent/"MS-DOS_disks")
    parser.add_argument("game",nargs="?",type=Path)
    parser.add_argument("--ps2",action="store_true",help="Use a legacy PS/2 keyboard kernel profile")
    options=parser.parse_args()
    out=ROOT.parent/"generated-artifacts/msdos-486"
    if options.ps2: out=out/"ps2"
    out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="inteilidos-486-") as folder:
        tmp=Path(folder)
        kernel=options.kernel.resolve() if options.kernel else build(out/"build",modern=not options.ps2)
        media=options.media.resolve()
        prepare(media,tmp/"disc")
        game=options.game.resolve() if options.game else None
        if game:
            destination=tmp/"disc/GAME";destination.mkdir()
            with zipfile.ZipFile(game) as archive:
                for name in ("D386MYH.EXE","CWSDPR0.EXE","DOOM1.WAD"):
                    (destination/name).write_bytes(archive.read(name))
        run("nasm","-f","bin","-DDOS=1",ROOT/"tests/msdos_486.asm","-o",tmp/"cpu.com")
        (tmp/"disc/CPU486.IMG").write_bytes(fat12_image({"CPU486.COM":(tmp/"cpu.com").read_bytes()}))
        run("xorriso","-as","mkisofs","-quiet","-iso-level","1",
            "-o",tmp/"dos.iso",tmp/"disc")
        native=tmp/"native.img";native_disk(native);safe=protected(native)
        # Default to USB for modern profiles; --ps2 exercises a native 486
        # profile without diverting QEMU keyboard events to an unused USB HID.
        args=("-m","64","-cpu","486" if options.ps2 else "core2duo",
              "-drive",f"file={native},if=ide,index=1,format=raw")
        if not options.ps2:
            args+=("-device","qemu-xhci,id=xhci","-device","usb-kbd,bus=xhci.0")
        guest=Guest(kernel,tmp/"dos.iso",False,tmp,args)
        def rendered(timeout=120,previous=None):
            """A BIOS mode byte alone does not prove that Doom drew a frame."""
            deadline=time.monotonic()+timeout
            path=out/"doom386.ppm"
            while time.monotonic()<deadline:
                guest.command("screendump",{"filename":str(path)})
                w,h,pixels=ppm(path)
                colors=set(zip(pixels[::3],pixels[1::3],pixels[2::3]))
                visible=sum(bool(r|g|b) for r,g,b in
                            zip(pixels[::3],pixels[1::3],pixels[2::3]))
                if len(colors)>48 and visible>w*h//8 and pixels!=previous:
                    return pixels
                if "MS-DOS stopped:" in guest.screen():
                    raise AssertionError("DOS-extender game stopped:\n"+guest.screen())
                time.sleep(.5)
            raise AssertionError("No rendered Doom frame; see "+str(path)+"\n"+guest.screen())
        def prompt(expected="C:\\>",timeout=60):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                if guest.screen().rstrip().endswith(expected):return
                time.sleep(.1)
            raise AssertionError("Missing completed DOS prompt:\n"+guest.screen())
        try:
            guest.wait("Welcome to inteiliDOS");guest.wait("> ")
            shell(guest,"hello")
            original_font=hello_glyph(guest,out/"font-before.ppm")
            shell(guest,"ms-dos")
            guest.wait("MS-DOS 6.22 -",timeout=120)
            shell(guest,"cls")
            shell(guest,"echo BEFORE486>C:\\CHECK.TXT")
            # Select explicit game media on re-entry, also exercising C:.
            guest.key("f8");guest.wait("Returned from MS-DOS")
            shell(guest,"ms-dos /image CPU486.IMG")
            guest.wait("MS-DOS 6.22 -",timeout=120)
            shell(guest,"a:\\cpu486")
            guest.wait("486 PROTECTED PAGING PASS",timeout=60)
            shell(guest,"type c:\\check.txt");guest.wait("BEFORE486")
            guest.command("screendump",{"filename":str(out/"protected-paging.ppm")})
            guest.key("f8");guest.wait("Returned from MS-DOS")
            shell(guest,"ms-dos /cdrom" if game else "ms-dos")
            guest.wait("MS-DOS 6.22 -",timeout=120)
            shell(guest,"type c:\\check.txt");guest.wait("BEFORE486")
            if game:
                # Install onto writable C:; extenders/game startup may create
                # swap/configuration files and cannot do that on the CD.
                shell(guest,"md c:\\doom386")
                prompt()
                for name in ("d386myh.exe","cwsdpr0.exe","doom1.wad"):
                    shell(guest,"copy d:\\game\\"+name+" c:\\doom386")
                    prompt()
                shell(guest,"cd \\doom386")
                prompt("C:\\DOOM386>")
                shell(guest,"d386myh -timedemo demo3")
                deadline=time.monotonic()+30
                mode=tmp/"mode.bin"
                while time.monotonic()<deadline:
                    guest.command("pmemsave",{"val":0x1000000+0x449,"size":1,
                                             "filename":str(mode)})
                    if mode.read_bytes()[0]==0x13:
                        first=rendered()
                        time.sleep(3) # let the opening screen wipe finish
                        rendered(previous=first)
                        break
                    if "MS-DOS stopped:" in guest.screen():
                        raise AssertionError("DOS-extender game stopped:\n"+guest.screen())
                    time.sleep(.3)
                else:
                    raise AssertionError("DOS-extender game did not enter VGA:\n"+guest.screen())
                # F10 is Doom8088's quit key: this must reach the game's
                # protected-mode keyboard handler, not just native polling.
                guest.key("f10")
                prompt("C:\\DOOM386>",timeout=60)
                shell(guest,"d386myh -timedemo demo3")
                first=rendered()
                time.sleep(3)
                rendered(previous=first)
            guest.key("f8");guest.wait("Returned from MS-DOS")
            shell(guest,"hello")
            assert hello_glyph(guest,out/"font-after.ppm")==original_font
            shell(guest,"ms-dos")
            guest.wait("MS-DOS 6.22 -",timeout=120)
            shell(guest,"type c:\\check.txt");guest.wait("BEFORE486")
            if game:
                shell(guest,"dir c:\\doom386\\d386myh.exe")
                guest.wait("D386MYH")
            guest.key("f8");guest.wait("Returned from MS-DOS")
        finally:
            if game:
                guest.command("pmemsave",{"val":0,"size":0x500000,
                                         "filename":str(out/"host-ram.bin")})
                guest.command("pmemsave",{"val":0x1000000,"size":16*1024*1024,
                                         "filename":str(out/"game-ram.bin")})
            print(guest.screen())
            guest.close()
        assert protected(native)==safe,"Native OS data changed"
    print("PASS: DOS boot, persistent C:, 486 protected/paged fixture, F8, re-entry, native-file preservation")
    if game: print("PASS: Doom8088/CWSDPR0 rendered changing frames, F10 input, restart, F8/font restoration and persistent files")

if __name__=="__main__":
    main()
