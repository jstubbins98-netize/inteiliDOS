#!/usr/bin/env python3
"""Execute large ELF programs from FAT16/FAT32 USB roots and directories."""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile
from run_usb_storage_test import fat_image
from run_msdos_usb_test import add_files, start
from run_msdos_external_media_test import native_disk
from run_msdos622_test import shell


def folder_entry(path):
    b=bytearray(path.read_bytes())
    prefix=struct.unpack_from("<I",b,454)[0] if b[450] in (6,12) else 0
    v=memoryview(b)[prefix*512:]
    reserved=struct.unpack_from("<H",v,14)[0]
    roots=struct.unpack_from("<H",v,17)[0]
    fat=struct.unpack_from("<H",v,22)[0] or struct.unpack_from("<I",v,36)[0]
    bits=16 if roots else 32
    root=(reserved+2*fat)*512
    data=reserved+2*fat+(roots*32+511)//512
    # Directory uses a free cluster below add_files' allocation range.
    offset=(data+30*v[13])*512
    v[offset:offset+32]=v[root+64:root+96]  # GOOD.ELF entry
    v[offset:offset+11]=b"NESTED  ELF"
    ent=root+128
    v[ent:ent+11]=b"PROGRAMS   ";v[ent+11]=0x10
    struct.pack_into("<H",v,ent+26,32)
    struct.pack_into("<H" if bits==16 else "<I",v,
                     reserved*512+32*(bits//8),0xffff if bits==16 else 0x0fffffff)
    v[(reserved+fat)*512:(reserved+2*fat)*512]=v[reserved*512:(reserved+fat)*512]
    path.write_bytes(b)


def main():
    kernel=Path(sys.argv[1]).resolve()
    output=Path("generated-artifacts/launchpad-battery").resolve()
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="launchpad-usb-") as name:
        t=Path(name)
        asm=t/"app.s"
        asm.write_text(".text\n.globl _start\n_start:\n"
                       "incl DONE\nret\n.data\n.globl DONE\nDONE: .long 0\n"
                       ".space 70000,0x55\n")
        subprocess.run(["as","--32",str(asm),"-o",str(t/"app.o")],check=True)
        elf=t/"app.elf"
        subprocess.run(["ld","-m","elf_i386","-Ttext=0x00a00000","-e","_start",
                        str(t/"app.o"),"-o",str(elf)],check=True)
        addr=int(next(line.split()[0] for line in
                      subprocess.check_output(["nm",str(elf)],text=True).splitlines()
                      if line.split()[-1]=="DONE"),16)
        assert elf.stat().st_size>65536
        disks=[t/"usb16.img",t/"usb32.img"]
        for path,bits in zip(disks,(16,32)):
            fat_image(path,bits,True)
            add_files(path,{"GOOD.ELF":elf.read_bytes(),"BAD.ELF":bytes(512)})
            folder_entry(path)
        original=[hashlib.sha256(p.read_bytes()).digest() for p in disks]
        native=t/"native.img";native_disk(native)
        g=start(kernel,native,t/"guest",disks)
        def execute(name="GOOD.ELF"):
            g.key("ret");g.wait("Press any key to launch");g.key("spc")
            g.wait(name) # restored LaunchPad
            flag=t/"flag"
            g.command("pmemsave",{"val":addr,"size":4,"filename":str(flag)})
            assert flag.read_bytes()==b"\x01\0\0\0"
        try:
            shell(g,"launchpad");g.key("u");g.wait("GOOD.ELF")
            # HELLO.TXT, LARGE.BIN, GOOD.ELF, BAD.ELF, PROGRAMS
            g.key("down");g.key("down");execute()
            g.key("down");g.key("ret");g.wait("Not an IPGM")
            g.key("spc")
            g.key("down");g.key("ret");g.wait("NESTED.ELF")
            execute("NESTED.ELF")
            g.command("screendump",{"filename":str(output/"launchpad-usb.ppm")})
            g.key("backspace");g.wait("GOOD.ELF")
            g.key("u");g.wait("USB1:");g.wait("GOOD.ELF")
            g.key("down");g.key("down");execute()
            g.key("esc");g.wait("C:\\>")
            shell(g,"health");g.wait("SYSTEM HEALTH")
            g.key("5");g.wait("Battery percentage:")
            g.wait("Battery health");g.wait("Unavailable")
            g.command("screendump",{"filename":str(output/"battery.ppm")})
            g.key("right");g.wait("Uptime:")
            g.key("left");g.wait("Battery percentage:")
            g.key("esc");g.wait("C:\\>")
            shell(g,"usb /eject");g.wait("safe to remove")
            shell(g,"hello");g.wait("Hello!")
        finally:g.close()
        assert original==[hashlib.sha256(p.read_bytes()).digest() for p in disks]
    print("PASS: FAT16/FAT32 USB ELF >64KB, nested folders, invalid program refusal, source switching, battery tab navigation, keyboard/eject and unchanged source files")

if __name__=="__main__":main()
