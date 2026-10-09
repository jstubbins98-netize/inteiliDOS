#!/usr/bin/env python3
"""Boot the complete OS, use LaunchPad, run COM/MZ, and return to the shell.

Usage: python3 tests/run_launchpad_dos_test.py [/tmp/test-build/inteilidOS.elf]
With no argument, builds a disposable test-profile kernel first.
"""
from pathlib import Path
import json
import socket
import subprocess
import sys
import tempfile
import time
from build_test_kernel import build
from run_dos_tests import fat12_image, run, TESTS


class Guest:
    def __init__(self, kernel, media, floppy, folder):
        self.folder = folder
        self.snapshot = folder/"vga.bin"
        qmp = folder/"qmp.sock"
        qmp.unlink(missing_ok=True)
        cmd = ["qemu-system-i386", "-nodefaults", "-device", "VGA",
               "-machine", "pc", "-cpu", "486", "-m", "16",
               "-kernel", str(kernel), "-display", "none", "-no-reboot",
               "-qmp", "unix:"+str(qmp)+",server=on,wait=off"]
        # Keep default emulated VGA and PS/2; do not introduce a default CD
        # when testing floppy-only LaunchPad source selection.
        cmd += ["-drive", "file="+str(media)+(",if=floppy,format=raw" if floppy
                                             else ",if=ide,index=0,media=cdrom")]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.sock = socket.socket(socket.AF_UNIX)
        deadline = time.monotonic()+10
        while True:
            try:
                self.sock.connect(str(qmp))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic()>deadline: raise
                time.sleep(0.05)
        self.io = self.sock.makefile("rwb", buffering=0)
        json.loads(self.io.readline())
        try:
            self.command("qmp_capabilities")
        except (ConnectionResetError, json.JSONDecodeError):
            self.proc.kill()
            _, error = self.proc.communicate()
            raise AssertionError("QEMU failed at startup: "+error.decode()) from None

    def command(self, command, arguments=None):
        self.io.write((json.dumps({"execute": command, "arguments": arguments or {}})+"\n").encode())
        while True:
            response = json.loads(self.io.readline())
            if "return" in response: return response["return"]
            if "error" in response: raise AssertionError(response)

    def key(self, code):
        self.command("send-key", {"keys": [{"type": "qcode", "data": code}], "hold-time": 50})
        time.sleep(0.12)

    def text(self, text):
        for c in text:
            self.key("ret" if c == "\n" else c)

    def screen(self):
        self.command("pmemsave", {"val": 0xB8000, "size": 4000, "filename": str(self.snapshot)})
        chars = self.snapshot.read_bytes()[::2].decode("cp437")
        return "\n".join(chars[i:i+80] for i in range(0, 2000, 80))

    def wait(self, text, timeout=30):
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            screen = self.screen()
            if text in screen: return screen
            if self.proc.poll() is not None: break
            time.sleep(0.1)
        raise AssertionError(f"Missing {text!r}:\n{self.screen()}")

    def close(self):
        self.sock.close()
        self.proc.kill()
        self.proc.communicate()


def main():
    with tempfile.TemporaryDirectory(prefix="inteilidos-launchpad-") as folder:
        tmp = Path(folder)
        kernel = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else build(tmp/"build")
        disc = tmp/"disc"
        disc.mkdir()
        files = {"DATA.TXT": b"WXYZ"}
        for case, name in ((0, "DEMO.COM"), (1, "DEMO.EXE")):
            run("nasm", "-f", "bin", "-DCASE="+str(case), TESTS/"dos_fixture.asm",
                "-o", disc/name)
            files[name] = (disc/name).read_bytes()
        (disc/"DATA.TXT").write_bytes(files["DATA.TXT"])
        run("xorriso", "-as", "mkisofs", "-quiet", "-iso-level", "1", "-o", tmp/"test.iso", disc)
        (tmp/"test.img").write_bytes(fat12_image(files))
        for floppy in (False, True):
            guest = Guest(kernel, tmp/("test.img" if floppy else "test.iso"), floppy, tmp)
            try:
                guest.wait("Welcome to inteiliDOS")
                guest.wait("> ")
                guest.text("program\n")
                guest.wait("DEMO.COM")
                # Root directory order: DATA.TXT, DEMO.COM, DEMO.EXE.
                guest.key("down")
                guest.key("ret")
                guest.wait("DOS program: v86 compatibility")
                guest.key("ret")
                guest.wait("DOS program ended (exit code 42)")
                guest.key("ret")
                guest.wait("DEMO.COM")
                guest.key("down")
                guest.key("ret")
                guest.wait("DOS program: v86 compatibility")
                guest.key("ret")
                guest.wait("DOS program ended (exit code 42)")
                guest.key("ret")
                guest.wait("DEMO.EXE")
                guest.key("q")
                guest.wait("> ")
                guest.text("help\n")
                guest.wait("PROGRAM")
                print(f"PASS: complete OS -> LaunchPad -> COM/MZ on "
                      f"{'floppy' if floppy else 'CD-ROM'} -> shell", flush=True)
            finally:
                guest.close()


if __name__ == "__main__":
    main()
