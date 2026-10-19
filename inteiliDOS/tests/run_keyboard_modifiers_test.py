#!/usr/bin/env python3
"""Host-only keyboard regression: actual drivers with hardware sections unused."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="keyboard-modifiers-") as folder:
    for name in ("keyboard_modifiers_test","usb_report_test"):
        binary=Path(folder)/name
        subprocess.run(["gcc","-std=gnu11","-O1","-Wall","-Wextra",
                        "-ffunction-sections","-fdata-sections","-Wno-pointer-to-int-cast",
                        str(root/"tests"/(name+".c")),str(root/"kernel/usb.c"),
                        "-Wl,--gc-sections","-o",str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
