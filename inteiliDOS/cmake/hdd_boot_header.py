#!/usr/bin/env python3
"""Generate installer templates from their canonical real-mode NASM sources."""
from pathlib import Path
import subprocess
import sys
import tempfile

def templates(nasm="nasm"):
    root=Path(__file__).resolve().parents[1]
    data=b""
    with tempfile.TemporaryDirectory(prefix="hdd-bootstrap-") as folder:
        for name in ("hdd_mbr","hdd_vbr"):
            out=Path(folder)/(name+".bin")
            subprocess.run([nasm,"-f","bin",str(root/"boot"/(name+".asm")),
                            "-o",str(out)],check=True)
            image=out.read_bytes()
            assert len(image)==512 and image[510:]==b"\x55\xaa"
            data+=image
    return data

def generate(output,nasm="nasm"):
    data=templates(nasm)
    path=Path(output);path.parent.mkdir(parents=True,exist_ok=True)
    rows=[", ".join(f"0x{v:02X}" for v in data[i:i+16]) for i in range(0,len(data),16)]
    path.write_text("#ifndef HDD_BOOT_DATA_H\n#define HDD_BOOT_DATA_H\n"
                    "#include <stdint.h>\nstatic const uint8_t hdd_boot_data[1024]={\n"+
                    ",\n".join(rows)+"\n};\n#endif\n")

if __name__=="__main__":
    generate(sys.argv[1],sys.argv[2] if len(sys.argv)>2 else "nasm")
