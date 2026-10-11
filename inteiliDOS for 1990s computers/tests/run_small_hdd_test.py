#!/usr/bin/env python3
"""Clean reinstall/boot and logical C: persistence on every IDE connector.

QEMU advertises LBA, so pair this test with ata_chs_test.c to verify CHS
task-file behavior. Covers sole slave, primary-master CD-ROM, secondary
connectors and multiple disks. Only temporary disks are overwritten.
"""
import hashlib
import struct
import sys
import tempfile
from pathlib import Path
from run_launchpad_dos_test import Guest, fat12_image
from run_hdd_cold_boot_test import shell, save_basic, load_basic
from run_msdos622_test import type_text


def checksum(path):
    return hashlib.sha256(path.read_bytes()).digest()


def persistence(folder, args, name, value, protected=()):
    """Two distinct BIOS boots; no kernel loader or setup media."""
    before=[checksum(p) for p in protected]
    boot=("-boot","order=c,strict=on")+args
    guest=Guest(None,None,False,folder,boot)
    guest.sock.settimeout(15)
    try:
        guest.wait("Welcome to inteiliDOS",timeout=60);guest.wait("> ")
        save_basic(guest,name,value)
        load_basic(guest,name,value)
    finally:
        guest.close()
    guest=Guest(None,None,False,folder,boot)
    guest.sock.settimeout(15)
    try:
        guest.wait("Welcome to inteiliDOS",timeout=60);guest.wait("> ")
        load_basic(guest,name,value)
        save_basic(guest,"afterboot",value+1)
        load_basic(guest,"afterboot",value+1)
    finally:
        guest.close()
    assert [checksum(p) for p in protected]==before,"Unrelated disk modified"


def native_apps(folder, args):
    """Editor and spreadsheet UI save/load across an HDD-only cold reboot."""
    boot=("-boot","order=c,strict=on")+args
    guest=Guest(None,None,False,folder,boot)
    try:
        guest.wait("Welcome to inteiliDOS",timeout=60);guest.wait("> ")
        shell(guest,"iedit");guest.wait("iEdit")
        type_text(guest,"editor slave\nsave")
        guest.wait("Save as");shell(guest,"editor")
        guest.wait("Save to:");guest.key("1");guest.wait("Saved: C:\\editor.txt")
        type_text(guest,"quit");guest.wait("> ")
        shell(guest,"sheets");guest.wait("Ready")
        shell(guest,"9876")
        type_text(guest,":save\n");guest.wait("Save as");shell(guest,"sheet")
        guest.wait("Save to:");guest.key("1");guest.wait("Saved: C:\\sheet.csv")
        guest.key("ret");guest.key("q");guest.wait("> ")
    finally:
        guest.close()
    guest=Guest(None,None,False,folder,boot)
    try:
        guest.wait("Welcome to inteiliDOS",timeout=60);guest.wait("> ")
        shell(guest,"iedit");guest.wait("iEdit")
        type_text(guest,"load")
        guest.wait("Y or Ctrl+O to discard");guest.key("y")
        guest.wait("Open (.txt):");shell(guest,"editor")
        guest.wait("Load from:");guest.key("1");guest.wait("Opened: C:\\editor.txt")
        guest.wait("editor slave")
        guest.key("down");type_text(guest,"quit");guest.wait("> ")
        shell(guest,"sheets");guest.wait("Ready")
        type_text(guest,":load\n");guest.wait("Open (.csv):");shell(guest,"sheet")
        guest.wait("Load from:");guest.key("1");guest.wait("Opened: C:\\sheet.csv")
        guest.key("ret");guest.wait("9876");guest.key("q");guest.wait("> ")
    finally:
        guest.close()
    print("PASS: editor and spreadsheet C: UI saves/loads across slave HDD cold boot")


