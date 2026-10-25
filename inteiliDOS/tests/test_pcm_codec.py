#!/usr/bin/env python3
"""Host regression for analog HDA codec readiness and session diagnostics."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pcm-codec-") as folder:
    tmp = Path(folder)
    (tmp/"config.h").write_text("#define CONFIG_ENABLE_PCI 1\n")
    subprocess.run([
        "cc", "-std=gnu11", "-O1", "-ffunction-sections", "-fdata-sections",
        "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
        "-fno-pie", "-no-pie",
        "-I"+str(tmp), str(root/"tests/pcm_codec_test.c"),
        str(root/"kernel/msdos/sound.c"),
        str(root/"kernel/msdos/fm.c"),
        str(root/"kernel/msdos/opl3/opl3.c"),
        "-Wl,--gc-sections", "-o", str(tmp/"test"),
    ], check=True)
    subprocess.run([str(tmp/"test")], check=True)
