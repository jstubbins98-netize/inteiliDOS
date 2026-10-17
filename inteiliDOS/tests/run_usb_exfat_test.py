#!/usr/bin/env python3
"""Disposable exFAT superfloppy/MBR/GPT + real xHCI/EHCI native smoke tests.
Requires mkfs.exfat and fsck.exfat. No physical device is opened.
Usage: python3 tests/run_usb_exfat_test.py /path/to/inteilidOS.elf [--ehci]
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import os
import uuid
import zlib
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell

ROOT=Path(__file__).resolve().parents[1]
def command(*args):
    subprocess.run([str(a) for a in args],check=True,
                   env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0"))

def wrap(raw,dest,gpt):
    prefix=2048;volsize=raw.stat().st_size//512
    total=prefix+volsize+2048
    with open(dest,"wb") as out:
        out.truncate(total*512);out.seek(prefix*512)
        with open(raw,"rb") as src:shutil.copyfileobj(src,out)
        mbr=bytearray(512);mbr[450]=0xEE if gpt else 7
        struct.pack_into("<II",mbr,454,1 if gpt else prefix,total-1 if gpt else volsize)
        mbr[510:512]=b"\x55\xaa";out.seek(0);out.write(mbr)
        if gpt:
            entries=bytearray(128*128)
            entries[:16]=uuid.UUID("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7").bytes_le
            entries[16:32]=uuid.UUID(int=17).bytes_le
            struct.pack_into("<QQQ",entries,32,prefix,prefix+volsize-1,0)
            for current,backup,table in ((1,total-1,2),(total-1,1,total-33)):
                h=bytearray(512)
                struct.pack_into("<8sIIIIQQQQ16sQIII",h,0,b"EFI PART",0x10000,92,0,0,
                    current,backup,34,total-34,uuid.UUID(int=18).bytes_le,
                    table,128,128,zlib.crc32(entries))
                struct.pack_into("<I",h,16,zlib.crc32(h[:92]))
                out.seek(table*512);out.write(entries);out.seek(current*512);out.write(h)
    with open(dest,"r+b") as out:
        out.seek(prefix*512);boot=bytearray(out.read(12*512))
        struct.pack_into("<Q",boot,64,prefix)
        checksum=0
        for i,b in enumerate(boot[:11*512]):
            if i not in (106,107,112):
                checksum=(((checksum>>1)|((checksum&1)<<31))+b)&0xFFFFFFFF
        boot[11*512:12*512]=struct.pack("<I",checksum)*128
        out.seek(prefix*512);out.write(boot);out.write(boot)

def check_volume(image,prefix,temp):
    volume=temp/"fsck-volume.img"
    with open(image,"rb") as src,open(volume,"wb") as dst:
        src.seek(prefix*512);boot=src.read(512)
        sectors=struct.unpack_from("<Q",boot,72)[0]
        dst.write(boot);remaining=sectors*512-512
        while remaining:
            block=src.read(min(1048576,remaining));assert block
            dst.write(block);remaining-=len(block)
    command("fsck.exfat","-n",volume)

def main():
    kernel=Path(sys.argv[1]).resolve();ehci="--ehci" in sys.argv[2:]
    for tool in ("mkfs.exfat","fsck.exfat"):
        if not shutil.which(tool):raise SystemExit("Install exfatprogs for "+tool)
    captures=ROOT.parent/"generated-artifacts/usb-exfat";captures.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="usb-exfat-") as folder:
        t=Path(folder);host=t/"exfat-test"
        command("gcc","-std=c11","-O1","-g","-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",ROOT/"tests/usb_exfat_test.c",ROOT/"kernel/usb_storage.c",
            ROOT/"kernel/fatfs/ff.c",ROOT/"kernel/fatfs/ffunicode.c","-o",host)
        raw=t/"raw.img"
        with open(raw,"wb") as f:f.truncate(64*1024*1024)
        command("mkfs.exfat","-L","USBTEST",raw)
        command(host,"create",raw);command(host,"verify",raw);check_volume(raw,0,t)
        for kind in ("mbr","gpt"):
            disk=t/(kind+".img");wrap(raw,disk,kind=="gpt")
            command(host,"verify",disk);check_volume(disk,2048,t)
        disk=t/"gpt.img"
        controllers=("-device","ich9-usb-ehci1,id=usb",
            "-device","ich9-usb-uhci1,masterbus=usb.0,firstport=0",
            "-device","ich9-usb-uhci2,masterbus=usb.0,firstport=2",
            "-device","ich9-usb-uhci3,masterbus=usb.0,firstport=4") if ehci else (
            "-device","qemu-xhci,id=usb")
        args=("-m","256",*controllers,"-device",
            "usb-kbd,bus=usb.0,port=2"+(",usb_version=1" if ehci else ""),
            "-drive",f"if=none,id=stick,file={disk},format=raw",
            "-device","usb-storage,drive=stick,bus=usb.0,port=1")
        g=Guest(kernel,None,False,t,extra_args=args,qemu_binary="qemu-system-x86_64")
        try:
            g.wait("Welcome to inteiliDOS");g.wait("> ")
            shell(g,"usb /show-ports");g.wait("No USB drives mounted")
            shell(g,"usb /mount");g.wait("1 USB drive(s) mounted")
            g.wait("Mounted (exFAT read/write)")
            shell(g,"usb /show-ports");g.wait("1 mounted USB drive(s)")
            g.command("screendump",{"filename":str(captures/("ports-ehci.ppm" if ehci else "ports-xhci.ppm"))})
            shell(g,"type USB0:\\HELLO.TXT");g.wait("REAL-USB-EXFAT-DATA")
            shell(g,"copy USB0:\\HELLO.TXT USB0:\\SAVED.TXT");g.wait("Copied")
            shell(g,"type USB0:\\SAVED.TXT");g.wait("REAL-USB-EXFAT-DATA")
            shell(g,"usb /eject");g.wait("Ejected; safe to remove")
            shell(g,"usb /show-ports");g.wait("No USB drives mounted")
            # This command's response also proves that the USB keyboard
            # still operates after storage eject.
        finally:g.close()
        command(host,"verify",disk);check_volume(disk,2048,t)
        print("PASS: native GPT exFAT mount/list/read/save/eject and USB keyboard preservation",
              "EHCI" if ehci else "xHCI")

if __name__=="__main__":main()
