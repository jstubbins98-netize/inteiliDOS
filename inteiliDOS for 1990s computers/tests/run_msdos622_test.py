#!/usr/bin/env python3
"""Real DOS boot/install/write/re-entry/reboot/F8 test in the complete OS.
Requires the user's four MS-DOS disk images; never downloads DOS.
All physical/virtual test disks and screenshots are disposable /tmp fixtures.
Usage: run_msdos622_test.py [kernel] [DOS disk directory] [--installed]
The installed variant protects sentinel VBR/kernel/config sectors in a native
partition starting at LBA 4096; default mode preserves the legacy flat fixture.
"""
from pathlib import Path
import struct
import hashlib
import sys
import tempfile
import time

from build_test_kernel import build, run, ROOT
from run_launchpad_dos_test import Guest, fat12_image
sys.path.insert(0,str(ROOT/"cmake"))
from prepare_msdos622 import prepare

def type_text(guest, text):
    punctuation={" ":"spc", "\n":"ret", "-":"minus", ".":"dot",
                 "\\":"backslash", "/":"slash", ":":"semicolon",
                 ">":"dot", "<":"comma", "?":"slash", "=":"equal"}
    for c in text:
        key=punctuation.get(c,c.lower())
        codes=[]
        if c.isupper() or c in ":><?":
            codes.append({"type":"qcode","data":"shift"})
        codes.append({"type":"qcode","data":key})
        guest.command("send-key",{"keys":codes,"hold-time":30})
        time.sleep(0.07)

def shell(guest, text):
    type_text(guest,text+"\n")

def clear_dos(guest):
    shell(guest,"cls")
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        if "MS-DOS 6.22 -" not in guest.screen() and "C:\\>" in guest.screen():
            return
        time.sleep(.1)
    raise AssertionError("DOS CLS did not complete:\n"+guest.screen())

