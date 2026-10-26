#!/usr/bin/env python3
"""Format disposable USB image files only; never opens physical media."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="usb-format-") as folder:
    tmp = Path(folder)
    executable = tmp/"format-test"
    subprocess.run([
        "cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
        "-ffunction-sections", "-fdata-sections",
        str(root/"tests/usb_format_test.c"),
        str(root/"kernel/fatfs/ff.c"), str(root/"kernel/fatfs/ffunicode.c"),
        "-Wl,--gc-sections", "-o", str(executable),
    ], check=True)
    for filesystem, signature, offset, size in (
        (1,b"FAT  ",54,128*1024*1024),
        (2,b"FAT32",82,64*1024*1024),
        (3,b"EXFAT",3,128*1024*1024),
        (1,b"FAT  ",54,2*1024*1024*1024),
    ):
        images = [tmp/"target.img", tmp/"other.img"]
        for i,image in enumerate(images):
            with image.open("wb") as f:
                f.truncate(size if i==0 else 128*1024*1024)
        subprocess.run([str(executable), *map(str, images), str(filesystem)], check=True)
        with images[0].open("rb") as f:
            mbr = f.read(512)
            assert mbr[510:512] == b"\x55\xaa"
            start = struct.unpack_from("<I",mbr,454)[0]
            assert start > 0
            f.seek(start*512)
            boot = f.read(512)
            assert boot[offset:offset+5] == signature
            if filesystem == 1:
                reserved = struct.unpack_from("<H",boot,14)[0]
                roots = struct.unpack_from("<H",boot,17)[0]
                total = struct.unpack_from("<H",boot,19)[0] or struct.unpack_from("<I",boot,32)[0]
                fat_size = struct.unpack_from("<H",boot,22)[0]
                clusters = (total-reserved-boot[16]*fat_size-(roots*32+511)//512)//boot[13]
                assert fat_size and 4085 <= clusters < 65525
        if filesystem != 3 and shutil.which("mdir"):
            subprocess.run(["mdir","-i",f"{images[0]}@@{start*512}","::"],check=True,
                           stdout=subprocess.DEVNULL)
        if filesystem == 3 and shutil.which("fsck.exfat"):
            volume = tmp/"exfat-volume.img"
            with images[0].open("rb") as src, volume.open("wb") as dst:
                src.seek(start*512)
                remaining = struct.unpack_from("<I",mbr,458)[0]*512
                while remaining:
                    data = src.read(min(remaining,1024*1024))
                    assert data
                    dst.write(data);remaining -= len(data)
            subprocess.run(["fsck.exfat","-n",str(volume)],check=True)
    print("PASS: independent MBR/BPB checks for all three filesystems")
