#!/usr/bin/env python3
"""Destructive USB formatting only on temporary QEMU image files."""
from pathlib import Path
import argparse
import subprocess
import tempfile
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("kernel",type=Path)
parser.add_argument("--ehci",action="store_true",help="EHCI storage with companion UHCI keyboard")
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
out=root/("generated-artifacts/usb-format-ehci" if args.ehci else "generated-artifacts/usb-format")
out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix="usb-format-qemu-") as folder:
    tmp=Path(folder)
    images=[tmp/"selected.img",tmp/"other.img"]
    for i,image in enumerate(images):
        with image.open("wb") as f:f.truncate(128*1024*1024)
        subprocess.run(["mformat","-i",str(image),"-h","32","-s","64","-t","128","::"],check=True)
        source=tmp/f"source{i}.txt";source.write_bytes(f"KEEP-DATA-{i}\r\n".encode())
        subprocess.run(["mcopy","-i",str(image),str(source),"::KEEP.TXT"],check=True)
    controllers=("-device","ich9-usb-ehci1,id=usb",
                 "-device","ich9-usb-uhci1,masterbus=usb.0,firstport=0",
                 "-device","ich9-usb-uhci2,masterbus=usb.0,firstport=2",
                 "-device","ich9-usb-uhci3,masterbus=usb.0,firstport=4") if args.ehci else (
                 "-device","qemu-xhci,id=usb")
    extra=("-cpu","core2duo","-m","256",*controllers,
           "-device","usb-kbd,bus=usb.0,port=2"+(",usb_version=1" if args.ehci else ""),
           "-drive",f"if=none,id=stick0,file={images[0]},format=raw",
           "-device","usb-storage,id=usbdrive0,drive=stick0,bus=usb.0,port=1",
           "-drive",f"if=none,id=stick1,file={images[1]},format=raw",
           "-device","usb-storage,id=usbdrive1,drive=stick1,bus=usb.0,port=3")
    g=Guest(args.kernel.resolve(),None,False,tmp,extra,qemu_binary="qemu-system-x86_64")
    try:
        g.wait("Welcome to inteiliDOS",45)
        shell(g,"usb /mount");g.wait("2 USB drive(s) mounted.")
        shell(g,"usb /format usb0: fat32");g.wait("Type exactly FORMAT USB0:")
        shell(g,"NO");g.wait("USB format cancelled.")
        shell(g,"type usb0:\\keep.txt");g.wait("KEEP-DATA-0")
        # Bare command exercises all three interactive decisions.
        shell(g,"usb /format");g.wait("Select USB0:..USB3:")
        shell(g,"USB0:");g.wait("Enter name or number")
        shell(g,"EXFAT");g.wait("Type exactly FORMAT USB0:")
        shell(g,"FORMAT USB0:")
        g.wait("USB0: formatted as exFAT and remounted read/write.",90)
        shell(g,"copy usb1:\\keep.txt usb0:\\after.txt")
        shell(g,"type usb0:\\after.txt");g.wait("KEEP-DATA-1")
        for filesystem in ("FAT32","FAT16"):
            shell(g,f"usb /format usb1: {filesystem}")
            g.wait("Type exactly FORMAT USB1:")
            shell(g,"FORMAT USB1:")
            g.wait(f"USB1: formatted as {filesystem} and remounted read/write.",90)
        shell(g,"cls")
        shell(g,"type usb0:\\after.txt");g.wait("KEEP-DATA-1")
        shell(g,"usb /show-ports");g.wait("Mounted (exFAT read/write)")
        g.wait("Mounted (FAT16 read/write)")
        g.command("screendump",{"filename":str(out/"format-complete.ppm")})
        shell(g,"usb /eject");g.wait("Keyboard devices were not ejected.")
        shell(g,"usb");g.wait("USB /FORMAT")
        print("PASS: native USB keyboard, two drives, cancelled format, interactive exFAT, "
              "FAT32/FAT16 selection, cross-drive preservation, copy after format and safe eject")
    except Exception:
        print(g.screen(),flush=True)
        g.command("screendump",{"filename":str(out/"failure.ppm")})
        raise
    finally:
        g.close()