def main():
    with tempfile.TemporaryDirectory(prefix="inteilidos-msdos622-") as folder:
        tmp=Path(folder)
        kernel=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(tmp/"build")
        media=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else ROOT.parent/"attached_assets"
        prepare(media,tmp/"disc")
        run("nasm","-f","bin",ROOT/"tests/msdos622_fixture.asm","-o",tmp/"pc.com")
        (tmp/"game.img").write_bytes(fat12_image({"PC.COM":(tmp/"pc.com").read_bytes(),
                                                 "FLOPPY.TXT":b"REAL-FLOPPY"}))
        (tmp/"disc"/"GAME.IMG").write_bytes((tmp/"game.img").read_bytes())
        run("xorriso","-as","mkisofs","-quiet","-iso-level","1",
            "-o",tmp/"dos.iso",tmp/"disc")
        native=tmp/"native.img"
        with native.open("wb") as f: f.truncate(40*1024*1024)
        directory=bytearray(512)
        dir_lba,data_lba=2048,2049
        installed="--installed" in sys.argv[3:]
        if installed:
            part_start=4096;part_sectors=40*2048-part_start
            dir_lba=part_start+1033;data_lba=dir_lba+1
            mbr=bytearray(512);mbr[510:]=b"\x55\xaa"
            mbr[446]=0x80;mbr[450]=0x99
            struct.pack_into("<II",mbr,454,part_start,part_sectors)
            fields=(0x314c4449,1,part_start,part_sectors,1024,8,1033)
            checksum=0x9e3779b9
            for value in fields:checksum^=value
            config=struct.pack("<8I",*fields,checksum)+bytes(480)
            with native.open("r+b") as f:
                f.write(mbr)
                f.seek(part_start*512);f.write(b"\xa5"*(1025*512))
                f.seek((part_start+1025)*512);f.write(config)
        struct.pack_into("<I",directory,0,0x46530001)
        struct.pack_into("<24sII",directory,4,b"NATIVE.TXT",data_lba,11)
        with native.open("r+b") as f:
            f.seek(dir_lba*512);f.write(directory)
            f.seek(data_lba*512);f.write(b"KEEP-NATIVE")
        with native.open("rb") as f:
            protected=hashlib.sha256(f.read(dir_lba*512)).digest()
        def start():
            g=Guest(kernel,tmp/"dos.iso",False,tmp,
                    ("-drive","file="+str(native)+",if=ide,index=1,format=raw",
                     "-drive","file="+str(tmp/"game.img")+",if=floppy,format=raw"))
            g.sock.settimeout(15)
            g.wait("Welcome to inteiliDOS");g.wait("> ")
            return g
        guest=start()
        try:
            shell(guest,"ms-dos")
            print(guest.wait("MS-DOS 6.22 -",timeout=150),flush=True)
            guest.wait("C:\\>")
            clear_dos(guest)
            shell(guest,"ver")
            guest.wait("MS-DOS Version 6.22")
            shell(guest,"md games")
            shell(guest,"copy a:\\command.com c:\\games\\tool.com")
            guest.wait("1 file")
            clear_dos(guest)
            shell(guest,"c:\\games\\tool.com /c ver")
            guest.wait("MS-DOS Version 6.22")
            shell(guest,"echo DOS-SAVED>c:\\games\\save.txt")
            time.sleep(1)
            guest.key("f8")
            guest.wait("Returned from MS-DOS")
            shell(guest,"ms-dos")
            guest.wait("MS-DOS 6.22 -")
            guest.wait("C:\\>")
            clear_dos(guest)
            shell(guest,"type c:\\games\\save.txt")
            guest.wait("DOS-SAVED")
            guest.key("f8")
            guest.wait("Returned from MS-DOS")
            print("PASS: real DOS 6.22, nested COM execution, install, writes, F8 and re-entry",flush=True)
        finally: guest.close()
        guest=start()
        try:
            shell(guest,"ms-dos")
            guest.wait("MS-DOS 6.22 -")
            guest.wait("C:\\>")
            clear_dos(guest)
            shell(guest,"type c:\\games\\save.txt")
            guest.wait("DOS-SAVED")
            clear_dos(guest)
            shell(guest,"c:\\games\\tool.com /c ver")
            guest.wait("MS-DOS Version 6.22")
            # DEBUG is a real MZ EXE copied from the installation disk.
            shell(guest,"debug")
            time.sleep(1)
            shell(guest,"r")
            guest.wait("AX=")
            shell(guest,"q")
            guest.wait("C:\\>")
            output=ROOT.parent/"generated-artifacts"/"msdos622"
            output.mkdir(parents=True,exist_ok=True)
            guest.command("screendump",{"filename":str(output/"dos.ppm")})
            # CLI infinite loop: F8 must work even with guest IF cleared.
            shell(guest,"debug")
            time.sleep(1)
            shell(guest,"a 100")
            shell(guest,"cli")
            shell(guest,"jmp 101")
            shell(guest,"")
            shell(guest,"g=100")
            time.sleep(1)
            guest.key("f8")
            guest.wait("Returned from MS-DOS")
            shell(guest,"help")
            guest.wait("MS-DOS")
            print("PASS: persistence after host reboot; MZ EXE; F8 through guest CLI; shell restored",flush=True)
            # Boot C again while mounting a game floppy image stored on CD.
            shell(guest,"ms-dos /image game.img")
            guest.wait("MS-DOS 6.22 -")
            shell(guest,"a:\\pc.com")
            guest.wait("CPU186 OK")
            guest.key("spc")
            time.sleep(1)
            guest.command("screendump",{"filename":str(output/"cga.ppm")})
            guest.key("spc")
            guest.wait("CGA AND SPEAKER OK")
            guest.key("f8")
            guest.wait("Returned from MS-DOS")
            shell(guest,"ms-dos")
            guest.wait("MS-DOS 6.22 -")
            guest.key("f7")
            shell(guest,"type a:\\floppy.txt")
            guest.wait("REAL-FLOPPY")
            # Swapping back to Disk 2 must invalidate the cached A: root.
            guest.key("f6")
            # A full Disk 2 directory can scroll its volume label off screen
            # before the next capture. Check a Disk 2-only file instead.
            shell(guest,"dir a:\\unformat.com")
            guest.wait("UNFORMAT")
            guest.key("f8")
            guest.wait("Returned from MS-DOS")
            guest.command("screendump",{"filename":str(output/"shell.ppm")})
            print("PASS: 80186 execution, CGA, speaker, CD game image, physical floppy and media swap",flush=True)
        finally: guest.close()
        with native.open("rb") as f:
            assert hashlib.sha256(f.read(dir_lba*512)).digest()==protected, \
                "MBR, boot/kernel/configuration region was damaged"
            f.seek(data_lba*512)
            assert f.read(11)==b"KEEP-NATIVE", "Native file was damaged"
        print("PASS: original native file preserved",flush=True)
        # Refuse physical formatting and insufficient-space allocations.
        for formatted, expected in ((False,"No physical disk was formatted"),
                                    (True,"Could not create DOS HDD")):
            small=tmp/"small.img"
            with small.open("wb") as f: f.truncate(5*1024*1024)
            if formatted:
                with small.open("r+b") as f:
                    # Small-disk refusal remains a legacy fixture, even when
                    # the main persistence test uses an installed partition.
                    small_dir=bytearray(directory)
                    struct.pack_into("<I",small_dir,28,2049)
                    f.seek(2048*512);f.write(small_dir)
                    f.seek(2049*512);f.write(b"KEEP-NATIVE")
            before=hashlib.sha256(small.read_bytes()).digest()
            g=Guest(kernel,tmp/"dos.iso",False,tmp,
                    ("-drive","file="+str(small)+",if=ide,index=1,format=raw"))
            g.sock.settimeout(15)
            try:
                g.wait("Welcome to inteiliDOS");g.wait("> ")
                shell(g,"ms-dos")
                g.wait(expected)
            finally:g.close()
            assert hashlib.sha256(small.read_bytes()).digest()==before
        print("PASS: unformatted/small physical disks remain byte-for-byte unchanged",flush=True)

if __name__=="__main__":
    main()
