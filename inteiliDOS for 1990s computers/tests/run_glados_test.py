#!/usr/bin/env python3
"""Boot the OS and verify GLADOS end-to-end, including the entire QBasic melody."""
from array import array
from collections import Counter
from pathlib import Path
import argparse
import re
import json
import shutil
import subprocess
import sys
import tempfile
import time
import wave
from build_test_kernel import build
from run_launchpad_dos_test import Guest
ROOT=Path(__file__).resolve().parents[1]
EGG=ROOT/"want_you_gone_easter_egg"
sys.path.insert(0,str(EGG/"tools"))
from qbasic_music import convert


def phrase_starts():
    """Measure musical phrases, ignoring BASIC display and typing routines."""
    source=(EGG/"assets/melody.bas").read_text(encoding="cp437")
    sections=(
        ("'Verse 1", "MBAB>C+"),
        ("'Epic chorus\n", "MBL64O2B->"),
        ("'Now for verse TWO", "MBAB>C+"),
        ("'Epic chorus NUMERO DOS", "MBL64O2B->"),
        ("'Verse three", "MBAB>C+"),
        ("'Epic chorus THREE", "MBL64O2B->"),
    )
    starts=[]
    for marker,opening in sections:
        section=source.index(marker)
        phrase=re.search(r'^\s*PLAY "'+re.escape(opening),
                         source[section:],re.M)
        assert phrase, "Missing musical phrase: "+marker
        starts.append(convert(source[:section+phrase.start()])[1])
    assert starts==[5400,25200,47100,66900,88200,108000]
    return starts


def stanza_rows(text):
    rows=re.findall(r'\{("(?:[^"\\]|\\.)*"),(\d+)u,(\d+)u,(\d+)u,(\d+)u,(\d+)u\}',text)
    return [(json.loads(literal),*(int(n) for n in numbers)) for literal,*numbers in rows]


def expected_cells(stanza):
    text,start,typing,length,row,col=stanza
    screen=bytearray(b" \x60"*2000)
    origin=col
    for char in text:
        if char=="\n":
            row+=1;col=origin
        else:
            assert 0<=row<25 and 0<=col<80,"Lyric outside VGA screen"
            screen[2*(row*80+col)]=ord(char);col+=1
    return bytes(screen)


