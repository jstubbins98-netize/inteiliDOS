#!/usr/bin/env python3
"""Native disk layout regressions; all fixtures are disposable.
Pass a built kernel path to also run the real installer/BASIC save in QEMU.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent

def run(*args):
    subprocess.run([str(a) for a in args], check=True)

def main():
    with tempfile.TemporaryDirectory(prefix="native-layout-") as folder:
        tmp=Path(folder)
        run("gcc","-std=gnu11","-Wall","-Wextra","-Werror",
            "-fsanitize=address,undefined","-g",HERE/"fs_layout_test.c",
            "-o",tmp/"fs-layout")
        run(tmp/"fs-layout")
        run("gcc","-std=gnu11","-Wall","-Wextra","-Werror",
            "-fsanitize=address,undefined","-g",HERE/"fs_volume_test.c",
            "-o",tmp/"fs-volume")
        run(tmp/"fs-volume")
        run("gcc","-std=gnu11","-Wall","-Wextra","-Werror",
            "-fsanitize=address,undefined","-g",
            "-ffunction-sections","-fdata-sections",HERE/"msdos_space_test.c",
            "-Wl,--gc-sections","-o",tmp/"msdos-space")
        run(tmp/"msdos-space")
        # setup.c contains a pre-existing unused helper and 32-bit RAM casts;
        # neither belongs to the host-side partition-preflight entry point.
        run("gcc","-std=gnu11","-Wall","-Wextra","-Werror",
            "-Wno-unused-function","-Wno-pointer-to-int-cast",
            "-ffunction-sections","-fdata-sections",HERE/"setup_layout_test.c",
            "-Wl,--gc-sections","-o",tmp/"setup-layout")
        run(tmp/"setup-layout")
    if len(sys.argv)>1:
        run(sys.executable,HERE/"run_setup_test.py",Path(sys.argv[1]).resolve())

if __name__=="__main__":
    main()
