#!/usr/bin/env python3
"""Host PCM and FM tests; optional address/undefined-behavior sanitizers."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
env = dict(os.environ)
env["ASAN_OPTIONS"] = "detect_leaks=0"
with tempfile.TemporaryDirectory(prefix="dos-fm-") as folder:
    for name in ("msdos_sound_test", "msdos_sound16_test", "msdos_fm_test"):
        target = Path(folder)/name
        command = ["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        command += [str(root/"tests"/(name+".c")),
                    *[str(root/"kernel/msdos"/p) for p in
                      ("sound.c", "fm.c", "opl3/opl3.c")], "-o", str(target)]
        subprocess.run(command, check=True)
        subprocess.run([str(target)], check=True, env=env)