def main():
    kernel=Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="small-ide-") as temp:
        folder=Path(temp)
        disk=folder/"hdd.img"
        with disk.open("wb") as f:
            f.truncate(10*1024*1024)
            mbr=bytearray(512)
            mbr[510:]=b"\x55\xaa"
            struct.pack_into("<B3sB3sII",mbr,446,0x80,b"\xfe\xff\xff",
                             0x99,b"\xfe\xff\xff",2048,20480-2048)
            f.write(mbr)
            f.seek(2048*512)
            f.write(struct.pack("<I",0x46530001))
        floppy=folder/"empty.img"
        floppy.write_bytes(fat12_image({}))
        args=("-drive",f"file={disk},if=ide,index=1,format=raw")
        guest=Guest(kernel,floppy,True,folder,args)
        guest.sock.settimeout(15)
        try:
            guest.wait("Welcome to inteiliDOS");guest.wait("> ")
            shell(guest,"setup");guest.wait("Setup Wizard");guest.key("ret")
            guest.wait("Choose an installation mode");guest.key("down");guest.key("ret")
            guest.wait("Select the target installation drive")
            guest.wait("[10 MB]");guest.key("ret")
            guest.wait("Clean install")
            guest.wait("Press ENTER to begin installation");guest.key("ret")
            guest.wait("Installation successful",timeout=180)
            output=Path(__file__).resolve().parents[2]/"generated-artifacts"/"setup"
            output.mkdir(parents=True,exist_ok=True)
            guest.command("screendump",{"filename":str(output/"small-hdd.ppm")})
        finally:
            guest.close()
        installed=disk.read_bytes()
        prefix=installed[:(2048+1033)*512]
        persistence(folder,args,"slave",1010)
        native_apps(folder,args)
        assert disk.read_bytes()[:len(prefix)]==prefix
        print("PASS: 10MiB sole IDE slave clean reinstall, C: saves and BIOS cold-boot reads")
        # Empty ATAPI tray is still a primary-master device, not a native HDD.
        disk.write_bytes(installed)
        cd=("-drive","if=ide,index=0,media=cdrom")+args
        persistence(folder,cd,"cdslave",2020)
        print("PASS: primary-master CD-ROM plus slave HDD; persistent C:")
        for slot in (0,2,3):
            disk.write_bytes(installed)
            moved=("-drive",f"file={disk},if=ide,index={slot},format=raw")
            persistence(folder,moved,f"slot{slot}",3030+slot)
            assert disk.read_bytes()[:len(prefix)]==prefix
            print(f"PASS: IDE slot {slot} native C: persists through BIOS cold boot")
        # A safe flat filesystem on a lower connector must not steal C: from
        # the installed disk. Its full image must remain byte-for-byte intact.
        flat=folder/"legacy.img"
        flat_data=bytearray(10*1024*1024)
        struct.pack_into("<I",flat_data,2048*512,0x46530001)
        struct.pack_into("<24sII",flat_data,2048*512+4,b"KEEP.TXT",2049,4)
        flat_data[2049*512:2049*512+4]=b"KEEP"
        flat.write_bytes(flat_data)
        foreign=folder/"foreign.img"
        foreign_data=bytearray(b"\x5a"*(10*1024*1024))
        foreign_data[:512]=bytes(512)
        foreign_data[510:512]=b"\x55\xaa"
        struct.pack_into("<B3sB3sII",foreign_data,446,0,b"\xfe\xff\xff",
                         0x06,b"\xfe\xff\xff",2048,20480-2048)
        foreign.write_bytes(foreign_data)
        disk.write_bytes(installed)
        # Explicit BIOS boot priority: the lower-slot legacy data disk has no
        # boot code. This is normal firmware selection, not a kernel loader.
        multi=("-drive",f"file={flat},if=ide,index=0,format=raw",
               "-drive",f"file={disk},if=none,id=native,format=raw",
               "-device","ide-hd,drive=native,bus=ide.0,unit=1,bootindex=1",
               "-drive",f"file={foreign},if=ide,index=2,format=raw")
        persistence(folder,multi,"multi",4040,(flat,foreign))
        assert disk.read_bytes()[:len(prefix)]==prefix
        print("PASS: multiple disks; installed slave wins C:, legacy and foreign disks unchanged")


if __name__=="__main__":
    main()
