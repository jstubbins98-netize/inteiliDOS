#!/usr/bin/env python3
"""Optional real-game regression using a separately supplied installed Mario HDD.

Never modifies the supplied disk or bundles the copyrighted game. Selects
music and voice through SETUP, records native HDA, and observes PCM during FM.
"""
import argparse
import array
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import wave
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("kernel", type=Path)
parser.add_argument("installed_disk", type=Path)
parser.add_argument("--sound16", action="store_true")
parser.add_argument("--usb-keyboard", action="store_true",
                    help="Use USB input for modern kernels without PS/2 input")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root/"generated-artifacts/mario-fm-test"
out.mkdir(parents=True, exist_ok=True)
tag = ("sb16" if args.sound16 else "sb2")+"-fm"
wav = out/(tag+".wav")
symbols = {p[2]: int(p[0], 16) for line in
           subprocess.check_output(["nm", str(args.kernel.resolve())], text=True).splitlines()
           if len(p := line.split()) == 3}
with tempfile.TemporaryDirectory(prefix="mario-fm-") as folder:
    tmp = Path(folder)
    disk = tmp/"native.img"
    shutil.copyfile(args.installed_disk, disk)
    input_devices=("-device","qemu-xhci,id=usb",
                   "-device","usb-kbd,bus=usb.0,port=2") if args.usb_keyboard else ()
    g = Guest(args.kernel.resolve(), None, False, tmp, ("-m","256",*input_devices,
        "-drive", f"file={disk},if=ide,index=1,format=raw",
        "-audiodev", f"wav,id=audio,path={wav},out.frequency=48000",
        "-device", "ich9-intel-hda", "-device", "hda-duplex,audiodev=audio"))

    def select(title, text):
        deadline=time.monotonic()+15
        while True:
            screen=g.screen()
            if title in screen and "=>" in screen:
                break
            assert time.monotonic()<deadline, "Missing SETUP selection:\n"+screen
            time.sleep(.1)
        rows = screen.splitlines()
        current = next(i for i, row in enumerate(rows) if "=>" in row)
        target = next(i for i, row in enumerate(rows) if text in row)
        for _ in range(abs(current-target)):
            g.key("down" if current < target else "up")
        g.key("spc")
        assert "√ "+text in g.screen(), "SETUP did not select "+text
        g.key("ret")
        time.sleep(.3)

    def read(name, size):
        path = tmp/"state.bin"
        g.command("pmemsave", {"val": symbols[name], "size": size, "filename": str(path)})
        return path.read_bytes()

    try:
        g.wait("Welcome to inteiliDOS");g.wait("> ")
        shell(g, "ms-dos /sound16" if args.sound16 else "ms-dos /sound2")
        g.wait("MS-DOS 6.22 -", 180);g.wait("C:\\>")
        shell(g, "set")
        g.wait("BLASTER=A220 I5 D1")
        shell(g, "cd \\mario");shell(g, "setup")
        g.wait("Press any key to begin");g.key("ret")
        select("Please select the video mode", "VGA")
        select("Please select the music card", "Sound Blaster")
        select("Please select the digital voice card", "Sound Blaster")
        g.wait("Please select the options")
        # SETUP initially checks all three; explicitly enforce the desired state.
        for i, text in enumerate(("Music", "Sound Effects", "Digital Voice")):
            row = next(row for row in g.screen().splitlines() if text in row)
            if "√" not in row:
                g.key("spc")
            if i < 2:
                g.key("down")
        g.key("ret");g.wait("speed of your computer");g.key("ret")
        g.wait("Setup successful!")
        shell(g, "mario")
        active = music = False
        started=time.monotonic()
        deadline = started+40
        next_key=started+5
        while time.monotonic() < deadline:
            # ILP32 sound state: speaker/active and the current digital samples.
            # FM has its own chip and is not counted as PCM here.
            state = read("s", 68)
            active |= bool(state[59] and state[57] and any(state[64:68]))
            music |= bool(read("f", 64)[53])
            if active and music:
                time.sleep(3)
                break
            if time.monotonic()>=next_key:
                g.key("spc")
                next_key+=7
            time.sleep(.05)
        g.command("screendump", {"filename": str(out/(tag+".ppm"))})
        if not (active and music):
            print("Mario speech verification incomplete:",
                  "digital_voice=",active,"FM=",music,flush=True)
            print("Last DSP state:",read("s",68).hex(),flush=True)
            print(g.screen(),flush=True)
        assert active, "Mario did not play digital voice; inspect saved screen and DSP state"
        assert music, "Mario did not program FM music"
        g.key("f8");g.wait("Returned from MS-DOS")
        assert read("s", 68)[8:12] == b"\0"*4, "Sound device survived F8"
        assert read("f", 64)[:4] == b"\0"*4, "FM survived F8"
    finally:
        if g.proc.poll() is None:
            g.command("quit")
            try:
                g.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                g.proc.kill();g.proc.communicate()
                raise
        g.sock.close()
with wave.open(str(wav), "rb") as f:
    values = array.array("h", f.readframes(f.getnframes()))
    assert sum(abs(x) > 100 for x in values) > 48000, "Silent Mario recording"
print("PASS: Mario SETUP-selected music/voice, native HDA recording, F8; "+tag)
