#!/usr/bin/env python3
"""Copy user-supplied DOS disks into a separate DOS media CD tree unchanged.

Accept Disk1.img, Disk2.img, Disk3.img, Suppdisk.img (or upload timestamp
suffixes), or already named D6221.IMG etc. No downloading DOS binaries.
"""
from pathlib import Path
import shutil
import struct
import sys

def prepare(source, output):
    source, output = Path(source), Path(output)
    matches = []
    for stem, target in zip(("Disk1", "Disk2", "Disk3", "Suppdisk"),
                            ("D6221.IMG", "D6222.IMG", "D6223.IMG", "D622S.IMG")):
        choices = sorted(p for p in source.glob("*") if p.is_file() and
                         (p.name.lower() == target.lower() or
                          p.name.lower().startswith(stem.lower()) and
                          p.suffix.lower() == ".img"))
        if len(choices) != 1:
            raise ValueError(f"Expected exactly one {stem}.img in {source}")
        data = choices[0].read_bytes()
        if (len(data) != 1474560 or data[510:512] != b"\x55\xaa" or
            struct.unpack_from("<H", data, 11)[0] != 512 or data[13] != 1 or
            struct.unpack_from("<H", data, 17)[0] != 224 or
            struct.unpack_from("<H", data, 22)[0] != 9):
            raise ValueError(f"Not a standard 1.44 MB FAT12 DOS disk: {choices[0]}")
        matches.append((choices[0], target))
    root = matches[0][0].read_bytes()[19*512:33*512]
    entries = {root[i:i+11] for i in range(0, len(root), 32)}
    if not {b"IO      SYS", b"MSDOS   SYS", b"COMMAND COM"} <= entries:
        raise ValueError("Disk 1 is missing DOS boot files")
    output.mkdir(parents=True, exist_ok=True)
    for image, target in matches:
        shutil.copyfile(image, output/target)
    print("Prepared all four user-supplied DOS disks.")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: prepare_msdos622.py <disk folder> <ISO folder>")
    prepare(*sys.argv[1:])
