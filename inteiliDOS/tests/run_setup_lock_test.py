#!/usr/bin/env python3
"""Install on a disposable HDD; SETUP is blocked on real cold HDD boots."""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile
from run_launchpad_dos_test import Guest
from run_msdos622_test import shell

def main():
    kernel=Path(sys.argv[1]).resolve()
    symbols=subprocess.check_output(["nm","-n",str(kernel)],text=True)
    address=int(next(s.split()[0] for s in symbols.splitlines()
                     if s.split()[-1]=="setup_install_state"),16)
    output=Path("generated-artifacts/setup-lock").resolve()
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="setup-lock-") as folder:
        t=Path(folder);disk=t/"target.img";other=t/"other.img"
        for p in (disk,other):
            with p.open("wb") as f:f.truncate(64*1024**2)
        other_hash=hashlib.sha256(other.read_bytes()).digest()
        args=("-drive",f"file={disk},if=ide,index=0,format=raw",
              "-drive",f"file={other},if=ide,index=1,format=raw")
        def guest(name,kernel_path=kernel):
            d=t/name;d.mkdir()
            return Guest(kernel_path,None,False,d,args+("-boot","order=c,strict=on"))
        g=guest("installer")
        try:
            g.wait("Welcome to inteiliDOS");g.wait("> ")
            shell(g,"setup");g.wait("Setup Wizard")
            g.key("esc");g.wait("> ")  # cancellation does not lock anything
            shell(g,"setup");g.wait("Setup Wizard");g.key("ret")
            g.wait("Choose an installation mode");g.key("down");g.key("ret")
            g.wait("Select the target installation drive");g.key("ret")
            g.wait("Press ENTER to begin installation");g.key("ret")
            g.wait("Installation successful",timeout=180)
            g.key("spc");g.wait("> ")
            # The running installation-media copy stays usable.
            shell(g,"setup");g.wait("Setup Wizard");g.key("esc");g.wait("> ")
        finally:g.close()
        data=disk.read_bytes()
        start=struct.unpack_from("<I",data,454)[0]
        offset=(start+1)*512+address-0x100000
        assert struct.unpack_from("<I",data,offset)[0]==0x4c4f434b
        assert hashlib.sha256(other.read_bytes()).digest()==other_hash
        installed_hash=hashlib.sha256(data).digest()
        # No -kernel or CD: execute the actual MBR, VBR and installed kernel.
        for name in ("cold","second-cold"):
            g=guest(name,None)
            try:
                g.wait("Welcome to inteiliDOS",timeout=60);g.wait("> ")
                for cmd in ("setup","install-os"):
                    shell(g,cmd);g.wait("SETUP is disabled on this installed HDD.")
                    g.wait("Boot inteiliDOS installation media");g.wait("> ")
                    assert "Setup Wizard" not in g.screen()
                g.command("screendump",{"filename":str(output/"disabled.ppm")})
                shell(g,"hello");g.wait("Hello!")
            finally:g.close()
            assert hashlib.sha256(disk.read_bytes()).digest()==installed_hash
            assert hashlib.sha256(other.read_bytes()).digest()==other_hash
        # Fresh installer media can still open SETUP beside the installed HDD.
        g=guest("recovery-media")
        try:
            g.wait("Welcome to inteiliDOS");g.wait("> ")
            shell(g,"setup");g.wait("Setup Wizard");g.key("esc")
        finally:g.close()
        assert hashlib.sha256(disk.read_bytes()).digest()==installed_hash
    print("PASS: cancel/retry, successful target-only installation lock, two actual HDD cold boots, SETUP/INSTALL-OS refusals without writes, fresh installation media remains usable")

if __name__=="__main__":main()
