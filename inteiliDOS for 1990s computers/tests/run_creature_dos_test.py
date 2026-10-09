#!/usr/bin/env python3
"""Exercise the user's Creature DOS EXE without bundling third-party code.

Usage: python3 tests/run_creature_dos_test.py /path/to/CREATURE.EXE [kernel.elf]
The Pascal CRT calibrator overflows on unrestricted modern-speed CPUs.
QEMU icount bounds emulated instruction speed; it does not modify the EXE.
"""
from pathlib import Path
import sys
import tempfile
import time
from build_test_kernel import build
from run_launchpad_dos_test import Guest
from run_dos_tests import fat12_image, run


def launch(guest):
    guest.wait("Welcome to inteiliDOS")
    guest.wait("> ")
    guest.text("program\n")
    guest.wait("CREATURE.EXE")
    guest.key("ret")
    guest.wait("DOS program: v86 compatibility")
    guest.key("ret")
    screen = guest.wait("CreatureRaces")
    assert screen.splitlines()[9][31:].startswith("- CreatureRaces v1.0 -"), screen


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    executable = Path(sys.argv[1]).read_bytes()
    if executable[:2] != b"MZ":
        raise SystemExit("Expected a DOS MZ EXE.")
    with tempfile.TemporaryDirectory(prefix="inteilidos-creature-") as folder:
        tmp = Path(folder)
        kernel = Path(sys.argv[2]).resolve() if len(sys.argv) == 3 else build(tmp/"build")
        disc = tmp/"disc"
        disc.mkdir()
        (disc/"CREATURE.EXE").write_bytes(executable)
        run("xorriso", "-as", "mkisofs", "-quiet", "-iso-level", "1",
            "-o", tmp/"creature.iso", disc)
        (tmp/"creature.img").write_bytes(fat12_image({"CREATURE.EXE": executable}))
        for floppy in (False, True):
            guest = Guest(kernel, tmp/("creature.img" if floppy else "creature.iso"),
                          floppy, tmp, ("-icount", "shift=4,align=off,sleep=on"))
            try:
                launch(guest)
                guest.key("ret")
                track = guest.wait("What Creature")
                assert "01" in track.splitlines()[3][4:6], track
                # Check the actual DOS color attribute, not just its text.
                assert guest.snapshot.read_bytes()[(3*80+4)*2+1] & 15 == 4
                guest.text("1\n")
                guest.wait("What Will Your")
                guest.key("w")
                guest.wait("You Bet? $")
                guest.text("1\n")
                guest.wait("Race About to Begin...")
                deadline = time.monotonic()+120
                while time.monotonic() < deadline:
                    result = guest.screen()
                    if "Sorry. You Lose." in result or "Your Creature " in result:
                        break
                    if "LaunchPad DOS:" in result:
                        raise AssertionError(result)
                    time.sleep(0.2)
                else:
                    raise AssertionError("Race did not finish:\n"+result)
                guest.key("ret")
                guest.wait("Keep Playing? (Y/N)")
                guest.key("n")
                guest.wait("DOS program ended (exit code 3)")
                guest.key("ret")
                guest.wait("CREATURE.EXE")
                guest.key("q")
                guest.wait("> ")
                guest.text("help\n")
                guest.wait("PROGRAM")
                # Also exercise F8 while the actual Pascal CRT waits for input.
                guest.text("program\n")
                guest.wait("CREATURE.EXE")
                guest.key("ret")
                guest.wait("DOS program: v86 compatibility")
                guest.key("ret")
                guest.wait("CreatureRaces")
                guest.key("f8")
                guest.wait("Stopped with F8.")
                print("PASS: Creature title/colors/input/race/normal exit/F8 on "
                      + ("floppy" if floppy else "CD-ROM"), flush=True)
            finally:
                guest.close()


if __name__ == "__main__":
    main()
