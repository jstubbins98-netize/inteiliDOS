#!/usr/bin/env python3
"""Verify System Health in a real i386 guest, including failing read injection.
Usage: python3 tests/run_health_test.py [kernel.elf]
"""
from pathlib import Path
import hashlib
import re
import subprocess
import sys
import tempfile
import time
from build_test_kernel import build, ROOT
from run_dos_tests import fat12_image, run, TESTS
from run_launchpad_dos_test import Guest


def focused(tmp):
    flags=["-m32","-march=i386","-O1","-ffreestanding","-fno-builtin","-fno-pie",
           "-fno-pic","-fno-stack-protector","-ffunction-sections","-fdata-sections",
           "-Wall","-Wextra","-Werror","-DCONFIG_ENABLE_AHCI=1","-DCONFIG_ENABLE_PCI_IDE=1"]
    objects=[]
    for source in (TESTS/"health_checks.c",ROOT/"kernel/ata.c",ROOT/"kernel/vga.c"):
        obj=tmp/(source.stem+".o")
        run("gcc",*flags,"-c",source,"-o",obj)
        objects.append(obj)
    run("nasm","-f","elf32",TESTS/"cdrom_boot.asm","-o",tmp/"boot.o")
    run("ld","-m","elf_i386","--gc-sections","-T",TESTS/"cdrom_host.ld",
        "-o",tmp/"checks.elf",tmp/"boot.o",*objects)
    log=tmp/"checks.log"
    result=subprocess.run(["qemu-system-i386","-nodefaults","-device","VGA",
                           "-kernel",str(tmp/"checks.elf"),"-display","none","-no-reboot",
                           "-debugcon","file:"+str(log),
                           "-device","isa-debug-exit,iobase=0xf4,iosize=0x04"],
                          stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
    assert result.returncode==33, log.read_text()+result.stderr.decode()
    print(log.read_text().strip())


def integration(kernel, tmp, kind):
    folder=tmp/kind; folder.mkdir()
    floppy=folder/"boot.img"; floppy.write_bytes(fat12_image({}))
    drives=[]
    extra=[]
    for index in range(2 if kind=="multiple" else 0 if kind=="no-hdd" else 1):
        disk=folder/f"hdd{index}.img"
        with disk.open("wb") as stream: stream.truncate((16+index*16)*1024*1024)
        digest=hashlib.sha256(disk.read_bytes()).digest()
        drives.append((disk,digest))
        name=str(disk)
        if kind=="read-error":
            config=folder/"errors.conf"
            config.write_text('[inject-error]\nevent = "read_aio"\nerrno = "5"\n'
                              'sector = "0"\nonce = "off"\nimmediately = "on"\n')
            name="blkdebug:"+str(config)+":"+name
        extra+=["-drive",f"file={name},if=ide,index={index},format=raw"]
    guest=Guest(kernel,floppy,True,folder,extra)
    try:
        guest.wait("Welcome to inteiliDOS"); guest.wait("> ")
        guest.text("health\n"); guest.wait("SYSTEM HEALTH")
        if kind=="read-error": guest.wait("PROBLEM DETECTED")
        else: guest.wait("No problems detected")
        guest.key("tab"); guest.wait("Disk health")
        if kind=="no-hdd": guest.wait("No ATA hard disk detected.")
        else:
            guest.wait("QEMU HARDDISK"); guest.wait("Temperature:")
            guest.wait("READ ERROR" if kind=="read-error" else "Read samples: PASS")
        if kind=="multiple":
            guest.key("down"); guest.wait("Disk 2/2"); guest.wait("Capacity: 32 MiB")
            guest.key("up"); guest.wait("Disk 1/2"); guest.wait("Capacity: 16 MiB")
        guest.key("r")
        guest.key("right")
        guest.wait("Live PIT ticks:")
        a=int(re.search(r"Live PIT ticks: (\d+)",guest.screen()).group(1))
        time.sleep(1.2)
        b=int(re.search(r"Live PIT ticks: (\d+)",guest.screen()).group(1))
        assert b>a, "Diagnostics did not refresh"
        guest.key("left"); guest.key("1"); guest.wait("Heap metadata:")
        guest.key("esc"); guest.wait("> ")
        guest.text("health\n"); guest.wait("SYSTEM HEALTH"); guest.key("q"); guest.wait("> ")
        guest.text("help\n"); guest.wait("HEALTH")
    finally:
        guest.close()
    for disk,digest in drives:
        assert hashlib.sha256(disk.read_bytes()).digest()==digest, "Disk was modified"
    print("PASS: health command, tabs, live updates, recheck, exit and read-only disks: "+kind)


def main():
    with tempfile.TemporaryDirectory(prefix="inteilidos-health-") as name:
        tmp=Path(name)
        focused(tmp)
        kernel=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(tmp/"build")
        for kind in ("no-hdd","normal","multiple","read-error"):
            integration(kernel,tmp,kind)


if __name__=="__main__": main()
