#!/usr/bin/env python3
"""Exact BIOS modifier/key tests in real DOS, with PS/2 and USB-only hosts.
No DOS files are bundled; only disposable test media are written.
"""
from pathlib import Path
import sys
import tempfile
from build_test_kernel import ROOT, build, run
from run_launchpad_dos_test import Guest, fat12_image
from run_msdos622_test import prepare, shell, clear_dos
from run_msdos_external_media_test import native_disk, protected

# Independent BIOS expectations: qcodes, scan:ASCII, AH12 shift flags.
CASES = [
    (["shift","f1"],0x5400,0x0002),
    (["ctrl","f1"],0x5e00,0x0104),
    (["alt","f1"],0x6800,0x0208),
    (["shift","f5"],0x5800,0x0002),
    (["ctrl","f5"],0x6200,0x0104),
    (["alt","f5"],0x6c00,0x0208),
    (["shift","f9"],0x5c00,0x0002),
    (["ctrl","f9"],0x6600,0x0104),
    (["alt","f9"],0x7000,0x0208),
    (["shift","f10"],0x5d00,0x0002),
    (["ctrl","f10"],0x6700,0x0104),
    (["alt","f10"],0x7100,0x0208),
    (["shift","f11"],0x8700,0x0002),
    (["ctrl","f11"],0x8900,0x0104),
    (["alt","f11"],0x8b00,0x0208),
    (["shift_r","f12"],0x8800,0x0001),
    (["ctrl_r","f12"],0x8a00,0x0404),
    (["alt_r","f12"],0x8c00,0x0808),
    (["ctrl","shift","f1"],0x5e00,0x0106),
    (["alt","shift","f1"],0x6800,0x020a),
    (["ctrl","alt","f1"],0x5e00,0x030c),
    (["ctrl","alt","shift","f12"],0x8a00,0x030e),
    (["a"],0x1e61,0x0000),
    (["shift","a"],0x1e41,0x0002),
    (["ctrl","c"],0x2e03,0x0104),
    (["alt","d"],0x2000,0x0208),
    (["ctrl","h"],0x2308,0x0104),
    (["ctrl","i"],0x1709,0x0104),
    (["ctrl","m"],0x320d,0x0104),
    (["shift","semicolon"],0x273a,0x0002),
    (["shift","1"],0x0221,0x0002),
    (["shift","tab"],0x0f00,0x0002),
    (["ctrl","tab"],0x9400,0x0104),
    (["alt","tab"],0xa500,0x0208),
    (["ctrl","backspace"],0x0e7f,0x0104),
    (["ctrl","ret"],0x1c0a,0x0104),
    (["ctrl","2"],0x0300,0x0104),
    (["ctrl","bracket_left"],0x1a1b,0x0104),
    (["ctrl","backslash"],0x2b1c,0x0104),
    (["ctrl","bracket_right"],0x1b1d,0x0104),
    (["alt","1"],0x7800,0x0208),
    (["alt","ret"],0xa600,0x0208),
    (["ctrl","left"],0x7300,0x0104),
    (["alt","right"],0x9d00,0x0208),
    (["shift","up"],0x4800,0x0002),
]
# Modifier-only transitions: two held shifts, releasing one, left/right
# Ctrl+Alt, and releasing one side while the other remains held.
STATES = [
    ([("shift",True)],0x0002),
    ([("shift_r",True)],0x0003),
    ([("shift",False)],0x0001),
    ([("shift_r",False)],0x0000),
    ([("ctrl",True),("ctrl_r",True),("alt",True),("alt_r",True)],0x0f0c),
    ([("ctrl",False),("alt",False)],0x0c0c),
    ([("ctrl_r",False),("alt_r",False)],0x0000),
]
RESERVED_STATES = [
    ([("shift",True),("f6",True)],0x0002),
    ([("f6",False),("shift",False)],0x0000),
    ([("ctrl",True),("f7",True)],0x0104),
    ([("f7",False),("ctrl",False)],0x0000),
]
PHYSICAL = {
    "f1":0x3b,"f5":0x3f,"f9":0x43,"f10":0x44,"f11":0x57,"f12":0x58,
    "a":0x1e,"c":0x2e,"d":0x20,"h":0x23,"i":0x17,"m":0x32,
    "semicolon":0x27,"1":2,"2":3,"tab":0x0f,"backspace":0x0e,"ret":0x1c,
    "bracket_left":0x1a,"backslash":0x2b,"bracket_right":0x1b,
    "left":0x4b,"right":0x4d,"up":0x48,
}

