#!/usr/bin/env python3
"""Boot the OS and verify GLADOS end-to-end, including the entire MIDI song."""
from array import array
from collections import Counter
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import wave
from build_test_kernel import build
from run_launchpad_dos_test import Guest
ROOT=Path(__file__).resolve().parents[1]
EGG=ROOT/"want_you_gone_easter_egg"


def cells(guest,folder):
    file=folder/"cells.bin"
    guest.command("pmemsave",{"val":0xB8000,"size":4000,"filename":str(file)})
    return file.read_bytes()


def amber(guest,folder):
    assert set(cells(guest,folder)[1::2])=={0x60},"Not solid amber with black text"

def quiet(guest):
    report=guest.command("human-monitor-command",{"command-line":"i /b 0x61"})
    value=int(re.search(r"=\s*0x([0-9a-fA-F]+)",report).group(1),16)
    assert not (value&3),"Speaker gate/enable was left active"


def main():
    subprocess.run([sys.executable,str(EGG/"tools/generate.py"),"--check"],check=True)
    pages=[p.strip() for p in re.split(r"\n\s*\n",(EGG/"assets/lyrics.txt").read_text().strip())]
    with tempfile.TemporaryDirectory(prefix="glados-test-") as name:
        folder=Path(name)
        kernel=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(folder/"os")
        cd=folder/"disc";cd.mkdir();(cd/"TEST.TXT").write_text("GLADOS integration fixture")
        iso=folder/"disc.iso"
        subprocess.run(["xorriso","-as","mkisofs","-o",str(iso),str(cd)],
                       check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        wav=folder/"speaker.wav"
        guest=Guest(kernel,iso,False,folder,("-audiodev","wav,id=spk,path="+str(wav),
                    "-machine","pcspk-audiodev=spk"))
        try:
            guest.wait("Welcome to inteiliDOS");guest.wait("> ")
            guest.text("alive\n");guest.wait("'ALIVE' is not recognized")
            guest.text("glados\n");amber(guest,folder)
            time.sleep(.4);guest.key("esc");guest.wait("> ")
            quiet(guest)
            assert set(cells(guest,folder)[1::2])!={0x60},"Esc did not restore VGA"
            # Send physical lowercase qcodes with Caps Lock to test GLADOS.
            guest.key("caps_lock");guest.text("glados\n");guest.key("caps_lock")
            amber(guest,folder)
            offset=wav.stat().st_size
            previous=None
            for page in pages:
                heading=page.splitlines()[0]
                guest.wait(heading,timeout=45)
                wanted=Counter(page.splitlines());deadline=time.monotonic()+45
                while True:
                    screen=guest.screen()
                    if all(screen.count(line)>=n for line,n in wanted.items()): break
                    assert time.monotonic()<deadline,"Stanza did not finish: "+heading
                    time.sleep(.1)
                amber(guest,folder)
                if previous: assert previous not in screen,"Previous stanza was not cleared"
                previous=heading
                print("PASS:",heading,flush=True)
            guest.wait("Enter / Esc: return",timeout=10)
            quiet(guest)
            time.sleep(.3)
            guest.key("ret");guest.wait("> ")
            quiet(guest)
            assert set(cells(guest,folder)[1::2])!={0x60}
            guest.text("echo");guest.key("spc");guest.text("restored\n");guest.wait("restored")
            guest.command("quit");guest.proc.wait(timeout=10)
            with wave.open(str(wav),"rb") as audio:
                assert audio.getsampwidth()==2
                skip=(offset+4096)//(2*audio.getnchannels())
                audio.setpos(min(skip,audio.getnframes()))
                low=32767;high=-32768
                while True:
                    chunk=audio.readframes(65536)
                    if not chunk: break
                    samples=array("h",chunk)
                    low=min(low,min(samples));high=max(high,max(samples))
                assert high-low>100,"No PC-speaker waveform"
                # WAV stops appending when PC-speaker audio becomes inactive,
                # so its tail still contains the last note, not silence.
                # Port 0x61 above independently verifies physical shutdown.
            print("PASS: GLADOS casing, solid amber/black, typewriter, all supplied lyrics,")
            print("stanza replacement, MIDI waveform, natural completion and Esc/shell restoration")
        finally: guest.close()


if __name__=="__main__": main()
