#!/usr/bin/env python3
"""Host regression for saved DOS HDD startup without optical hardware."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        raise SystemExit("A host C compiler is required.")
    strip = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
    with tempfile.TemporaryDirectory(prefix="msdos-reopen-") as folder:
        binary = Path(folder) / "reopen"
        subprocess.run([cc, "-O1", "-ffunction-sections", "-fdata-sections",
                        str(Path(__file__).with_name("msdos_reopen_test.c")),
                        strip, "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