def asset_checks(folder):
    maintained=(EGG/"assets/stanzas.inc").read_text()
    stanzas=stanza_rows(maintained)
    assert len(stanzas)==6,"Expected all six maintained stanzas"
    assert [s[1] for s in stanzas]==phrase_starts(), "Stanzas precede PLAY phrases"
    ends=[s[1] for s in stanzas[1:]]+[136806]
    for stanza,end in zip(stanzas,ends):
        assert stanza[2]>0, "Invalid typing duration"
        assert end-stanza[1]-stanza[2]==2500, "Completed stanza needs 2.5 seconds to read"
    for stanza in stanzas:
        assert len(stanza[0].encode("ascii"))==stanza[3],"Stale stanza length"
        assert "\0" not in stanza[0],"Embedded lyric terminator"
        expected_cells(stanza)
    copy=folder/"egg"
    shutil.copytree(EGG/"assets",copy/"assets")
    shutil.copytree(EGG/"tools",copy/"tools")
    subprocess.run([sys.executable,str(copy/"tools/generate.py")],check=True)
    subprocess.run([sys.executable,str(copy/"tools/generate.py"),"--check"],check=True)
    assert (copy/"assets/stanzas.inc").read_bytes()==(EGG/"assets/stanzas.inc").read_bytes()
    generated=(copy/"data.h").read_bytes()
    assert generated==(EGG/"data.h").read_bytes(),"Regeneration changed embedded assets"
    assert stanza_rows(generated.decode())==stanzas,"Regeneration changed stanza metadata"
    # Bad maintained counts must fail explicitly, rather than re-embed the bug.
    stale=re.sub(r'(\d+u,\d+u,)\d+u,',r'\g<1>999u,',maintained,count=1)
    (copy/"assets/stanzas.inc").write_text(stale)
    result=subprocess.run([sys.executable,str(copy/"tools/generate.py")],capture_output=True,text=True)
    assert result.returncode and "stored length 999" in result.stderr
    assert (copy/"data.h").read_bytes()==generated,"Failed generation overwrote data"

    # Compile the actual player typing loop, not a Python reimplementation.
    source=(EGG/"glados.c").read_text()
    loop=re.search(r'            while \(printed<wanted.*?\n            \}',source,re.S).group()
    harness=folder/"typing.c"
    harness.write_text("""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "data.h"
#define AMBER 0x6000u
static uint16_t SCREEN[2000], expected[2000];
static void check(glados_stanza stanza) {
    char guarded[512];
    unsigned length=strlen(stanza.text);
    memset(guarded, '!', sizeof guarded);
    memcpy(guarded, stanza.text, length+1);
    stanza.text=guarded;
    const glados_stanza *s=&stanza;
    unsigned printed=0,row=s->row,col=s->col;
    for (unsigned i=0;i<2000;i++) SCREEN[i]=expected[i]=AMBER|' ';
    for (unsigned wanted=0;wanted<=s->len+20;wanted++) {
""" + loop + """
        unsigned count=wanted<length ? wanted : length;
        /* Build the expected prefix independently for each frame. */
        unsigned erow=s->row,ecol=s->col;
        for (unsigned i=0;i<count;i++) {
            char c=s->text[i];
            if (c=='\\n') { erow++;ecol=s->col; }
            else expected[erow*80+ecol++]=AMBER|(uint8_t)c;
        }
        assert(printed==count);
        assert(memcmp(SCREEN,expected,sizeof SCREEN)==0);
    }
}
int main(void) {
    for (unsigned i=0;i<sizeof glados_stanzas/sizeof glados_stanzas[0];i++) {
        glados_stanza s=glados_stanzas[i];
        check(s);
        s.len+=10;check(s); /* stale heading counts, with poison after NUL */
    }
    glados_stanza empty={"",0,1,10,0,0};check(empty);
}
""")
    executable=folder/"typing"
    subprocess.run(["gcc","-std=c11","-Wall","-Wextra","-I"+str(EGG),
                    str(harness),"-o",str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
    print("PASS: six exact stanza lengths, regeneration preservation, stale-count rejection,")
    print("player typing loop stops at NUL with oversized counts and empty text",flush=True)
    return stanzas


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


def timed_cells(guest,folder,ticks_address):
    """Use the guest's music clock, not QEMU's variable wall-clock speed."""
    guest.command("stop")
    try:
        file=folder/"ticks.bin"
        guest.command("pmemsave",{"val":ticks_address,"size":4,"filename":str(file)})
        ticks=int.from_bytes(file.read_bytes(),"little")
        return ticks,cells(guest,folder)
    finally:
        guest.command("cont")


def wait_frame(guest,folder,ticks_address,predicate,timeout=45):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        tick,frame=timed_cells(guest,folder,ticks_address)
        if predicate(tick,frame): return tick,frame
        time.sleep(.03)
    raise AssertionError("Timed lyric frame did not arrive")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel",nargs="?")
    parser.add_argument("--assets-only",action="store_true")
    parser.add_argument("--screenshot",type=Path,help="Save the final VGA page as a PPM image")
    args=parser.parse_args()
    subprocess.run([sys.executable,str(EGG/"tools/generate.py"),"--check"],check=True)
    with tempfile.TemporaryDirectory(prefix="glados-test-") as name:
        folder=Path(name)
        stanzas=asset_checks(folder)
        if args.assets_only: return
        kernel=Path(args.kernel).resolve() if args.kernel else build(folder/"os")
        symbols=subprocess.check_output(["nm","-n",str(kernel)],text=True)
        clocks=re.findall(r"^([0-9a-fA-F]+) [bB] ticks$",symbols,re.M)
        # DOS has its own static ticks too. Resolve the native clock's load
        # through the accessor instead of relying on duplicate local names.
        accessor=subprocess.check_output(
            ["objdump","-d","--disassemble=timer_get_ticks",str(kernel)],text=True)
        load=re.search(r"\bmov\s+0x([0-9a-fA-F]+),%eax",accessor)
        assert load, "Cannot identify the native melody clock load"
        ticks_address=int(load[1],16)
        assert ticks_address in [int(clock,16) for clock in clocks]
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
            guest.key("caps_lock");guest.text("glados")
            before,_=timed_cells(guest,folder,ticks_address)
            guest.command("send-key",{"keys":[{"type":"qcode","data":"ret"}],"hold-time":50})
            after,_=wait_frame(guest,folder,ticks_address,
                              lambda tick,frame: set(frame[1::2])=={0x60})
            assert after-before<150, "Playback start observation was too slow"
            origin=(before+after)//2
            guest.key("caps_lock")
            amber(guest,folder)
            offset=wav.stat().st_size
            previous=None
            previous_frame=b" \x60"*2000
            for index,stanza in enumerate(stanzas):
                page=stanza[0].strip()
                heading=page.splitlines()[0]
                start=stanza[1]
                _,frame=wait_frame(guest,folder,ticks_address,
                                  lambda tick,frame: tick-origin>=start-250)
                assert frame==previous_frame, "Stanza changed before its musical phrase"
                # A leading newline is invisible. The first visible glyph is
                # scheduled one character interval after the musical cue.
                first_cell=2*((stanza[4]+1)*80+stanza[5])
                visible=start+(stanza[2]+stanza[3]-1)//stanza[3]
                tick,frame=wait_frame(guest,folder,ticks_address,
                    lambda tick,frame: frame[first_cell]==ord(page[0])
                        and frame!=previous_frame)
                assert abs(tick-origin-visible)<150, \
                    f"Stanza {index+1} off phrase: {tick-origin} ms, expected {visible} ms"
                assert set(frame[1::2])=={0x60}
                if previous: assert previous not in frame[::2].decode("cp437")
                print(f"PASS: phrase {index+1} at {start} ms; first glyph observed "
                      f"at {tick-origin} ms (expected {visible} ms)",flush=True)
                guest.wait(heading,timeout=45)
                wanted=Counter(page.splitlines());deadline=time.monotonic()+45
                while True:
                    screen=guest.screen()
                    if all(screen.count(line)>=n for line,n in wanted.items()): break
                    assert time.monotonic()<deadline,"Stanza did not finish: "+heading
                    time.sleep(.1)
                # Wait beyond the last scheduled character: the former oversized
                # count could print garbage after all expected lines appeared.
                time.sleep(.5)
                assert cells(guest,folder)==expected_cells(stanza), \
                    "Trailing garbage or changed lyric placement: "+heading
                amber(guest,folder)
                if previous: assert previous not in screen,"Previous stanza was not cleared"
                previous=heading
                previous_frame=expected_cells(stanza)
                end=stanzas[index+1][1] if index<5 else 136806
                _,frame=wait_frame(guest,folder,ticks_address,
                                  lambda tick,frame: tick-origin>=end-250)
                assert frame==previous_frame, "Completed stanza was not retained for reading"
                if index==5:
                    guest.command("screendump",{"filename":str(folder/"final-stanza.ppm")})
                    # Optional copy for inspecting the actual bare-metal VGA
                    # display; no web preview or authentication bypass needed.
                    if args.screenshot:
                        shutil.copyfile(folder/"final-stanza.ppm",args.screenshot)
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
            print("stanza replacement, QBasic melody waveform, natural completion and Esc/shell restoration")
        finally: guest.close()


if __name__=="__main__": main()
