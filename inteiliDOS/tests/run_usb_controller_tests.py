#!/usr/bin/env python3
"""Host USB controller models; no privileged I/O."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="usb-controllers-") as folder:
    for name in ("usb_xhci_init_test","usb_ehci_init_test","usb_bot_test","usb_transport_test"):
        out=Path(folder)/name
        extra=[] if name=="usb_transport_test" else [str(root/"kernel/usb_bot.c")]
        subprocess.run(["gcc","-std=c11","-O1","-Wall","-Wextra","-Werror",
                        str(root/"tests"/(name+".c")),*extra,
                        "-o",str(out)],check=True)
        subprocess.run([str(out)],check=True)
