#!/usr/bin/env python3
"""Full-kernel QEMU: Cooling UI and monitoring outside HEALTH.

All media are temporary fixtures. No user disks, fan overrides or auth bypass.
Optional --pcm reproduces a high-rate PCM/QMP stall; not a passing baseline.
"""
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time

from build_test_kernel import build, run, ROOT
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell


def main():
    output=ROOT.parent/"generated-artifacts"/"thermal-monitor"
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="inteilidos-thermal-") as folder:
        tmp=Path(folder)
        kernel=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(tmp/"build")
        symbols=subprocess.check_output(["nm","-n",str(kernel)],text=True)
        def address(name):
            return int(next(line.split()[0] for line in symbols.splitlines()
                            if len(line.split())==3 and line.split()[2]==name),16)
        root=tmp/"disc";root.mkdir()
        source=tmp/"pcm.s"
        source.write_text(
            ".text\n.globl _start\n_start:\n"
            "movl $1, PHASE\n"
            f"movl {address('live')+4}, %eax\nmovl %eax, BEFORE\n"
            "pushl $88200\npushl $BUFFER\n"
            f"movl ${address('speaker_play_pcm')}, %eax\ncall *%eax\naddl $8,%esp\n"
            f"movl {address('live')+4}, %eax\nmovl %eax, AFTER\n"
            "movl $2, PHASE\nret\n"
            ".data\n.globl PHASE\nPHASE: .long 0\n"
            "BEFORE: .long 0\nAFTER: .long 0\nBUFFER: .fill 88200,1,128\n")
        run("as","--32",source,"-o",tmp/"pcm.o")
        app=root/"PCM.ELF"
        run("ld","-m","elf_i386","-Ttext=0x00A00000","-e","_start",
            tmp/"pcm.o","-o",app)
        app_symbols=subprocess.check_output(["nm",str(app)],text=True)
        phase=int(next(l.split()[0] for l in app_symbols.splitlines()
                       if l.split()[-1]=="PHASE"),16)
        run("xorriso","-as","mkisofs","-quiet","-V","THERMAL_TEST",
            "-o",tmp/"disc.iso",root)
        guest=Guest(kernel,tmp/"disc.iso",False,tmp)
        guest.sock.settimeout(10)
        try:
            guest.wait("Welcome to inteiliDOS")
            print("Booted; checking background cadence and Cooling UI",flush=True)
            def read_u32(addr):
                file=tmp/"memory.bin"
                guest.command("pmemsave",{"val":addr,"size":4,"filename":str(file)})
                return struct.unpack("<I",file.read_bytes())[0]
            # QEMU without a supported monitor must not invent sensor data.
            live=address("live")
            first=read_u32(live+4)
            time.sleep(1.1)
            assert read_u32(live+4)>=first+2
            shell(guest,"health")
            guest.wait("1 Health dashboard");guest.key("4")
            text=guest.wait("Fan control: unavailable")
            assert "CPU temperature: Unavailable" in text
            assert "protection is NOT confirmed" in text
            assert text.count("Status unavailable")==3
            assert "running cooling fan..." not in text
            guest.command("screendump",{"filename":str(output/"cooling.ppm")})
            checks=int(re.search(r"checks: (\d+)",text).group(1))
            time.sleep(1.1)
            text=guest.screen()
            assert int(re.search(r"checks: (\d+)",text).group(1))>=checks+2
            # New tab wraps both ways; existing tabs still exist.
            guest.key("right");guest.wait("Heap metadata")
            guest.key("left");guest.wait("Fan control: unavailable")
            guest.key("2");guest.wait("No ATA hard disk detected")
            guest.key("3");guest.wait("CPU vendor")
            guest.key("esc")
            first=read_u32(live+4);time.sleep(1.1)
            assert read_u32(live+4)>=first+2
            if "--pcm" not in sys.argv:
                print("PASS: real kernel background checks outside HEALTH, Cooling tab truthfulness/navigation, dashboard reentry",flush=True)
                return
            print("Dashboard checks passed; starting PCM fixture",flush=True)
            shell(guest,"cdrom /run")
            guest.wait("PCM.ELF");guest.key("ret")
            guest.wait("Press any key to launch");guest.key("spc")
            print("PCM launched; fixture captures counters immediately before/after playback",flush=True)
            # High-rate speaker I/O can starve QEMU monitor requests. The
            # guest fixture itself records counters, avoiding observer I/O
            # during PCM. A deferred tick correction cannot pass this check.
            time.sleep(5)
            limit=time.monotonic()+8
            while read_u32(phase)!=2 and time.monotonic()<limit: time.sleep(.1)
            assert read_u32(phase)==2
            assert read_u32(phase+8)-read_u32(phase+4)>=6
            guest.key("esc")
            shell(guest,"health");guest.key("4")
            guest.wait("Fan control: unavailable")
            print("PASS: real kernel background checks outside HEALTH, Cooling tab truthfulness/navigation, PCM-time monitoring and timer progress, dashboard reentry")
        finally:
            guest.sock.close()
            guest.proc.kill()
            _,error=guest.proc.communicate()
            (output/"qemu.log").write_bytes(error)


if __name__=="__main__":
    main()
