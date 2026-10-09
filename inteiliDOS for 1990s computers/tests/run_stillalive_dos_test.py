#!/usr/bin/env python3
"""Run a supplied Still Alive DOS EXE without redistributing it.

Usage: python3 tests/run_stillalive_dos_test.py STILLALI.EXE kernel.elf [--full]
Checks actual floppy/CD loading, advancing text, recorded speaker output,
F8 cleanup, and a working shell. --full also waits for natural completion.
"""
from pathlib import Path
import concurrent.futures
import struct
import sys
import tempfile
import time
import wave
from run_launchpad_dos_test import Guest
from run_dos_tests import fat12_image, run


def check_medium(kernel, media, floppy, folder, full):
    folder.mkdir()
    audio = folder/"speaker.wav"
    guest = Guest(kernel, media, floppy, folder,
                  ("-icount", "shift=4,align=off,sleep=on",
                   "-audiodev", "wav,id=speaker,path="+str(audio),
                   "-machine", "pcspk-audiodev=speaker"))
    try:
        guest.wait("Welcome to inteiliDOS")
        guest.wait("> ")
        guest.text("program\n")
        guest.wait("STILLALI.EXE")
        guest.key("ret")
        guest.wait("DOS program: v86 compatibility")
        # Discard the native boot chime, including any queued backend samples.
        time.sleep(0.3)
        audio_offset = audio.stat().st_size if audio.exists() else 0
        guest.key("ret")
        guest.wait("Test Assessment Report")
        guest.wait("This was a triumph.", timeout=30)
        guest.wait("HUGE SUCCESS.", timeout=30)
        print("PASS: Still Alive lyrics advancing on "
              + ("floppy" if floppy else "CD-ROM"), flush=True)
        if full:
            deadline = time.monotonic()+300
            while time.monotonic() < deadline:
                screen = guest.screen()
                if "run-time error" in screen.lower() or "Hit any key to return to system" in screen:
                    raise AssertionError(screen)
                if "DOS program ended" in screen:
                    assert "DOS program ended (exit code 0)" in screen, screen
                    break
                if "LaunchPad DOS:" in screen: raise AssertionError(screen)
                time.sleep(0.3)
            else:
                raise AssertionError("No natural completion:\n"+screen)
            guest.key("ret")
            guest.wait("STILLALI.EXE")
            guest.key("ret")
            guest.wait("DOS program: v86 compatibility")
            guest.key("ret")
            guest.wait("Test Assessment Report")
        guest.key("f8")
        guest.wait("Stopped with F8.")
        guest.key("ret")
        guest.wait("STILLALI.EXE")
        guest.key("q")
        guest.wait("> ")
        guest.text("help\n")
        guest.wait("PROGRAM")
        guest.command("quit")  # finalize the WAV header/buffers
        guest.proc.wait(timeout=10)
        with wave.open(str(audio), "rb") as recording:
            assert recording.getsampwidth() == 2
            skip = max(0, audio_offset-44)+4096
            skip //= recording.getnchannels()*2
            recording.setpos(min(skip, recording.getnframes()))
            data = recording.readframes(recording.getnframes()-recording.tell())
            samples = struct.unpack("<"+"h"*(len(data)//2), data)
            assert samples and max(samples)-min(samples)>100, "No guest speaker signal"
        print("PASS: Still Alive speaker waveform/F8/return to shell on "
              + ("floppy" if floppy else "CD-ROM")
              + ("; full playback completed" if full else ""), flush=True)
    finally:
        guest.close()


def main():
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    image = Path(sys.argv[1]).read_bytes()
    if image[:2] != b"MZ": raise SystemExit("Expected a DOS MZ EXE.")
    kernel = Path(sys.argv[2]).resolve()
    full = len(sys.argv) == 4 and sys.argv[3] == "--full"
    with tempfile.TemporaryDirectory(prefix="inteilidos-stillalive-") as name:
        tmp = Path(name)
        disc = tmp/"disc"
        disc.mkdir()
        (disc/"STILLALI.EXE").write_bytes(image)
        run("xorriso", "-as", "mkisofs", "-quiet", "-iso-level", "1",
            "-o", tmp/"song.iso", disc)
        (tmp/"song.img").write_bytes(fat12_image({"STILLALI.EXE": image}))
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            jobs = [pool.submit(check_medium, kernel,
                                tmp/("song.img" if floppy else "song.iso"),
                                floppy, tmp/("floppy" if floppy else "cd"), full)
                    for floppy in (False, True)]
            for job in jobs: job.result()


if __name__ == "__main__":
    main()
