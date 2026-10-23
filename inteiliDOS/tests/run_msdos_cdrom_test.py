#!/usr/bin/env python3
"""Real DOS ISO game access on SATA ATAPI sharing AHCI with the native HDD.
Uses only locally supplied DOS install media; all disks are disposable copies.
"""
from pathlib import Path
import argparse
import hashlib
import shutil
import subprocess
import tempfile
import time
import re
from build_test_kernel import ROOT, build, run
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell, clear_dos
from run_msdos_external_media_test import native_disk, protected


def dos(g,text,timeout=60,write_refusal=False):
    """Wait for DOS to finish, not just to print a partial directory/result.
    DIR's disk-free scan can still be running after the filenames appear;
    keys sent then can be discarded by its Ctrl-C checks."""
    shell(g,text)
    time.sleep(.25)
    deadline=time.monotonic()+timeout
    acknowledged=0
    while time.monotonic()<deadline:
        screen=g.screen()
        lines=[line.strip() for line in screen.splitlines() if line.strip()]
        if lines and re.fullmatch(r"[A-Z]:\\[^>]*>",lines[-1]): return screen
        if write_refusal and lines and "Abort, Retry, Fail?" in lines[-1]:
            if acknowledged>=4:
                raise AssertionError("DOS did not accept Fail:\n"+screen)
            g.key("f");acknowledged+=1;time.sleep(.5)
        time.sleep(.15)
    raise AssertionError("DOS did not finish "+text+":\n"+g.screen())


def make_iso(folder, output):
    run("xorriso", "-as", "mkisofs", "-quiet", "-iso-level", "1",
        "-V", "DOSGAMES", "-o", output, folder)