def events(g, keys):
    g.command("input-send-event",{"events":[
        {"type":"key","data":{"down":down,"key":{"type":"qcode","data":key}}}
        for key,down in keys]})

def main():
    output=ROOT.parent/"generated-artifacts/msdos-modifiers"
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="msdos-modifiers-") as folder:
        tmp=Path(folder)
        ps2=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(tmp/"ps2-build")
        usb=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else build(tmp/"usb-build",modern=True)
        media=Path(sys.argv[3]).resolve() if len(sys.argv)>3 else ROOT.parent/"MS-DOS_disks"
        prepare(media,tmp/"disc")
        inc=tmp/"cases.inc"
        inc.write_text("\n".join(
            f"state {flags:#x},{900+i}" for i,(_,flags) in enumerate(RESERVED_STATES))+"\n"+
            "\n".join(
            f"case {word:#x},{flags:#x},{0x1000 if i%2 else 0},{i},{PHYSICAL[keys[-1]]:#x}"
            for i,(keys,word,flags) in enumerate(CASES))+"\n"+
            "\n".join(f"state {flags:#x},{i}" for i,(_,flags) in enumerate(STATES))+"\n")
        run("nasm","-f","bin",f'-DCASE_INCLUDE="{inc}"',
            ROOT/"tests/msdos_modifiers.asm","-o",tmp/"modkey.com")
        (tmp/"disc/MODKEY.IMG").write_bytes(fat12_image({"MODKEY.COM":(tmp/"modkey.com").read_bytes()}))
        run("xorriso","-as","mkisofs","-quiet","-iso-level","1","-o",tmp/"keys.iso",tmp/"disc")
        native=tmp/"native.img";native_disk(native);safe=protected(native)
        for label,kernel in (("ps2",ps2),("usb",usb)):
            session=tmp/label;session.mkdir()
            args=["-drive",f"file={native},if=ide,index=1,format=raw"]
            if label=="usb":
                args+=["-cpu","core2duo","-m","256","-device","qemu-xhci,id=usb",
                       "-device","usb-kbd,bus=usb.0"]
            g=Guest(kernel,tmp/"keys.iso",False,session,tuple(args))
            try:
                g.wait("Welcome to inteiliDOS");g.wait("> ")
                shell(g,"ms-dos");g.wait("MS-DOS 6.22 -",180);g.wait("C:\\>")
                g.key("f8");g.wait("Returned from MS-DOS")
                shell(g,"ms-dos /image MODKEY.IMG")
                g.wait("MS-DOS 6.22 -",90);g.wait("C:\\>")
                clear_dos(g);shell(g,"a:\\modkey.com")
                for i,(changes,flags) in enumerate(RESERVED_STATES):
                    g.wait(f"STATE CASE {900+i}!")
                    events(g,changes)
                    g.wait(f"STATE OK {900+i}!",10)
                for i,(keys,word,flags) in enumerate(CASES):
                    g.wait(f"KEY CASE {i}!")
                    events(g,[(key,True) for key in keys])
                    try:
                        g.wait(f"RELEASE CASE {i}!",10)
                    except AssertionError:
                        raise AssertionError(f"{label} case {i}: {keys}, BIOS {word:04x}, "
                                             f"flags {flags:04x}\n{g.screen()}") from None
                    events(g,[(key,False) for key in reversed(keys)])
                for i,(changes,flags) in enumerate(STATES):
                    g.wait(f"STATE CASE {i}!")
                    events(g,changes)
                    g.wait(f"STATE OK {i}!",10)
                g.wait("DOS MODIFIERS OK");g.wait("C:\\>")
                assert "DOS MODIFIERS FAILED" not in g.screen()
                g.command("screendump",{"filename":str(output/(label+".ppm"))})
                # Reserved controls remain host controls even when modified.
                events(g,[("alt",True),("f8",True)])
                g.wait("Returned from MS-DOS")
                events(g,[("f8",False),("alt",False)])
                shell(g,"hello");g.wait("Hello!")
                # Native F2 and F10 keep their character-only consumer path.
                shell(g,"iedit");g.wait("F2:Save")
                g.key("f2");g.wait("Save as");g.key("esc");g.wait("Save cancelled")
                g.key("f10");g.wait("C:\\>")
            finally:
                g.close()
            assert protected(native)==safe
            print(f"PASS {label}: {len(CASES)} exact BIOS key/held/released-state cases, "
                  "legacy/enhanced peek/read, modifier-only transitions, modified "
                  "F6/F7/F8, shell typing and native F2/F10",flush=True)

if __name__=="__main__":
    main()
