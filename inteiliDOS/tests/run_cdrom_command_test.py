#!/usr/bin/env python3
"""Complete-kernel CDROM test with disposable ISO and native ELF fixtures."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell


def run(*args):
    subprocess.run([str(a) for a in args],check=True,stdout=subprocess.DEVNULL)


def main():
    kernel=Path(sys.argv[1]).resolve()
    sata="--sata" in sys.argv[2:]
    empty="--empty" in sys.argv[2:]
    if empty and not sata:raise SystemExit("--empty requires --sata")
    output=Path("generated-artifacts/cdrom-command").resolve()
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cdrom-command-") as folder:
        t=Path(folder);root=t/"root";(root/"APPS").mkdir(parents=True)
        (root/"NOTES.TXT").write_text("Native CDROM command test\n")
        (root/"BAD.ELF").write_bytes(b"MZnot a native ELF executable")
        asm=t/"app.s"
        asm.write_text(".text\n.globl _start\n_start:\n"
                       "movl $0xCDAA2026, APP_DONE\nret\n"
                       ".data\n.globl APP_DONE\nAPP_DONE: .long 0\n")
        run("as","--32",asm,"-o",t/"app.o")
        elf=root/"APPS"/"GOOD.ELF"
        # All segments are beyond the full 5-9 MiB staging window.
        run("ld","-m","elf_i386","-Ttext=0x00A00000","-e","_start",t/"app.o","-o",elf)
        symbols=subprocess.check_output(["nm",str(elf)],text=True)
        addr=int(next(line.split()[0] for line in symbols.splitlines()
                      if line.split()[-1]=="APP_DONE"),16)
        cd=t/"cd.iso"
        run("xorriso","-as","mkisofs","-quiet","-V","CDROM_TEST","-o",cd,root)
        digest=hashlib.sha256(cd.read_bytes()).digest()
        disk=t/"hdd.raw"
        with disk.open("wb") as f:f.truncate(8*1024*1024)
        hdd_before=hashlib.sha256(disk.read_bytes()).digest()
        sata_args=("-cpu","qemu64","-m","256","-device","qemu-xhci,id=usb",
                   "-device","usb-kbd,bus=usb.0",
                   "-device","ahci,id=sata",
                   "-drive",("if=none,id=optical,media=cdrom" if empty else
                             f"file={cd},if=none,id=optical,media=cdrom,format=raw"),
                   "-device","ide-cd,drive=optical,bus=sata.0",
                   "-drive",f"file={disk},if=none,id=harddisk,format=raw",
                   "-device","ide-hd,drive=harddisk,bus=sata.1")
        g=Guest(kernel,None if sata else cd,False,t,
                extra_args=sata_args if sata else ())
        try:
            g.wait("Welcome to inteiliDOS")
            shell(g,"cls")
            shell(g,"cdrom")
            g.wait("Show command usage")
            shell(g,"cdrom /?")
            g.wait("Unknown CDROM option")
            shell(g,"cls")
            shell(g,"cdrom /drive-info")
            if empty:
                g.wait("SATA / AHCI optical")
                g.wait("No readable data disc")
                g.command("blockdev-change-medium",{"device":"optical",
                          "filename":str(cd),"format":"raw"})
                g.command("blockdev-close-tray",{"device":"optical"})
                shell(g,"cls");shell(g,"cdrom /drive-info")
            g.wait("volume: CDROM_TEST")
            g.wait("ATAPI")
            if sata:g.wait("SATA / AHCI optical")
            shell(g,"cdrom /list")
            g.wait("NOTES.TXT");g.wait("<DIR> APPS")
            g.command("screendump",{"filename":str(output/"listing.ppm")})
            shell(g,"cdrom /run")
            g.wait("BAD.ELF");assert "NOTES.TXT" not in g.screen()
            # BAD.ELF must not route into the removed DOS compatibility path.
            g.key("down");g.key("ret");g.wait("Not a native ELF executable")
            g.key("spc");g.key("up");g.key("ret");g.wait("GOOD.ELF")
            g.key("ret");g.wait("Press any key to launch")
            g.key("spc")
            g.command("pmemsave",{"val":addr,"size":4,"filename":str(t/"flag.bin")})
            assert (t/"flag.bin").read_bytes()==bytes.fromhex("2620aacd")
            g.key("esc")
            shell(g,"launchpad");g.wait("NOTES.TXT");g.key("esc")
            block=next(b for b in g.command("query-block")
                       if b.get("inserted",{}).get("file")==str(cd))
            shell(g,"cdrom /eject");g.wait("Eject command accepted")
            shell(g,"cdrom /drive-info");g.wait("No readable data disc")
            g.command("blockdev-change-medium",{"device":block["device"],
                      "filename":str(cd),"format":"raw"})
            g.command("blockdev-close-tray",{"device":block["device"]})
            shell(g,"cls");shell(g,"cdrom /drive-info");g.wait("volume: CDROM_TEST")
            shell(g,"cdrom /bad");g.wait("Unknown CDROM option")
            assert hashlib.sha256(cd.read_bytes()).digest()==digest
            assert hashlib.sha256(disk.read_bytes()).digest()==hdd_before
            print(f"PASS: {'SATA/AHCI' if sata else 'IDE'} (boot-empty={empty}) live CD info/listing, ELF-only browser, nested ELF execution, invalid ELF rejection, normal LaunchPad restoration, eject/reinsert, unchanged ISO/HDD")
        finally:
            g.close()


if __name__=="__main__":
    main()
