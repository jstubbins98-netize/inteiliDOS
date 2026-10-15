#!/usr/bin/env python3
"""Optical-slot expansion must not alias or hide LaunchPad's floppy source."""
from pathlib import Path
import subprocess
import sys
import tempfile
from run_launchpad_dos_test import Guest, fat12_image


def main():
    kernel=Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="launchpad-floppy-") as folder:
        tmp=Path(folder)
        asm=tmp/"app.s"
        asm.write_text(".text\n.globl _start\n_start:\n"
                       "movl $0xF10A2026, DONE\nret\n"
                       ".data\n.globl DONE\nDONE: .long 0\n")
        subprocess.run(["as","--32",str(asm),"-o",str(tmp/"app.o")],check=True)
        elf=tmp/"GOOD.ELF"
        subprocess.run(["ld","-m","elf_i386","-Ttext=0x00A00000","-e","_start",
                        str(tmp/"app.o"),"-o",str(elf)],check=True)
        symbols=subprocess.check_output(["nm",str(elf)],text=True)
        addr=int(next(line.split()[0] for line in symbols.splitlines()
                      if line.split()[-1]=="DONE"),16)
        media=tmp/"floppy.img"
        media.write_bytes(fat12_image({"GOOD.ELF":elf.read_bytes()}))
        guest=Guest(kernel,media,True,tmp)
        try:
            guest.wait("Welcome to inteiliDOS");guest.text("launchpad\n")
            guest.wait("Floppy A:");guest.wait("GOOD.ELF")
            guest.key("ret");guest.wait("Press any key to launch");guest.key("spc")
            guest.command("pmemsave",{"val":addr,"size":4,"filename":str(tmp/"flag")})
            assert (tmp/"flag").read_bytes()==bytes.fromhex("26200af1")
            guest.key("esc");guest.wait("> ")
            print("PASS: floppy source remains distinct from SATA slots; FAT12 native ELF executes and returns to shell")
        finally:guest.close()


if __name__=="__main__":
    main()