def session(kernel, native, iso, folder):
    folder.mkdir()
    args = ["-m", "256", "-device", "qemu-xhci,id=usb",
            "-device", "usb-kbd,bus=usb.0,port=2",
            "-device", "ich9-ahci,id=sata",
            "-drive", f"file={native},if=none,id=native,format=raw",
            "-device", "ide-hd,drive=native,bus=sata.0"]
    if iso:
        args += ["-drive", f"file={iso},if=none,id=cd,media=cdrom,readonly=on",
                 "-device", "ide-cd,drive=cd,bus=sata.1"]
    else:
        args += ["-device","ide-cd,bus=sata.1"]
    g = Guest.__new__(Guest)
    try:
        Guest.__init__(g,kernel,None,False,folder,tuple(args))
    except Exception:
        if hasattr(g,"proc"):
            if g.proc.poll() is None: g.proc.kill()
            _,error=g.proc.communicate()
            raise AssertionError(error.decode(errors="replace"))
        raise
    g.wait("Welcome to inteiliDOS"); g.wait("> ")
    return g


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path)
    parser.add_argument("--media", type=Path, default=ROOT.parent / "MS-DOS_disks")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="dos-cdrom-") as directory:
        tmp = Path(directory)
        kernel = args.kernel.resolve() if args.kernel else build(tmp/"build", modern=True)
        disc = tmp/"disc"; game = disc/"GAMES"; game.mkdir(parents=True)
        run("nasm", "-f", "bin", ROOT/"tests/msdos_cdrom_game.asm", "-o", game/"PLAY.COM")
        (game/"ASSET.DAT").write_bytes(bytes([37])*132096)
        (game/"EMPTY.DAT").write_bytes(b"")
        (game/"DATA.TXT").write_bytes(b"ISO GAME DIRECTORY\r\n")
        (game/"DEEP").mkdir()
        (game/"DEEP"/"LEVEL.TXT").write_bytes(b"NESTED CD LEVEL\r\n")
        iso = tmp/"games.iso"; make_iso(disc, iso)
        original_iso = hashlib.sha256(iso.read_bytes()).digest()
        shutil.copyfile(args.media/"Disk1.img", disc/"D6221.IMG")
        install = tmp/"install.iso"; make_iso(disc, install)
        native = tmp/"native.img"; native_disk(native)
        safe = protected(native)
        # Seed the private C: through SATA CD; D: is opt-in, not needed for boot.
        g = session(kernel,native,install,tmp/"seed")
        try:
            shell(g,"ms-dos")
            g.wait("MS-DOS 6.22 -",timeout=180);g.wait("C:\\>")
            clear_dos(g)
            g.key("f8");g.wait("Returned from MS-DOS")
        finally:
            g.close()
        # Game-only ISO contains NO floppy image or DOS boot files.
        g = session(kernel,native,iso,tmp/"game")
        try:
            shell(g,"ms-dos /cdrom")
            g.wait("MS-DOS 6.22 -",timeout=60);g.wait("C:\\>")
            clear_dos(g)
            assert "GAMES" in dos(g,"dir d:\\")
            screen=dos(g,"dir d:\\games")
            assert "PLAY" in screen and "ASSET" in screen,screen
            assert "NESTED CD LEVEL" in dos(g,"type d:\\games\\deep\\level.txt")
            dos(g,"d:");dos(g,"cd \\games")
            assert "CDGAME PASS" in dos(g,"play")
            assert "1 file(s) copied" in dos(g,"copy data.txt c:\\cdcopy.txt")
            # A FAT-backed read-only disk can raise DOS's standard critical
            # write-protect prompt; answer Fail and verify no false success.
            screen=dos(g,"copy c:\\cdsave.txt d:\\blocked.txt",write_refusal=True)
            assert "1 file(s) copied" not in screen.split("copy c:\\cdsave.txt")[-1]
            dos(g,"echo WRITE-REFUSAL-RETURNED")
            assert "CDGAME PASS" in dos(g,"type c:\\cdsave.txt")
            assert "ISO GAME DIRECTORY" in dos(g,"type c:\\cdcopy.txt")
            # Screenshot is a native test artifact, not a web preview.
            g.command("screendump",{"filename":str(tmp/"cd-game.ppm")})
            capture=ROOT.parent/"generated-artifacts/msdos-cdrom/game.png"
            capture.parent.mkdir(parents=True,exist_ok=True)
            subprocess.run(["convert",str(tmp/"cd-game.ppm"),str(capture)],check=True)
            g.key("f8");g.wait("Returned from MS-DOS")
        finally:
            g.close()
        assert protected(native)==safe, "native sectors or sentinel changed"
        assert hashlib.sha256(iso.read_bytes()).digest()==original_iso
        # Verify copied bytes using the private disk's actual on-disk offset.
        with native.open("rb") as f:
            f.seek(2048*512);directory=f.read(512)
        import struct
        entries=[struct.unpack_from("<24sII",directory,4+i*32) for i in range(15)]
        lba=next(start for name,start,size in entries if name.rstrip(b"\0")==b"MSDOS622.HDD")
        device=f"{native}@@{(lba+1)*512}"
        for name,expected in (("CDSAVE.TXT",b"CDGAME PASS\r\n"),
                              ("CDCOPY.TXT",b"ISO GAME DIRECTORY\r\n")):
            data=subprocess.check_output(["mtype","-i",device,"::"+name])
            assert data==expected,(name,data)
        missing=subprocess.run(["mtype","-i",device,"::BLOCKED.TXT"],capture_output=True)
        assert missing.returncode, "read-only destination incorrectly appeared on C:"
        # Re-entry WITHOUT /CDROM leaves the native C: saves usable.
        g=session(kernel,native,None,tmp/"plain")
        try:
            shell(g,"ms-dos /cdrom")
            g.wait("cannot mount game CD",timeout=45)
            shell(g,"ms-dos")
            g.wait("MS-DOS 6.22 -",timeout=60);clear_dos(g)
            assert "CDGAME PASS" in dos(g,"type c:\\cdsave.txt")
            assert "Invalid drive specification" in dos(g,"d:")
            g.key("f8");g.wait("Returned from MS-DOS")
        finally:
            g.close()
        print("PASS: SATA ISO directories, game/resource reads, seek, private C: saves,"
              " write refusal, F8, re-entry, shared AHCI native data preserved")


if __name__ == "__main__":
    main()
