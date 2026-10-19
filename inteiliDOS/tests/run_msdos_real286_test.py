#!/usr/bin/env python3
"""Real-mode 286 fixture in actual DOS; all disk writes use disposable media."""
from pathlib import Path
import sys
import tempfile
import time
from build_test_kernel import ROOT, build, run
from run_launchpad_dos_test import Guest, fat12_image
from run_msdos622_test import prepare, shell, clear_dos
from run_msdos_external_media_test import native_disk, protected


def wait(guest, text, timeout=45, old_errors=0):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        screen=guest.screen()
        if text in screen:
            return screen
        if screen.count("MS-DOS stopped:")>old_errors or "REAL286 CPU TEST FAILED" in screen:
            raise AssertionError(screen)
        time.sleep(.1)
    raise AssertionError(f"Missing {text!r}:\n{guest.screen()}")


def main():
    out=ROOT.parent/"generated-artifacts/msdos-real286"
    out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="msdos-real286-") as folder:
        tmp=Path(folder)
        kernel=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(tmp/"build")
        media=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else ROOT.parent/"MS-DOS_disks"
        prepare(media,tmp/"disc")
        run("nasm","-f","bin",ROOT/"tests/msdos_real286.asm","-o",tmp/"cpu.com")
        # SMSW AX; OR AX,1; LMSW AX. Protected mode must not be entered.
        pm=bytes.fromhex("0f01e083c8010f01f0ebfe")
        invalid=bytes.fromhex("6690ebfe")  # 386 operand-size prefix on a 286
        source=tmp/"ports.asm"
        source.write_text("""bits 16
cpu 286
org 0x100
mov dx,ready
mov ah,9
int 0x21
cli
mov ax,0x9000
mov es,ax
xor di,di
mov dx,0x300
again:
mov cx,0xffff
rep insw
jmp again
ready db 'PORT LOOP READY',13,10,'$'
""")
        run("nasm","-f","bin",source,"-o",tmp/"ports.com")
        (tmp/"disc/CPU286.IMG").write_bytes(fat12_image({
            "CPU.COM":(tmp/"cpu.com").read_bytes(),"PROTECT.COM":pm,
            "I386.COM":invalid,"PORTS.COM":(tmp/"ports.com").read_bytes(),
        }))
        run("xorriso","-as","mkisofs","-quiet","-iso-level","1",
            "-o",tmp/"dos.iso",tmp/"disc")
        native=tmp/"native.img";native_disk(native);safe=protected(native)
        guest=Guest(kernel,tmp/"dos.iso",False,tmp,
                    ("-drive",f"file={native},if=ide,index=1,format=raw"))
        try:
            guest.wait("Welcome to inteiliDOS");guest.wait("> ")
            shell(guest,"ms-dos")
            wait(guest,"MS-DOS 6.22 -",180);wait(guest,"C:\\>")
            guest.key("f8");guest.wait("Returned from MS-DOS")
            def start():
                old_errors=guest.screen().count("MS-DOS stopped:")
                shell(guest,"ms-dos /image CPU286.IMG")
                wait(guest,"MS-DOS 6.22 -",90,old_errors);wait(guest,"C:\\>")
                clear_dos(guest)
            start()
            shell(guest,"a:\\cpu.com")
            print(wait(guest,"REAL286 CPU TEST OK"),flush=True)
            guest.command("screendump",{"filename":str(out/"real286-dos.ppm")})
            clear_dos(guest)
            prompts=guest.screen().count("C:\\>")
            shell(guest,"echo REAL286-SAVED>c:\\cpu.txt")
            deadline=time.monotonic()+30
            while guest.screen().count("C:\\>")<=prompts:
                if time.monotonic()>deadline:
                    raise AssertionError("DOS redirection did not finish:\n"+guest.screen())
                time.sleep(.1)
            guest.key("f8");guest.wait("Returned from MS-DOS")
            start();shell(guest,"a:\\protect.com")
            guest.wait("protected mode is not supported")
            start();shell(guest,"a:\\i386.com")
            guest.wait("unsupported CPU instruction")
            start();shell(guest,"type c:\\cpu.txt");wait(guest,"REAL286-SAVED")
            shell(guest,"a:\\ports.com");wait(guest,"PORT LOOP READY")
            guest.key("f8");guest.wait("Returned from MS-DOS")
            shell(guest,"help");guest.wait("HEALTH")
            guest.command("screendump",{"filename":str(out/"shell.ppm")})
        finally:
            guest.close()
        assert protected(native)==safe
        print("PASS: 286 detection/PUSH SP/shift masks, MSW/GDT/IDT/CLTS, "
              "relocated interrupts, #BR/#UD/#DE restart IP, byte/word REP port I/O, "
              "PE refusal, 386 rejection, saved DOS C:, F8 under CLI/REP, native files",flush=True)


if __name__=="__main__":
    main()
