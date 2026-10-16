#!/usr/bin/env python3
"""Real xHCI or EHCI/BOT/FAT writes, eject/remount and keyboard isolation in QEMU.
Only disposable images are modified. No host FAT tools are required.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell, type_text


def fat_image(path, bits=16, partitioned=False):
    sectors=65536 if bits==16 else 131072
    prefix=2048 if partitioned else 0
    b=bytearray((sectors+prefix)*512)
    start=prefix*512
    v=memoryview(b)[start:]
    v[:3]=b"\xeb\x3c\x90";v[3:11]=b"USBTEST "
    struct.pack_into("<HBHBHHBHHHII",v,11,512,4 if bits==16 else 1,
                     1 if bits==16 else 32,2,512 if bits==16 else 0,
                     0,0xF8,128 if bits==16 else 0,63,255,prefix,sectors)
    v[510:512]=b"\x55\xaa"
    if bits==16:
        v[36:39]=bytes([0x80,0,0x29]);v[43:54]=b"USB16TEST  ";v[54:62]=b"FAT16   "
        reserved,fatsize,rootsector,cluster_bytes=1,128,257,2048
        data_sector=289
        struct.pack_into("<HHH",v,512,0xFFF8,0xFFFF,0xFFFF)
        v[(reserved+fatsize)*512:(reserved+2*fatsize)*512]=v[512:129*512]
        cluster=2
    else:
        struct.pack_into("<IHHIHH",v,36,1024,0,0,2,1,6)
        v[64:67]=bytes([0x80,0,0x29]);v[71:82]=b"USB32TEST  ";v[82:90]=b"FAT32   "
        reserved,fatsize,rootsector,cluster_bytes=32,1024,2080,512
        data_sector=2080;cluster=3
        struct.pack_into("<IIII",v,32*512,0x0FFFFFF8,0xFFFFFFFF,0x0FFFFFFF,0x0FFFFFFF)
        v[1056*512:2080*512]=v[32*512:1056*512]
        struct.pack_into("<I",v,512,0x41615252)
        struct.pack_into("<III",v,512+484,0x61417272,0xFFFFFFFF,0xFFFFFFFF)
        struct.pack_into("<I",v,512+508,0xAA550000)
        v[6*512:7*512]=v[:512];v[7*512:8*512]=v[512:1024]
    payload=f"REAL-USB-FAT{bits}-DATA\n".encode()
    off=rootsector*512
    v[off:off+11]=b"HELLO   TXT";v[off+11]=0x20
    struct.pack_into("<H",v,off+26,cluster);struct.pack_into("<I",v,off+28,len(payload))
    file_sector=data_sector+(cluster-2)*(cluster_bytes//512)
    v[file_sector*512:file_sector*512+len(payload)]=payload
    # A multi-cluster binary fixture catches one-sector/contiguous-only I/O.
    large=bytes((i*37+11)&255 for i in range(10023))
    first=cluster+1
    count=(len(large)+cluster_bytes-1)//cluster_bytes
    root=off+32
    v[root:root+11]=b"LARGE   BIN";v[root+11]=0x20
    struct.pack_into("<H",v,root+26,first)
    struct.pack_into("<I",v,root+28,len(large))
    for i in range(count):
        struct.pack_into("<H" if bits==16 else "<I",v,
                         reserved*512+(first+i)*(bits//8),
                         first+i+1 if i<count-1 else (0xFFFF if bits==16 else 0x0FFFFFFF))
    pos=(data_sector+(first-2)*(cluster_bytes//512))*512
    v[pos:pos+len(large)]=large
    v[(reserved+fatsize)*512:(reserved+2*fatsize)*512]=v[reserved*512:(reserved+fatsize)*512]
    if partitioned:
        b[446+4]=0x06 if bits==16 else 0x0C
        struct.pack_into("<II",b,446+8,prefix,sectors)
        b[510:512]=b"\x55\xaa"
    path.write_bytes(b)
    return payload


def files_on_disk(path):
    b=path.read_bytes()
    prefix=struct.unpack_from("<I",b,454)[0] if b[450] in (6,0x0C) else 0
    v=memoryview(b)[prefix*512:]
    spc=v[13];reserved=struct.unpack_from("<H",v,14)[0]
    fats=v[16];entries=struct.unpack_from("<H",v,17)[0]
    fat_size=struct.unpack_from("<H",v,22)[0] or struct.unpack_from("<I",v,36)[0]
    fat_start=reserved*512
    root_start=reserved+fats*fat_size
    data_start=root_start+(entries*32+511)//512
    bits=16 if entries else 32
    def chain(cluster):
        seen=set();out=bytearray()
        while 2<=cluster<(0xFFF8 if bits==16 else 0x0FFFFFF8):
            assert cluster not in seen
            seen.add(cluster)
            pos=(data_start+(cluster-2)*spc)*512
            assert pos+spc*512<=len(v)
            out+=v[pos:pos+spc*512]
            cluster=struct.unpack_from("<H" if bits==16 else "<I",v,
                                       fat_start+cluster*(bits//8))[0]
            if bits==32:cluster&=0x0FFFFFFF
        return out
    directory=v[root_start*512:root_start*512+entries*32] if entries else chain(2)
    result={}
    for off in range(0,len(directory),32):
        ent=directory[off:off+32]
        if ent[0]==0:break
        if ent[0]==0xE5 or ent[11]&0x18 or ent[11]==0x0F:continue
        name=bytes(ent[:8]).decode().rstrip()+"."+bytes(ent[8:11]).decode().rstrip()
        cluster=struct.unpack_from("<H",ent,26)[0]
        if bits==32:cluster|=struct.unpack_from("<H",ent,20)[0]<<16
        size=struct.unpack_from("<I",ent,28)[0]
        result[name]=bytes(chain(cluster)[:size]) if size else b""
    # Independently verify both FAT copies still match after writes.
    for i in range(1,fats):
        assert v[fat_start:fat_start+fat_size*512]==v[fat_start+i*fat_size*512:
                                                    fat_start+(i+1)*fat_size*512]
    return result


def main():
    kernel=Path(sys.argv[1]).resolve()
    ehci="--ehci" in sys.argv[2:]
    output=Path("generated-artifacts/usb-storage"+("-ehci" if ehci else "")).resolve()
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="usb-storage-") as folder:
        t=Path(folder)
        disk0=t/"fat16.img";disk1=t/"fat32.img"
        source=fat_image(disk0,16,True);fat_image(disk1,32)
        # Exercise failure paths against the same filesystem code, with
        # deterministic flush/write/release errors before the real DMA test.
        root=Path(__file__).resolve().parents[1]
        host=t/"host-test"
        subprocess.run(["gcc","-std=c11","-O1","-Wall","-Wextra",
                        str(root/"tests/usb_storage_test.c"),
                        str(root/"kernel/usb_storage.c"),
                        str(root/"kernel/fatfs/ff.c"),"-o",str(host)],check=True)
        subprocess.run([str(host),str(disk0),str(disk1)],check=True)
        dialog=t/"dialog-test"
        subprocess.run(["gcc","-std=c11","-Wall","-Wextra","-Werror",
                        str(root/"tests/storage_dialog_test.c"),
                        str(root/"shell/storage_dialog.c"),"-o",str(dialog)],check=True)
        subprocess.run([str(dialog)],check=True)
        source=fat_image(disk0,16,True);fat_image(disk1,32)
        controllers=("-device","ich9-usb-ehci1,id=usb",
                     "-device","ich9-usb-uhci1,masterbus=usb.0,firstport=0",
                     "-device","ich9-usb-uhci2,masterbus=usb.0,firstport=2",
                     "-device","ich9-usb-uhci3,masterbus=usb.0,firstport=4") if ehci else (
                     "-device","qemu-xhci,id=usb")
        args=("-cpu","core2duo","-m","256",*controllers,
              "-device","usb-kbd,bus=usb.0,port=2"+(",usb_version=1" if ehci else ""),
              "-drive",f"if=none,id=stick0,file={disk0},format=raw",
              "-device","usb-storage,id=usbdrive0,drive=stick0,bus=usb.0,port=1",
              "-drive",f"if=none,id=stick1,file={disk1},format=raw",
              "-device","usb-storage,id=usbdrive1,drive=stick1,bus=usb.0,port=3")
        g=Guest(kernel,None,False,t,extra_args=args,qemu_binary="qemu-system-x86_64")
        try:
            g.wait("Welcome to inteiliDOS",timeout=45)
            shell(g,"usb");g.wait("USB /MOUNT")
            shell(g,"usb /bogus");g.wait("Unknown USB option")
            shell(g,"usb /mount /eject");g.wait("Unknown USB option or extra arguments")
            shell(g,"usb /mount");g.wait("2 USB drive(s) mounted.")
            g.wait("EHCI controller: EHCI controller running" if ehci else
                   "USB controller: xHCI controller running")
            if ehci: g.wait("No xHCI controller found")
            g.wait("connected ports: 2" if ehci else "connected ports: 3")
            g.wait("EHCI failed ports: 0" if ehci else "Failed ports: 0")
            # Interactive copy picks exactly one file, not every drive entry.
            shell(g,"usb /copy");g.wait("Copy ONE selected")
            g.key("0");g.wait("Choose file [Esc=cancel]")
            g.key("esc");g.wait("Copy cancelled; no files copied.")
            shell(g,"usb /copy");g.key("0");g.wait("Choose file [Esc=cancel]")
            type_text(g,"1\n");g.wait("Selected file: USB0:\\HELLO.TXT")
            type_text(g,"PICKED.TXT\n");g.wait("[3] USB drive")
            g.key("3");g.wait("[1] USB1:");g.key("1")
            g.wait("Copy ONLY USB0:\\HELLO.TXT -> USB1:\\PICKED.TXT?")
            g.command("screendump",{"filename":str(output/"selected-file.ppm")})
            g.key("y");g.wait(f"Copied {len(source)} bytes to USB1:\\PICKED.TXT")
            shell(g,"dir usb0:");g.wait("HELLO.TXT")
            shell(g,"type usb0:\\hello.txt");g.wait("REAL-USB-FAT16-DATA")
            shell(g,"copy usb0:\\hello.txt usb1:\\copied.txt")
            g.wait(f"Copied {len(source)} bytes")
            shell(g,"type usb1:\\copied.txt");g.wait("REAL-USB-FAT16-DATA")
            shell(g,"copy usb0:\\large.bin usb1:\\multi.bin");g.wait("Copied 10023 bytes")
            shell(g,"copy usb0:\\large.bin usb1:\\short.bin");g.wait("Copied 10023 bytes")
            shell(g,"copy usb0:\\hello.txt usb1:\\short.bin")
            g.wait(f"Copied {len(source)} bytes")
            shell(g,"cls");shell(g,"dir usb1:");g.wait("MULTI.BIN")
            g.command("screendump",{"filename":str(output/"mounted-files.ppm")})
            # Exercise iEdit save/open through its actual dialogs.
            shell(g,"iedit");g.wait("iEdit")
            type_text(g,"USB editor save works\n")
            g.key("f2")
            g.wait("Save as")
            type_text(g,"EDIT.TXT\n");g.wait("[3] USB drive")
            g.key("3");g.wait("[0] USB0:");g.key("0")
            g.wait("Saved: USB0:\\EDIT.TXT")
            type_text(g,"UNSAVED")
            g.key("f10");g.wait("Unsaved changes!");g.key("esc")
            g.key("f3");g.wait("Unsaved changes!");g.key("esc")
            g.key("f3");g.wait("Unsaved changes!")
            g.key("f3");g.wait("Open (.txt)")
            type_text(g,"USB1:\\COPIED.TXT\n");g.wait("Opened: USB1:\\COPIED.TXT")
            g.key("f10")
            g.wait("> ")
            # BASIC's save/load dialogs use the same USB target chooser.
            shell(g,"basic");g.wait("InteiliBASIC v1.0")
            type_text(g,"10 PRINT 42\nSAVE\n");g.wait("Filename (Enter")
            type_text(g,"TEST.BAS\n");g.wait("[3] USB drive")
            g.key("3");g.wait("[0] USB0:");g.key("0")
            g.wait("Saved: USB0:\\TEST.BAS")
            type_text(g,"NEW\nLOAD\n");g.wait("Filename to load")
            type_text(g,"TEST.BAS\n");g.wait("Load from:")
            g.key("3");g.wait("[0] USB0:");g.key("0")
            g.wait("Loaded: USB0:\\TEST.BAS")
            type_text(g,"RUN\n");g.wait("\n 42")
            type_text(g,"BYE\n");g.wait("C:\\>")
            # Sheets CSV persistence and reload, not just a UI destination.
            shell(g,"sheets");g.wait("InteiliSheets")
            g.key("ret");type_text(g,"123\n")
            g.key("down");g.key("ret");type_text(g,"456\n")
            # Save directly from an uncommitted cell edit.
            g.key("ret");g.key("f2");g.wait("Save as")
            type_text(g,"GRID.CSV\n");g.wait("[3] USB drive")
            g.key("3");g.wait("[1] USB1:");g.key("1")
            g.wait("Saved: USB1:\\GRID.CSV");g.key("spc")
            g.key("f3");g.wait("Open (.csv)")
            type_text(g,"GRID.CSV\n");g.wait("Load from:")
            g.key("3");g.wait("[1] USB1:");g.key("1")
            g.wait("Opened: USB1:\\GRID.CSV");g.key("spc")
            g.wait("[A1]: 123")
            g.key("down");g.key("down");g.wait("[A3]: 456")
            g.command("screendump",{"filename":str(output/"sheets-usb-reloaded.ppm")})
            g.key("ret");type_text(g,"7")
            g.key("f10");g.wait("Unsaved changes!");g.key("esc")
            g.wait("F10:Quit")
            g.key("f3");g.wait("Unsaved changes!");g.key("esc")
            g.key("f3");g.wait("Unsaved changes!");g.key("f3")
            g.wait("Open (.csv)")
            type_text(g,"GRID.CSV\n");g.wait("Load from:")
            g.key("3");g.wait("[1] USB1:");g.key("1")
            g.wait("Opened: USB1:\\GRID.CSV");g.key("spc")
            g.wait("[A1]: 123")
            g.key("f10");g.wait("C:\\>")
            shell(g,"usb /eject");g.wait("USB storage eject complete")
            shell(g,"type usb0:\\hello.txt");g.wait("USB drive is not mounted")
            shell(g,"usb /mount");g.wait("2 USB drive(s) mounted.")
            shell(g,"type usb0:\\edit.txt");g.wait("USB editor save works")
            shell(g,"usb /eject");g.wait("USB storage eject complete")
            # Keyboard must remain usable after many command/transfer ring wraps.
            # HELP is longer than one viewport; its IEDIT line scrolls away.
            shell(g,"help");g.wait("IntelliShell also understands plain English:")
            g.command("screendump",{"filename":str(output/"keyboard-after-eject.ppm")})
            assert files_on_disk(disk0)["EDIT.TXT"]==b"USB editor save works\n\n"
            assert files_on_disk(disk1)["COPIED.TXT"]==source
            assert files_on_disk(disk1)["PICKED.TXT"]==source
            assert files_on_disk(disk1)["HELLO.TXT"]==b"REAL-USB-FAT32-DATA\n"
            assert files_on_disk(disk0)["TEST.BAS"]==b"10 PRINT 42\n"
            assert files_on_disk(disk1)["GRID.CSV"]==b"Row,A,B,C,D,E,F,G\n1,123,,,,,,\n3,456,,,,,,\n"
            assert files_on_disk(disk1)["SHORT.BIN"]==source
            assert files_on_disk(disk1)["MULTI.BIN"]==bytes((i*37+11)&255 for i in range(10023))
            assert files_on_disk(disk0)["HELLO.TXT"]==source
            print("PASS: "+("EHCI storage + companion UHCI keyboard" if ehci else
                           "xHCI-only keyboard")+" + 2 BOT drives, MBR FAT16 and raw FAT32, "
                  "usage/options, DIR/TYPE/COPY, single-file chooser/cancel, iEdit, BASIC "
                  "and Sheets USB save/load, BASIC execution after load, safe eject, "
                  "blocked access, remount and independently verified persisted bytes/FAT mirrors")
        finally:
            print(g.screen(),flush=True)
            g.close()


if __name__=="__main__":main()
