#!/usr/bin/env python3
"""Native32 on real x86-64 QEMU CPUs, USB-only input and SATA persistence.

All disks are disposable /tmp fixtures. The sparse SATA image exceeds 128
GiB but consumes only a few allocated pages. Never touches user disks.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

from build_test_kernel import build, run, ROOT
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell

HIGH_LBA=0x10000001


def main():
    with tempfile.TemporaryDirectory(prefix="inteilidos-modern-") as folder:
        tmp=Path(folder)
        kernel=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(tmp/"build",modern=True)
        header=kernel.read_bytes()[:20]
        assert header[4]==1 and struct.unpack_from("<H",header,18)[0]==3
        symbols=subprocess.check_output(["nm","-n",str(kernel)],text=True)
        def address(name):
            return int(next(l.split()[0] for l in symbols.splitlines()
                            if len(l.split())==3 and l.split()[2]==name),16)
        source=tmp/"sata.c"
        source.write_text(
            '#include "kernel/ata.h"\n'
            'volatile unsigned RESULT;\nstatic unsigned char data[512],out[512];\n'
            'void _start(void *api) {(void)api;ata_drive_t d;RESULT=0;\n'
            f'int (*get)(unsigned char,ata_drive_t*)=(void*){address("ata_get_drive")};\n'
            f'int (*read)(unsigned char,unsigned,unsigned char*)=(void*){address("ata_read_sector")};\n'
            f'int (*write)(unsigned char,unsigned,const unsigned char*)=(void*){address("ata_write_sector")};\n'
            f'if(get(4,&d)||!d.present||d.drive_type!=ATA_TYPE_AHCI||d.total_sectors<={HIGH_LBA})return;RESULT|=1;\n'
            'if(read(4,7,out))return;for(int i=0;i<512;i++)if(out[i]!=0xA7)return;RESULT|=2;\n'
            f'if(read(4,{HIGH_LBA},out))return;for(int i=0;i<512;i++)if(out[i]!=0xA5)return;RESULT|=4;\n'
            'for(int i=0;i<512;i++)data[i]=0x5A;\n'
            f'if(write(4,{HIGH_LBA},data))return;RESULT|=8;\n'
            f'if(read(4,{HIGH_LBA},out))return;for(int i=0;i<512;i++)if(out[i]!=0x5A)return;RESULT|=16;\n'
            'if(read(4,d.total_sectors,out)!=-1||write(4,d.total_sectors,data)!=-1)return;RESULT|=32;\n'
            '}\n')
        run("gcc","-m32","-march=i386","-O1","-ffreestanding","-fno-pie",
            "-fno-stack-protector","-fno-builtin","-nostdlib","-I",ROOT,
            "-c",source,"-o",tmp/"app.o")
        root=tmp/"disc";root.mkdir()
        app=root/"SATA.ELF"
        run("ld","-m","elf_i386","-Ttext=0x00A00000","-e","_start",
            tmp/"app.o","-o",app)
        app_symbols=subprocess.check_output(["nm",str(app)],text=True)
        result_addr=int(next(l.split()[0] for l in app_symbols.splitlines()
                             if l.split()[-1]=="RESULT"),16)
        run("xorriso","-as","mkisofs","-quiet","-V","MODERN_TEST",
            "-o",tmp/"disc.iso",root)
        screenshots=ROOT.parent/"generated-artifacts"/"modern-hardware"
        screenshots.mkdir(parents=True,exist_ok=True)
        # No PS/2 driver in this profile: QMP input can only reach the shell
        # through native USB, not firmware-emulated PS/2 keyboard services.
        for host,cpu in (("qemu-xhci","core2duo"),("piix3-usb-uhci","phenom")):
            disk=tmp/(host+".raw")
            with disk.open("wb") as f:
                f.truncate(140*1024**3)
                f.seek(7*512);f.write(bytes([0xA7])*512)
                f.seek(HIGH_LBA*512);f.write(bytes([0xA5])*512)
            args=("-cpu",cpu,"-m","256","-device",host+",id=usb",
                  "-device","usb-mouse,bus=usb.0,port=1",
                  "-device","usb-kbd,bus=usb.0,port=2",
                  "-device","ahci,id=sata",
                  "-drive","file="+str(disk)+",if=none,id=satadisk,format=raw",
                  "-device","ide-hd,drive=satadisk,bus=sata.0")
            sata_cd="--sata-optical" in sys.argv[2:]
            if sata_cd:
                args+=("-drive",f"file={tmp/'disc.iso'},if=none,id=optical,media=cdrom,format=raw",
                       "-device","ide-cd,drive=optical,bus=sata.5")
            guest=Guest(kernel,None if sata_cd else tmp/"disc.iso",False,tmp,extra_args=args,
                        qemu_binary="qemu-system-x86_64")
            guest.sock.settimeout(15)
            try:
                guest.wait("Welcome to inteiliDOS",timeout=45)
                guest.wait("x86-64 capable")
                shell(guest,"health");guest.wait("SYSTEM HEALTH");guest.key("3")
                guest.wait("CPU x86-64: capable")
                guest.wait("xHCI USB keyboard active" if host=="qemu-xhci"
                           else "UHCI USB keyboard active")
                guest.key("2");guest.wait("SATA / AHCI")
                guest.wait("PASS")
                guest.command("screendump",{"filename":str(screenshots/(host+".ppm"))})
                guest.key("esc")
                # Explicit shift/caps behavior and report releases.
                guest.key("caps_lock")
                # Earlier headings scroll out of the 25-row text viewport.
                guest.text("help\n");guest.wait("IntelliShell also understands plain English:")
                guest.key("caps_lock")
                # Exercise keyboard transfer/event-ring wrap.
                for _ in range(16): guest.text("cls\n")
                shell(guest,"cdrom /run");guest.wait("SATA.ELF")
                guest.key("ret");guest.wait("Press any key to launch");guest.key("spc")
                value=tmp/"result.bin"
                limit=time.monotonic()+15
                result=0
                while time.monotonic()<limit:
                    guest.command("pmemsave",{"val":result_addr,"size":4,"filename":str(value)})
                    result=struct.unpack("<I",value.read_bytes())[0]
                    if result==63: break
                    time.sleep(.1)
                assert result==63, f"{host}: SATA fixture failed with flags={result:#x}"
                guest.key("esc")
                print(f"PASS {host}/{cpu}: IA-32 on x86-64, native USB-only keyboard after mouse, report-ring wrap, AHCI inventory/read/write/flush beyond 128GiB, out-of-range rejection"+
                      ("; test ELF loaded from SATA optical port 5" if sata_cd else ""),flush=True)
            finally:
                guest.close()
            with disk.open("rb") as f:
                f.seek(7*512);assert f.read(512)==bytes([0xA7])*512
                f.seek(HIGH_LBA*512);assert f.read(512)==bytes([0x5A])*512
                f.seek(0);assert f.read(512)==bytes(512)
            print("PASS persisted SATA sector and untouched sentinels",flush=True)


if __name__=="__main__":
    main()
