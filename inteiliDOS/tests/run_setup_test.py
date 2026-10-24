#!/usr/bin/env python3
"""Run the real setup UI on a disposable HDD and check written boot sectors."""
from pathlib import Path
import sys
import tempfile
import subprocess
import hashlib
import struct
from run_launchpad_dos_test import Guest, fat12_image

def main():
    kernel=Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="inteilidos-setup-") as folder:
        tmp=Path(folder)
        media=tmp/"blank.img"
        media.write_bytes(fat12_image({}))
        disk=tmp/"install.img"
        with disk.open("wb") as f:f.truncate(64*1024*1024)
        if "--existing" in sys.argv[2:]:
            old=bytearray(512)
            old[510:]=b"\x55\xaa"
            struct.pack_into("<B3sB3sII",old,446,0x80,b"\xfe\xff\xff",
                             0x99,b"\xfe\xff\xff",2048,131072-2048)
            with disk.open("r+b") as f:
                f.write(old)
                f.seek(2048*512)
                f.write(struct.pack("<I",0x46530001))
        guest=Guest(kernel,media,True,tmp,
                    ("-drive",f"file={disk},if=ide,index=0,format=raw"))
        guest.sock.settimeout(15)
        try:
            guest.wait("Welcome to inteiliDOS");guest.wait("> ")
            guest.text("setup\n")
            guest.wait("Setup Wizard")
            welcome=guest.screen()
            assert "HP Vectra" not in welcome and "VEi8" not in welcome
            assert "supported BIOS computers" in welcome
            out=Path(__file__).resolve().parents[2]/"generated-artifacts"/"setup"
            out.mkdir(parents=True,exist_ok=True)
            guest.command("screendump",{"filename":str(out/"welcome.ppm")})
            guest.key("ret")
            guest.wait("Choose an installation mode")
            guest.key("down");guest.key("ret")
            guest.wait("Select the target installation drive")
            guest.key("ret")
            guest.wait("Press ENTER to begin installation")
            guest.key("ret")
            guest.wait("Installation successful",timeout=180)
            complete=guest.screen()
            assert "HP Vectra" not in complete and "HP logo" not in complete
            out=Path(__file__).resolve().parents[2]/"generated-artifacts"/"setup"
            out.mkdir(parents=True,exist_ok=True)
            guest.command("screendump",{"filename":str(out/"installed.ppm")})
            guest.key("spc")
            guest.wait("> ")
            guest.text("basic\n");guest.wait("BYE to exit")
            guest.text("10");guest.key("spc");guest.text("print")
            guest.key("spc");guest.text("83\nsave\n");guest.wait("Filename (")
            guest.text("bootsafe\n");guest.wait("Save to:")
            guest.key("1");guest.wait("Saved")
            guest.text("bye\n");guest.wait("> ")
            guest.text("help\n");guest.wait("MS-DOS")
        finally:guest.close()
        with disk.open("rb") as f:
            mbr=f.read(512)
            assert mbr[510:]==b"\x55\xaa"
            entry=mbr[446:462];assert entry[0]==0x80
            lba=int.from_bytes(entry[8:12],"little")
            f.seek(lba*512);assert f.read(512)[510:]==b"\x55\xaa"
            # Read layout declared by the installer, not hardcoded FS LBAs.
            f.seek((lba+8193)*512)
            layout=struct.unpack("<8I",f.read(32))
            assert layout[:7]==(0x314c4449,2,lba,131072-lba,8192,8,8201)
            assert layout[7]==(0x9e3779b9 ^ layout[0] ^ layout[1] ^ layout[2] ^
                               layout[3] ^ layout[4] ^ layout[5] ^ layout[6])
            dir_lba=lba+layout[6]
            f.seek(dir_lba*512);directory=f.read(512)
            assert directory[:4]==struct.pack("<I",0x46530001)
            name,start,size=struct.unpack_from("<24sII",directory,4)
            assert name.rstrip(b"\0")==b"bootsafe.txt"
            assert start>dir_lba and size>0
            f.seek(start*512);assert f.read(size)==b"10 print 83\n"
            f.seek(0);protected=hashlib.sha256(f.read(dir_lba*512)).digest()
        # Exercise ordinary saves and the larger-than-current DOS allocation
        # through the actual fs.c on the exact disk produced by the UI.
        tool=tmp/"fs-layout"
        subprocess.run(["gcc","-std=gnu11","-Wall","-Wextra","-Werror",
                        str(Path(__file__).with_name("fs_layout_test.c")),
                        "-o",str(tool)],check=True)
        subprocess.run([str(tool),str(disk)],check=True)
        with disk.open("rb") as f:
            assert hashlib.sha256(f.read(dir_lba*512)).digest()==protected
        print("PASS: real setup + BASIC save + persistent DOS-image writes preserve MBR/VBR/kernel/config")

if __name__=="__main__":main()
