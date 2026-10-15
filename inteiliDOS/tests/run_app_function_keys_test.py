#!/usr/bin/env python3
"""PS/2 function keys, native C: saves and unsaved-change protection.

Only a disposable native HDD fixture is modified.
"""
from pathlib import Path
import sys
import tempfile
import struct

from build_test_kernel import build
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell, type_text
from run_msdos_external_media_test import native_disk, protected


def main():
    output = Path("generated-artifacts/app-function-keys").resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="app-function-keys-") as folder:
        tmp = Path(folder)
        kernel = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(tmp/"build")
        disk = tmp/"native.img"
        native_disk(disk)
        safe = protected(disk)
        g = Guest(kernel, None, False, tmp,
                  ("-drive", f"file={disk},if=ide,index=1,format=raw"))
        try:
            g.wait("Welcome to inteiliDOS");g.wait("C:\\>")
            shell(g, "iedit");g.wait("F2:Save")
            g.wait("F4:DelLine")
            type_text(g, "ONLYLINE");g.key("f4");g.wait("Line cleared.")
            assert "ONLYLINE" not in g.screen()
            type_text(g, "FIRSTLINE\nMIDDLELINE\nLASTLINE")
            g.key("up");g.key("f4");g.wait("Line deleted.")
            assert "MIDDLELINE" not in g.screen()
            assert "FIRSTLINE" in g.screen() and "LASTLINE" in g.screen()
            g.key("f4");g.wait("Line deleted.")
            assert "LASTLINE" not in g.screen() and "FIRSTLINE" in g.screen()
            g.key("f4");g.wait("Line cleared.")
            assert "FIRSTLINE" not in g.screen()
            type_text(g, "Function key editor")
            g.key("f2");g.wait("Save as");g.key("esc")
            g.wait("Save cancelled")
            g.key("f10");g.wait("Unsaved changes!");g.key("esc")
            g.key("f2");g.wait("Save as")
            type_text(g, "FKEY.TXT\n");g.wait("Save to:")
            g.key("1");g.wait("Saved: C:\\FKEY.TXT")
            type_text(g, "UNSAVED")
            g.key("f3");g.wait("Unsaved changes!");g.key("esc")
            g.wait("Open cancelled")
            g.key("f3");g.wait("Unsaved changes!");g.key("f3")
            g.wait("Open (.txt)")
            type_text(g, "FKEY.TXT\n");g.wait("Load from:")
            g.key("1");g.wait("Opened: C:\\FKEY.TXT")
            assert "UNSAVED" not in g.screen()
            g.command("screendump", {"filename": str(output/"iedit.ppm")})
            g.key("f10");g.wait("C:\\>")

            shell(g, "sheets");g.wait("F10:Quit")
            # F2 during cell editing must save the typed value, not a stale cell.
            type_text(g, "123");g.key("f2");g.wait("Save as")
            type_text(g, "FKEY.CSV\n");g.wait("Save to:")
            g.key("1");g.wait("Saved: C:\\FKEY.CSV");g.key("spc")
            g.wait("[A1]: 123")
            g.key("ret");type_text(g, "4");g.key("f10")
            g.wait("Unsaved changes!");g.key("esc")
            g.wait("[A1]: 1234")
            g.key("f3");g.wait("Unsaved changes!");g.key("esc")
            g.wait("F10:Quit")
            g.key("f3");g.wait("Unsaved changes!");g.key("f3")
            g.wait("Open (.csv)")
            type_text(g, "FKEY.CSV\n");g.wait("Load from:")
            g.key("1");g.wait("Opened: C:\\FKEY.CSV");g.key("spc")
            g.wait("[A1]: 123")
            g.command("screendump", {"filename": str(output/"sheets.ppm")})
            g.key("f10");g.wait("C:\\>")

            # Repeated F10 must also allow deliberate discard in both apps.
            shell(g, "iedit");g.wait("F2:Save")
            type_text(g, "DISCARD")
            g.key("f10");g.wait("Unsaved changes!")
            g.key("f10");g.wait("C:\\>")
            shell(g, "sheets");g.wait("F10:Quit")
            type_text(g, "9")
            g.key("f10");g.wait("Unsaved changes!")
            g.key("f10");g.wait("C:\\>")
            shell(g, "hello");g.wait("Hello!")
        finally:
            g.close()
        assert protected(disk)==safe
        # Native shell TYPE is a simulated command; verify actual saved bytes
        # independently rather than treating its placeholder as a file read.
        data=disk.read_bytes()
        directory=data[2048*512:2049*512]
        files={}
        for offset in range(4,4+15*32,32):
            name,start,size=struct.unpack_from("<24sII",directory,offset)
            name=name.split(b"\0",1)[0].decode()
            if name:files[name]=data[start*512:start*512+size]
        assert files["FKEY.TXT"]==b"Function key editor\n"
        assert files["FKEY.CSV"]==b"Row,A,B,C,D,E,F,G\n1,123,,,,,,\n"
    print("PASS: iEdit F4 single/middle/last line deletion; PS/2 F2/F3/F10 in iEdit/Sheets; native C: save/load; "
          "active cell saved; cancel preserves unsaved work; confirmed load "
          "and quit; clean quit; persisted data and native files preserved")


if __name__=="__main__":
    main()
