#!/usr/bin/env python3
"""Coexisting EHCI/xHCI storage, xHCI keyboard and real EHCI hotplug.
All media are disposable; independently verify FAT mirrors/persisted bytes.
"""
from pathlib import Path
import sys
import tempfile
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell
from run_usb_storage_test import fat_image, files_on_disk


def main():
    kernel=Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="usb-mixed-") as folder:
        t=Path(folder);x=t/"xhci.img";e=t/"ehci.img"
        source=fat_image(x,16,True);fat_image(e,32)
        args=("-cpu","core2duo","-m","256",
              "-device","qemu-xhci,id=x",
              "-device","ich9-usb-ehci1,id=e",
              "-device","usb-kbd,bus=x.0,port=2",
              "-drive",f"if=none,id=xd,file={x},format=raw",
              "-device","usb-storage,id=xs,drive=xd,bus=x.0,port=1",
              "-drive",f"if=none,id=ed,file={e},format=raw",
              "-device","usb-storage,id=es,drive=ed,bus=e.0,port=1")
        g=Guest(kernel,None,False,t,extra_args=args,qemu_binary="qemu-system-x86_64")
        try:
            g.wait("Welcome to inteiliDOS",45)
            shell(g,"usb /mount");g.wait("2 USB drive(s) mounted.")
            g.wait("USB controller: xHCI controller running")
            g.wait("EHCI controller: EHCI controller running")
            shell(g,"copy usb0:\\hello.txt usb1:\\mixed.txt");g.wait("Copied")
            shell(g,"cls")
            shell(g,"usb /mount");g.wait("2 USB drive(s) mounted.")
            shell(g,"type usb1:\\mixed.txt");g.wait("REAL-USB-FAT16-DATA")
            # No open files/deferred writes. Physical loss is NOT safe eject.
            g.command("device_del",{"id":"es"})
            shell(g,"cls")
            shell(g,"usb /eject");g.wait("Some drives could not be safely ejected")
            g.wait("USB1: Drive disconnected or failed; safe eject not confirmed")
            # Explicit mount retires the absent filesystem binding.
            shell(g,"cls");shell(g,"usb /mount");g.wait("1 USB drive(s) mounted.")
            # Legacy -drive backends are auto-deleted with their device.
            g.command("blockdev-add",{"node-name":"ed2","driver":"raw",
                                     "file":{"driver":"file","filename":str(e)}})
            g.command("device_add",{"driver":"usb-storage","id":"es2",
                                  "drive":"ed2","bus":"e.0","port":"1"})
            shell(g,"cls");shell(g,"usb /mount");g.wait("2 USB drive(s) mounted.")
            shell(g,"type usb1:\\mixed.txt");g.wait("REAL-USB-FAT16-DATA")
            shell(g,"usb /eject");g.wait("USB storage eject complete")
            shell(g,"cls")
            shell(g,"usb /mount");g.wait("2 USB drive(s) mounted.")
            shell(g,"copy usb1:\\hello.txt usb0:\\back.txt");g.wait("Copied")
            shell(g,"cls");shell(g,"usb /eject");g.wait("USB storage eject complete")
            shell(g,"help");g.wait("IntelliShell also understands plain English:")
            assert files_on_disk(e)["MIXED.TXT"]==source
            assert files_on_disk(x)["BACK.TXT"]==b"REAL-USB-FAT32-DATA\n"
            print("PASS: simultaneous EHCI/xHCI volumes, bidirectional copy, "
                  "stable remount, EHCI unplug/reinsert, honest failed eject, "
                  "persistent bytes/FAT mirrors and xHCI keyboard after eject")
        finally:
            print(g.screen(),flush=True);g.close()


if __name__=="__main__":
    main()
