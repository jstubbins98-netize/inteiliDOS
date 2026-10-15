#!/usr/bin/env python3
"""Real installer -> BIOS HDD boot -> native/DOS writes -> HDD-only cold boot.

Usage: run_hdd_cold_boot_test.py [kernel.elf] [supplied DOS disk directory]
No arguments builds a temporary test kernel and uses ../MS-DOS_disks. Every
disk is disposable. DOS install media is a data-only ISO, never a boot CD.
Both clean and alongside-existing-partition setup paths are exercised.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
import tempfile

from build_test_kernel import ROOT, build, run
from run_launchpad_dos_test import Guest
from run_msdos622_test import clear_dos, shell

sys.path.insert(0, str(ROOT / "cmake"))
from prepare_msdos622 import prepare

SECTOR = 512
DISK_SECTORS = 64 * 2048
FOREIGN_START, FOREIGN_SECTORS = 2048, 4096
FILES = {"before.txt": b"10 print 8401\n", "after.txt": b"10 print 8402\n"}
DOS_SECTORS = 520 * 63


def read_at(disk, lba, size):
    with disk.open("rb") as f:
        f.seek(lba * SECTOR)
        data = f.read(size)
    assert len(data) == size, f"Short read at LBA {lba}"
    return data


def digest(disk, lba, sectors):
    return hashlib.sha256(read_at(disk, lba, sectors * SECTOR)).digest()


def make_disk(disk, dual):
    with disk.open("wb") as f:
        f.truncate(DISK_SECTORS * SECTOR)
    if not dual:
        return None
    mbr = bytearray(SECTOR)
    # Existing OS is active; setup may change only its status byte.
    entry = struct.pack("<B3sB3sII", 0x80, b"\x01\x01\x00", 0x06,
                        b"\xfe\xff\xff", FOREIGN_START, FOREIGN_SECTORS)
    mbr[446:462] = entry
    mbr[510:] = b"\x55\xaa"
    # Synthetic unrelated data includes a recognisable VBR and nonzero body,
    # not a zero-filled partition whose accidental erasure could go unnoticed.
    with disk.open("r+b") as f:
        f.write(mbr)
        for lba in range(FOREIGN_START, FOREIGN_START + FOREIGN_SECTORS):
            data = bytearray(hashlib.sha256(struct.pack("<I", lba)).digest() * 16)
            if lba == FOREIGN_START:
                data[510:] = b"\x55\xaa"
            f.seek(lba * SECTOR)
            f.write(data)
    return entry, digest(disk, FOREIGN_START, FOREIGN_SECTORS)


def check_foreign(disk, foreign):
    if foreign is None:
        return
    entry, checksum = foreign
    actual = read_at(disk, 0, SECTOR)[446:462]
    assert actual == b"\0" + entry[1:], "Unrelated partition entry changed"
    assert digest(disk, FOREIGN_START, FOREIGN_SECTORS) == checksum, \
        "Unrelated partition contents changed"


def install(kernel, disk, folder, dual):
    guest = Guest(kernel, None, False, folder,
                  ("-drive", f"file={disk},if=ide,index=0,format=raw"))
    guest.sock.settimeout(15)
    try:
        guest.wait("Welcome to inteiliDOS")
        guest.wait("> ")
        shell(guest, "setup")
        guest.wait("Setup Wizard")
        guest.key("ret")
        guest.wait("Choose an installation mode")
        if not dual:
            guest.key("down")
        guest.key("ret")
        guest.wait("Select the target installation drive")
        guest.key("ret")
        guest.wait("Press ENTER to begin installation")
        guest.key("ret")
        guest.wait("Installation successful", timeout=180)
    finally:
        guest.close()


def layout(disk, dual):
    mbr = read_at(disk, 0, SECTOR)
    assert mbr[510:] == b"\x55\xaa", "Missing MBR signature"
    entries = [mbr[446 + i * 16:462 + i * 16] for i in range(4)]
    native = [entry for entry in entries if entry[4] == 0x99]
    assert len(native) == 1 and native[0][0] == 0x80
    assert sum(entry[0] == 0x80 for entry in entries) == 1
    start, sectors = struct.unpack_from("<II", native[0], 8)
    assert start == (FOREIGN_START + FOREIGN_SECTORS if dual else 2048)
    assert sectors == DISK_SECTORS - start
    assert read_at(disk, start, SECTOR)[510:] == b"\x55\xaa"
    descriptor = struct.unpack("<8I", read_at(disk, start + 1025, 32))
    assert descriptor[:7] == (0x314c4449, 1, start, sectors, 1024, 8, 1033)
    checksum = 0x9e3779b9
    for value in descriptor[:7]:
        checksum ^= value
    assert descriptor[7] == checksum
    return start + descriptor[6]


def check_files(disk, dir_lba, expected, dos=False):
    directory = read_at(disk, dir_lba, SECTOR)
    assert directory[:4] == struct.pack("<I", 0x46530001)
    found, extents = {}, []
    for offset in range(4, 484, 32):
        name, start, size = struct.unpack_from("<24sII", directory, offset)
        name = name.split(b"\0", 1)[0]
        if not name:
            continue
        name = name.decode("ascii")
        assert name not in found, f"Duplicate file {name}"
        sectors = max(1, (size + SECTOR - 1) // SECTOR)
        assert dir_lba < start and start + sectors <= DISK_SECTORS
        extents.append((start, start + sectors))
        found[name] = (start, size)
    extents.sort()
    assert all(a[1] <= b[0] for a, b in zip(extents, extents[1:])), \
        "Native file/DOS image extents overlap"
    for name, content in expected.items():
        assert name in found, f"Missing saved file {name}"
        start, size = found[name]
        assert size == len(content) and read_at(disk, start, size) == content
    if dos:
        assert "MSDOS622.HDD" in found, "DOS HDD was not published"
        start, size = found["MSDOS622.HDD"]
        assert size == DOS_SECTORS * SECTOR
        assert read_at(disk, start, SECTOR)[510:] == b"\x55\xaa"
        assert read_at(disk, start + 1, SECTOR)[510:] == b"\x55\xaa"


def cold_start(disk, folder, iso=None):
    args = ["-boot", "order=c,strict=on",
            "-drive", f"file={disk},if=ide,index=0,format=raw"]
    if iso is not None:
        args += ["-drive", f"file={iso},if=ide,index=2,media=cdrom,format=raw"]
    guest = Guest(None, None, False, folder, args)
    guest.sock.settimeout(15)
    try:
        assert not {"-kernel", "-initrd", "-cdrom"} & set(guest.argv)
        if iso is None:
            assert sum(arg == "-drive" for arg in guest.argv) == 1
        guest.wait("Welcome to inteiliDOS", timeout=60)
        guest.wait("> ")
        return guest
    except BaseException:
        guest.close()
        raise


def save_basic(guest, name, value):
    shell(guest, "basic")
    guest.wait("BYE to exit")
    shell(guest, f"10 print {value}")
    shell(guest, "save")
    guest.wait("Filename (")
    shell(guest, name)
    guest.wait("Save to:")
    guest.key("1")
    guest.wait("Saved")
    shell(guest, "bye")
    guest.wait("> ")


def load_basic(guest, name, value):
    shell(guest, "basic")
    guest.wait("BYE to exit")
    shell(guest, "load")
    guest.wait("Filename to load:")
    shell(guest, name)
    guest.wait("Load from:")
    guest.key("1")
    guest.wait(f"Loaded: C:\\{name}.txt (1 lines)")
    shell(guest, "list")
    guest.wait(f"10 print {value}")
    # RUN must produce a separate output line, not just echo the program text.
    shell(guest, "run")
    screen = guest.wait(str(value))
    assert str(value) in [line.strip() for line in screen.splitlines()], screen
    shell(guest, "bye")
    guest.wait("> ")


def dos_start(guest):
    shell(guest, "ms-dos")
    guest.wait("MS-DOS 6.22 -", timeout=180)
    guest.wait("C:\\>")
    clear_dos(guest)


def dos_exit(guest):
    guest.key("f8")
    guest.wait("Returned from MS-DOS")
    guest.wait("> ")


def exercise(kernel, iso, folder, dual):
    mode = "dual" if dual else "clean"
    folder.mkdir()
    disk = folder / "install.img"
    foreign = make_disk(disk, dual)
    install(kernel, disk, folder, dual)
    dir_lba = layout(disk, dual)
    protected = digest(disk, 0, dir_lba)
    check_foreign(disk, foreign)
    print(f"PASS [{mode}]: actual setup, versioned layout and foreign partition checks", flush=True)

    # First prove the just-installed disk boots with no optical/floppy media.
    guest = cold_start(disk, folder)
    try:
        save_basic(guest, "before", 8401)
    finally:
        guest.close()
    assert digest(disk, 0, dir_lba) == protected, "Native save damaged boot region"
    check_files(disk, dir_lba, {"before.txt": FILES["before.txt"]})
    check_foreign(disk, foreign)
    print(f"PASS [{mode}]: HDD-only BIOS boot and native save", flush=True)

    # A nonbootable CD supplies licensed DOS files only during initialization.
    guest = cold_start(disk, folder, iso)
    try:
        load_basic(guest, "before", 8401)
        dos_start(guest)
        shell(guest, "md games")
        shell(guest, "copy c:\\command.com c:\\games\\tool.com")
        guest.wait("1 file")
        shell(guest, "echo HDD-COLD-SAVED>c:\\games\\save.txt")
        clear_dos(guest)
        shell(guest, "type c:\\games\\save.txt")
        guest.wait("HDD-COLD-SAVED")
        dos_exit(guest)
        save_basic(guest, "after", 8402)
    finally:
        guest.close()
    assert digest(disk, 0, dir_lba) == protected, "DOS/native writes damaged boot region"
    check_files(disk, dir_lba, FILES, dos=True)
    check_foreign(disk, foreign)

    # New process, empty RAM, only the saved physical HDD. Neither OS startup
    # nor the DOS emulator may depend on the original kernel ELF or DOS CD.
    guest = cold_start(disk, folder)
    try:
        load_basic(guest, "before", 8401)
        load_basic(guest, "after", 8402)
        dos_start(guest)
        shell(guest, "type c:\\games\\save.txt")
        guest.wait("HDD-COLD-SAVED")
        clear_dos(guest)
        shell(guest, "c:\\games\\tool.com /c ver")
        guest.wait("MS-DOS Version 6.22")
        dos_exit(guest)
        shell(guest, "help")
        guest.wait("MS-DOS")
    finally:
        guest.close()
    assert digest(disk, 0, dir_lba) == protected, "Cold reboot damaged protected sectors"
    check_files(disk, dir_lba, FILES, dos=True)
    check_foreign(disk, foreign)
    print(f"PASS [{mode}]: HDD-only cold reboot, both BASIC files, DOS data/program, "
          "F8, protected sectors and unrelated partition preserved", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", nargs="?", type=Path)
    parser.add_argument("dos_disks", nargs="?", type=Path,
                        default=ROOT.parent / "MS-DOS_disks")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="inteilidos-cold-boot-") as folder:
        tmp = Path(folder)
        kernel = args.kernel.resolve() if args.kernel else build(tmp / "build")
        prepare(args.dos_disks.resolve(), tmp / "disc")
        iso = tmp / "dos-data.iso"
        run("xorriso", "-as", "mkisofs", "-quiet", "-iso-level", "1",
            "-o", iso, tmp / "disc")
        for dual in (False, True):
            exercise(kernel, iso, tmp / ("dual" if dual else "clean"), dual)


if __name__ == "__main__":
    main()
