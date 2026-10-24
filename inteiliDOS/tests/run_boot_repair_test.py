#!/usr/bin/env python3
"""Native setup -> legacy boot repair -> dirty-RAM BIOS boots. Disposable HDDs."""
import ast
import hashlib
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile

from run_launchpad_dos_test import Guest
from run_hdd_cold_boot_test import shell, save_basic, load_basic

ROOT=Path(__file__).resolve().parents[1]


def historical_array(name,size):
    source=(ROOT/"shell/setup.c").read_text()
    body=source.split("static const uint8_t "+name+"[",1)[1].split("{",1)[1].split("};",1)[0]
    body=re.sub(r"/\*.*?\*/","",body,flags=re.S)
    values=re.findall(r"'(?:\\.|[^'\\])*'|0x[0-9a-fA-F]+|\b\d+\b",body)
    data=bytes(ord(ast.literal_eval(v)) if v.startswith("'") else int(v,0) for v in values)
    assert len(data)<=size
    return data+bytes(size-len(data))


def dirty_ram(guest,folder,start,end):
    q=socket.socket(socket.AF_UNIX);q.settimeout(5)
    q.connect(str(folder/"qtest.sock"))
    f=q.makefile("rwb",buffering=0)
    for addr in range(start,end,32768):
        n=min(32768,end-addr)
        f.write(f"write 0x{addr:x} 0x{n:x} ".encode()+b"a5"*n+b"\n")
        assert f.readline().strip()==b"OK"
    f.close();q.close()
    guest.command("cont")


