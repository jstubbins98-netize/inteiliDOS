#!/usr/bin/env python3
"""Real DOS + PS/2 or native USB boot mouse through QMP.
--usb: xHCI; --ehci: ICH9 EHCI + UHCI companions; --uhci: standalone
PIIX3 UHCI input + EHCI storage. All USB modes disable the 8042.
--mouse-first reverses the two UHCI input ports to catch enumeration resets.
--ehci --split-companions uses separate UHCI controllers for keyboard/mouse.
Writes disposable disks only. Use a modern kernel for USB modes.
"""
from pathlib import Path
import sys
import tempfile
import time
import struct
import subprocess
from build_test_kernel import ROOT, build, run
from run_launchpad_dos_test import Guest, fat12_image
from run_msdos622_test import prepare, shell, clear_dos
from run_msdos_external_media_test import native_disk, protected
from run_msdos_video_test import ppm, hello_glyph
from run_msdos_usb_test import add_files
from run_usb_storage_test import fat_image, files_on_disk


def mouse(g, *events):
    g.command("input-send-event", {"events": list(events)})
    time.sleep(.3)


def main():
    modes=[m for m in ("usb","ehci","uhci") if "--"+m in sys.argv]
    if len(modes)>1:
        raise SystemExit("Choose just one of --usb, --ehci, --uhci")
    mode=modes[0] if modes else "ps2"
    usb=mode!="ps2";older=mode in ("ehci","uhci")
    output=ROOT.parent/("generated-artifacts/msdos-mouse-"+mode+
                       ("-mouse-first" if "--mouse-first" in sys.argv else "")+
                       ("-split" if "--split-companions" in sys.argv else ""))
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="msdos-mouse-") as folder:
        tmp=Path(folder)
        positional=[s for s in sys.argv[1:] if not s.startswith("--")]
        kernel=Path(positional[0]).resolve() if positional else build(tmp/"build",modern=usb)
        media=Path(positional[1]).resolve() if len(positional)>1 else ROOT.parent/"MS-DOS_disks"
        prepare(media,tmp/"disc")
        run("nasm","-f","bin",ROOT/"tests/msdos_mouse.asm","-o",tmp/"mouse.com")
        (tmp/"disc/MOUSE.IMG").write_bytes(fat12_image({
            "MOUSE.COM":(tmp/"mouse.com").read_bytes()}))
        run("xorriso","-as","mkisofs","-quiet","-iso-level","1",
            "-o",tmp/"mouse.iso",tmp/"disc")
        native=tmp/"native.img";native_disk(native);safe=protected(native)
        args=["-drive",f"file={native},if=ide,index=1,format=raw"]
        if usb:
            stick=tmp/"usb.img";fat_image(stick,16)
            add_files(stick,{"MOUSE.IMG":(tmp/"disc/MOUSE.IMG").read_bytes()})
            usb_original=stick.read_bytes()
            if mode=="ehci":
                # Both HID devices share the first companion's two ports;
                # high-speed BOT stays on EHCI root port 3.
                controllers=["-device","ich9-usb-ehci1,id=usb",
                             "-device","ich9-usb-uhci1,masterbus=usb.0,firstport=0"]
                if "--split-companions" in sys.argv:
                    controllers+=["-device","ich9-usb-uhci2,masterbus=usb.0,firstport=2"]
                    kport,mport="1","4";storage_port="5"
                else:
                    kport,mport=("2","1") if "--mouse-first" in sys.argv else ("1","2")
                    storage_port="3"
                input_bus="usb.0"
            elif mode=="uhci":
                controllers=["-device","piix3-usb-uhci,id=input",
                             "-device","ich9-usb-ehci1,id=usb"]
                kport,mport=("2","1") if "--mouse-first" in sys.argv else ("1","2")
                input_bus="input.0";storage_port="1"
            else:
                controllers=["-device","qemu-xhci,id=usb"]
                kport,mport="2","3";input_bus="usb.0";storage_port="1"
            # QEMU defaults these HID devices to USB 2.0 high speed on EHCI,
            # which would bypass the companion path being tested.
            hid_version=",usb_version=1" if older else ""
            mouse_spec=f"usb-mouse,id=mouse,bus={input_bus},port={mport}{hid_version}"
            args+=["-cpu","core2duo","-m","256",*controllers,
                   "-device",f"usb-kbd,bus={input_bus},port={kport}{hid_version}",
                   "-device",mouse_spec,
                   "-machine","pc,i8042=off",
                   "-drive",f"if=none,id=stick,file={stick},format=raw",
                   "-device",f"usb-storage,drive=stick,bus=usb.0,port={storage_port}"]
        g=Guest(kernel,tmp/"mouse.iso",False,tmp,tuple(args))
        try:
            g.wait("Welcome to inteiliDOS");g.wait("> ")
            shell(g,"hello");g.wait("Hello!")
            font=hello_glyph(g,output/"before.ppm")
            shell(g,"ms-dos")
            g.wait("MS-DOS 6.22 -",timeout=180);g.wait("C:\\>")
            g.key("f8");g.wait("Returned from MS-DOS")
            if usb:
                shell(g,"usb /mount");g.wait("USB0: Mounted")
                shell(g,"cls")
                shell(g,"usb /mount");g.wait("USB0: Mounted")
            # /MOUSE is independent of media selection, case and order.
            commands=("ms-dos /mOuSe /image MOUSE.IMG",
                      "ms-dos /USB USB0:\\MOUSE.IMG /MOUSE" if usb else
                      "ms-dos /image MOUSE.IMG /MOUSE")
            for index,command in enumerate(commands):
                shell(g,command)
                g.wait("MS-DOS 6.22 -",timeout=90);g.wait("C:\\>")
                (output/"startup.txt").write_text(g.screen())
                clear_dos(g);shell(g,"a:\\mouse.com")
                g.wait("MOUSE READY - MOVE RIGHT")
                mouse(g,{"type":"rel","data":{"axis":"x","value":32}})
                for label,button in (("LEFT","left"),("RIGHT","right"),("MIDDLE","middle")):
                    g.wait("CLICK "+label)
                    mouse(g,{"type":"btn","data":{"down":True,"button":button}})
                    mouse(g,{"type":"btn","data":{"down":False,"button":button}})
                g.wait("DOS MOUSE OK - ENTER FOR VGA")
                assert "DOS MOUSE FAILED" not in g.screen()
                g.command("screendump",{"filename":str(output/"text.ppm")})
                g.key("ret");time.sleep(1)
                g.command("screendump",{"filename":str(output/"vga.ppm")})
                width,height,pixels=ppm(output/"vga.ppm")
                background=pixels[:3]
                assert pixels==background*(width*height), "Pointer was visible before F12"
                g.key("f12");time.sleep(.1)
                g.command("screendump",{"filename":str(output/"vga-visible.ppm")})
                width,height,pixels=ppm(output/"vga-visible.ppm")
                assert any(pixels[(y*width+x)*3:(y*width+x)*3+3]!=background
                           for y in range(height//2,height//2+20)
                           for x in range(width//2,width//2+20)), "No visible VGA mouse pointer"
                g.key("f12");time.sleep(.1)
                g.command("screendump",{"filename":str(output/"vga-hidden.ppm")})
                width,height,pixels=ppm(output/"vga-hidden.ppm")
                assert pixels==background*(width*height), "Second F12 did not hide pointer"
                assert "DOS MOUSE FAILED" not in g.screen()
                mouse(g,{"type":"rel","data":{"axis":"x","value":20}})
                g.key("f8");g.wait("Returned from MS-DOS")
                shell(g,"hello");g.wait("Hello!")
                assert hello_glyph(g,output/"after.ppm")==font, "Native font not restored"
                if usb:
                    shell(g,"usb /mount");g.wait("USB0: Mounted")
                    if older:
                        shell(g,f"copy usb0:\\hello.txt usb0:\\after{index}.txt")
                        g.wait("Copied")
                        shell(g,"usb /eject");g.wait("USB storage eject complete")
                        shell(g,"cls")
                        shell(g,"usb /mount");g.wait("USB0: Mounted")
                        shell(g,f"type usb0:\\after{index}.txt")
                        g.wait("REAL-USB-FAT16-DATA")
            # Plain launch must not retain the prior resident driver.
            shell(g,"ms-dos /image MOUSE.IMG")
            g.wait("MS-DOS 6.22 -",timeout=90);g.wait("C:\\>")
            clear_dos(g);shell(g,"a:\\mouse.com")
            g.wait("DOS MOUSE FAILED")  # AX=0 must not report a mouse
            g.key("f8");g.wait("Returned from MS-DOS")
            shell(g,"hello");g.wait("Hello!")
        except Exception:
            symbols=subprocess.check_output(["nm","-n",str(kernel)],text=True).splitlines()
            address=int(next(s.split()[0] for s in symbols if s.endswith(" b m")),16)
            g.command("pmemsave",{"val":address,"size":256,"filename":str(output/"state.bin")})
            mem=int(next(s.split()[0] for s in symbols if s.endswith(" b mem")),16)
            g.command("pmemsave",{"val":mem,"size":4,"filename":str(output/"pointer.bin")})
            guest=struct.unpack("<I",(output/"pointer.bin").read_bytes())[0]
            g.command("pmemsave",{"val":guest,"size":0x110000,
                                 "filename":str(output/"guest.bin")})
            raise
        finally:
            g.close()
        if usb:
            # With no mouse or PS/2 controller, failure is bounded and explicit.
            # USB keyboard and F8 must keep working.
            missing=tmp/"no-ps2";missing.mkdir()
            # Remove the -device switch immediately preceding that mouse.
            mouse_index=args.index(mouse_spec)
            no_mouse=args[:mouse_index-1]+args[mouse_index+1:]
            g=Guest(kernel,tmp/"mouse.iso",False,missing,tuple(no_mouse))
            try:
                g.wait("Welcome to inteiliDOS");g.wait("> ")
                shell(g,"ms-dos /MOUSE /IMAGE MOUSE.IMG")
                g.wait("MS-DOS 6.22 -",timeout=90);g.wait("C:\\>")
                clear_dos(g);shell(g,"a:\\mouse.com")
                g.wait("DOS MOUSE FAILED")
                g.key("f8");g.wait("Returned from MS-DOS")
                # Startup diagnostics live in the saved native screen, not
                # in the DOS guest's video memory. F8 makes them visible again.
                g.wait("DOS mouse unavailable")
                shell(g,"hello");g.wait("Hello!")
            finally:
                g.close()
        assert protected(native)==safe
        if usb:
            if older:
                files=files_on_disk(stick)
                assert files["MOUSE.IMG"]==(tmp/"disc/MOUSE.IMG").read_bytes()
                for index in range(2):
                    assert files[f"AFTER{index}.TXT"]==b"REAL-USB-FAT16-DATA\n"
            else:
                assert stick.read_bytes()==usb_original, "USB game source was modified"
        print("PASS: DOS INT33, far chaining, "+mode+" movement/3 buttons, "
              "callbacks, VGA/F8, re-entry, opt-in isolation and native disk preserved")


if __name__=="__main__":
    main()