def main():
    kernel=Path(sys.argv[1]).resolve()
    ahci="--ahci" in sys.argv[2:]
    with tempfile.TemporaryDirectory(prefix="boot-repair-") as name:
        tmp=Path(name);disk=tmp/"native.raw"
        with disk.open("wb") as f:f.truncate(64*1024**2)
        disk_args=(
            ("-device","qemu-xhci,id=usb","-device","usb-kbd,bus=usb.0",
             "-device","ahci,id=sata",
             "-drive",f"file={disk},if=none,id=native,format=raw",
             "-device","ide-hd,drive=native,bus=sata.0") if ahci else
            ("-drive",f"file={disk},if=ide,index=0,format=raw"))
        guest=Guest(kernel,None,False,tmp,disk_args)
        guest.sock.settimeout(15)
        try:
            guest.wait("Welcome to inteiliDOS");shell(guest,"setup")
            guest.wait("Setup Wizard");guest.key("ret")
            guest.wait("Choose an installation mode");guest.key("down");guest.key("ret")
            guest.wait("Select the target installation drive");guest.key("ret")
            guest.wait("Press ENTER to begin installation");guest.key("ret")
            guest.wait("Installation successful",timeout=180);guest.key("spc")
            guest.wait("> ");save_basic(guest,"preserve",7351)
        finally:guest.close()
        data=bytearray(disk.read_bytes())
        start=struct.unpack_from("<I",data,454)[0]
        kernel_before=bytes(data[(start+1)*512:(start+8201)*512])
        mbr=historical_array("mbr_bootstrap",446)+bytes(data[446:512])
        old_vbr=bytearray(historical_array("vbr_code",512))
        new_vbr=data[start*512:(start+1)*512]
        # Historical bootstrap code, but retain the actual v2 volume marker:
        # recovery must not reinterpret the filesystem as the old layout.
        old_vbr[0x25:0x29]=new_vbr[0x25:0x29]
        for off,size in ((0x21,4),(0x36,2),(0xDA,4)):
            old_vbr[off:off+size]=new_vbr[off:off+size]
        data[:512]=mbr;data[start*512:(start+1)*512]=old_vbr
        disk.write_bytes(data)
        before=hashlib.sha256(data).digest()
        # Cancellation is genuinely read-only, including no backup creation.
        guest=Guest(kernel,None,False,tmp,disk_args)
        try:
            guest.wait("Welcome to inteiliDOS")
            shell(guest,"bootrepair");guest.wait("Type YES")
            guest.text("no\n");guest.wait("cancelled; disk unchanged")
        finally:guest.close()
        assert hashlib.sha256(disk.read_bytes()).digest()==before
        guest=Guest(kernel,None,False,tmp,disk_args)
        try:
            guest.wait("Welcome to inteiliDOS")
            shell(guest,"bootrepair");guest.wait("Type YES")
            guest.text("yes\n");guest.wait("Boot repair verified")
            out=ROOT.parent/"generated-artifacts"/"boot-repair"
            out.mkdir(parents=True,exist_ok=True)
            guest.command("screendump",{"filename":str(out/"repaired.ppm")})
        finally:guest.close()
        repaired=disk.read_bytes()
        assert repaired[446:512]==data[446:512]
        assert repaired[(start+1)*512:(start+8201)*512]==kernel_before
        directory=(start+8201)*512
        entries=[]
        for i in range(15):
            filename,lba,size=struct.unpack_from("<24sII",repaired,directory+4+i*32)
            entries.append((filename.rstrip(b"\0"),lba,size))
        _,lba,size=next(e for e in entries if e[0]==b"boot-backup0.bin")
        assert size==1024 and repaired[lba*512:lba*512+size]==mbr+old_vbr
        print("PASS: cancelled repair writes nothing; confirmed repair backs up exact boot sectors and preserves partitions/kernel/config",flush=True)
        # Entire native BSS range is dirty before firmware boots; fresh
        # processes also exercise the powered-off rather than reset path.
        for attempt in range(2):
            guest=Guest(None,None,False,tmp,
                        disk_args+("-boot","order=c,strict=on","-S",
                                   "-qtest","unix:"+str(tmp/"qtest.sock")+",server=on,wait=off",
                                   "-qtest-log","/dev/null"))
            guest.sock.settimeout(15)
            try:
                dirty_ram(guest,tmp,0x180000,0x500000)
                guest.wait("Welcome to inteiliDOS",timeout=45)
                load_basic(guest,"preserve",7351)
            finally:guest.close()
        print("PASS: repeated BIOS-only HDD cold boots with dirty RAM; original BASIC file preserved",flush=True)
        # BIOS chainloader explicitly hands off DL=0x81. The installed
        # loader must not accidentally read the first disk's empty sectors.
        asm=tmp/"chain.asm"
        asm.write_text("""bits 16
org 0x1000
cli
xor ax,ax
mov ds,ax
mov es,ax
mov ss,ax
mov sp,0x7B00
cld
mov si,0x7C00
mov di,0x1000
mov cx,256
rep movsw
jmp 0:go
go:
sti
; Simulate firmware handing off a graphics splash screen, not text mode.
; The installed VBR must restore mode 3, its font/palette and page zero.
mov ax,0x0013
int 0x10
xor ax,ax
mov ds,ax
mov es,ax
cld
mov dl,0x81
mov si,dap
mov ah,0x42
int 0x13
jc $
mov dl,0x81
jmp 0:0x7C00
align 4,db 0
dap: db 16,0
dw 1,0x7C00,0
dq 0
times 510-($-$$) db 0
dw 0xAA55
""")
        chain=tmp/"first.raw"
        subprocess.run(["nasm","-f","bin",str(asm),"-o",str(chain)],check=True)
        with chain.open("r+b") as f:f.truncate(4*1024**2)
        first_before=hashlib.sha256(chain.read_bytes()).digest()
        second_args=disk_args if ahci else (
            "-drive",f"file={disk},if=ide,index=1,format=raw")
        guest=Guest(None,None,False,tmp,
                    ("-drive",f"file={chain},if=ide,index=0,format=raw")+
                    second_args+("-boot","order=c"))
        try:
            guest.wait("Welcome to inteiliDOS",timeout=45)
            load_basic(guest,"preserve",7351)
            state=tmp/"video-state.bin"
            guest.command("pmemsave",{"val":0x449,"size":0x1a,"filename":str(state)})
            video=state.read_bytes()
            assert video[0]==3 and video[0x462-0x449]==0, \
                f"VBR did not restore BIOS text mode/page: {video.hex()}"
            out=ROOT.parent/"generated-artifacts"/"boot-repair"
            out.mkdir(parents=True,exist_ok=True)
            guest.command("screendump",{"filename":str(out/"graphics-handoff.ppm")})
        finally:guest.close()
        assert hashlib.sha256(chain.read_bytes()).digest()==first_before
        print("PASS: graphics-mode BIOS handoff restored to text/page 0 on drive 0x81; original files preserved and first HDD untouched",flush=True)


if __name__=="__main__":
    main()
