# Native HDD storage safety

Setup and the native filesystem share `kernel/disk_layout.h`. A new install
uses one MBR type-`0x99` partition. The offsets below are **partition-relative**,
even when a dual-boot partition begins somewhere other than physical LBA 2048.

| Region | Relative sectors | Purpose |
|---|---:|---|
| Boot | 0 | Volume boot record |
| Kernel | 1–8192 | Reserved kernel area, maximum 4 MiB |
| Configuration | 8193–8200 | Reserved; versioned layout descriptor at 8193 |
| Directory | 8201 | Existing 15-entry native directory format |
| File data | 8202 through partition end | Files and private DOS HDD image |

New installations use layout version 2. The BIOS HDD loader reads one 4 KiB
chunk into a low-memory bounce buffer and copies it into high memory before
reading the next chunk. It does not stage the whole kernel below 1 MiB.
The native 1–5 MiB RAM window and LaunchPad staging boundary are unchanged.
Before loading, the VBR collects a complete BIOS E820 memory map and passes it
through the kernel's Multiboot interface. This enables physical RAM allocation
after HDD-only boots, including the DOS emulator's 16 MiB guest allocation
above physical 16 MiB. Configured RAM is not a substitute for firmware data.
Version 1 installations retain their 512 KiB reservation and original file
locations; runtime storage supports both layouts without moving files.
Installing the larger kernel requires a separately confirmed new installation,
not an automatic in-place conversion of an existing disk.

Directory entries retain physical LBAs. The descriptor binds its version,
partition start/size, kernel/configuration reservations and directory offset
with a checksum. Runtime storage validates all MBR extents, rejects overlaps
and ambiguous native partitions, verifies the descriptor, and checks every
directory extent before any ordinary save or image allocation. Zero-byte files
still consume one sector. DOS image writes and bulk clearing use the same
partition bounds; they cannot reach the MBR, VBR, kernel or configuration.
Setup refuses an oversized kernel or insufficient partition space before any
write. It initializes the directory and publishes the descriptor only during
an explicitly confirmed new install.

## Existing disks

* **Recognized legacy flat filesystem:** an unpartitioned disk with the
  existing native magic at LBA 2048 keeps its directory there and its data at
  2049 onward. Valid files remain readable/writable without relocation.
* **Older installed native partition:** without a valid new layout descriptor,
  saves and DOS image access are blocked (`FS_ERR_LAYOUT`). Finding directory
  magic at the old physical location does not override an installed partition.
* **Blank, foreign, malformed or corrupt filesystem:** no automatic formatting
  or directory replacement. A missing native directory is `FS_ERR_NODIR`.
* **Dual-boot setup:** validates existing partitions and trailing free space.
  It refuses existing native installs, unknown boot sectors and detected flat
  legacy files rather than silently converting them. Clean install remains
  explicitly destructive, with the wizard's existing confirmation warning.

## Native C: and physical connectors

BASIC save/load (including its temporary cassette WAV), the editor,
spreadsheets, and the private MS-DOS HDD all use the same native C: selection.
Discovery checks all eight ATA indexes without writing anything. Only a
validated layout **and** directory qualifies. Active installed native
partitions come first, then other installed native partitions, then recognized
unpartitioned legacy filesystems. Equal-priority disks use ascending physical
connector index. C: is the first qualifying volume; D: is the next, never an
alias for C: or a promise that a device is removable.

This policy does not guess which HDD the BIOS booted when several equally
qualified native installs exist. Setups with that requirement need an explicit
volume-selection feature. Selection is stable across cold boots with the same
devices/layouts and is pinned after successful discovery during a boot.
Every use revalidates the selected volume; corrupt, full or unreadable C:
reports a failure rather than silently sending data to another disk. Failed
discovery is retried, so a subsequently confirmed setup can establish storage.
No unknown disk is initialized or migrated to obtain a drive letter.

The existing `fs_read`, `fs_write`, image and ATA APIs still take **physical**
indexes. Applications use `fs_volume_read`, `fs_volume_write`, or resolve a
physical index with `fs_volume_drive` for bounded image operations.

Configuration audit: runtime has no separate settings-file persistence.
Setup writes the layout descriptor/reserved configuration sectors to its
explicitly selected physical target; it must not use logical C: to install.
Build profiles are host-side inputs, not guest disk saves. DISKCHECK and System
Health retain their physical diagnostic behavior, not logical-volume mapping.

## Backup-safe migration policy

This change does **not** migrate existing layouts. Do not run a clean install
as a way to recover old files: it discards their filesystem.

A future migration requires the disk owner's explicit approval of the source,
destination and changed sector ranges. First make a complete offline disk-image
backup on separate storage, verify its checksum and readability, and retain the
original untouched. Validate and export all old directory entries and files
from a copy. Construct the new partition layout on another copy or a new disk,
copy files with recalculated LBAs, and verify file checksums, directory bounds
and a cold boot before offering to replace the original. If old filesystem
data already overwrote boot/kernel sectors, backup preserves the evidence;
do not invent a directory or claim that damaged files were recovered.

## Regression checks

```sh
python3 tests/run_fs_layout_tests.py
python3 tests/build_test_kernel.py /tmp/native-layout-build
python3 tests/run_fs_layout_tests.py /tmp/native-layout-build/inteilidOS.elf
python3 tests/run_msdos622_test.py /tmp/native-layout-build/inteilidOS.elf ../MS-DOS_disks --installed
python3 tests/run_hdd_cold_boot_test.py /tmp/native-layout-build/inteilidOS.elf ../MS-DOS_disks
python3 tests/run_hdd_stream_test.py
python3 tests/run_hdd_map_boot_test.py
python3 tests/run_small_hdd_test.py /tmp/native-layout-build/inteilidOS.elf
```

Host checks use the real filesystem and installer preflight against disposable
ATA fixtures, with address/undefined-behavior checks for the filesystem.
The streaming-loader test boots on an emulated i486 and verifies every loaded
byte with 600 KiB, 1.2 MiB and 4 MiB fixtures. The memory-map boot test injects
20/24-byte E820 replies, disabled entries, invalid signatures, partial failures
and endless continuations. The HDD cold-boot test verifies DOS startup and
persistence without installation media on a 64 MiB VM, real detected RAM rather
than the 16 MiB profile constant, and safe refusal on a genuinely 16 MiB VM.
Logical-volume fixtures cover all eight ATA indexes, selection priority,
separate C:/D: volumes, unchanged physical APIs, failed discovery followed by
setup, and no write redirection after corruption, I/O failure or full storage.
The small-HDD QEMU test performs real clean setup on a sole IDE slave, then
BIOS cold boots and BASIC save/load on every IDE connector, a slave with a
primary-master CD-ROM, and multiple disks. Full hashes protect the unrelated
legacy and foreign disks; reserved-sector comparisons protect the installed
disk's boot/kernel/configuration regions.
Editor and spreadsheet dialogs also save and restore their contents across
separate slave-HDD BIOS boots. The editor accepts Y to confirm discarding
unsaved edits before opening a file (in addition to its existing Ctrl+O code).
They cover non-default partition starts, foreign partitions, legacy files,
unformatted/old/corrupt layouts, integer overflow, zero-byte allocation, full
partitions, forged image bounds and a 32 MiB private image. The QEMU check uses
the actual setup wizard and BASIC save UI, then exercises native saves and image
writes against that installed disk and verifies the protected regions byte-for-byte.
The `--installed` DOS integration variant runs real DOS on a non-default-start
partition fixture, checks file persistence/re-entry/reboot, and verifies the
reserved sectors' checksum after actual DOS writes. Without that flag the
integration test retains its flat legacy-disk fixture.
The installed-HDD cold-boot regression runs real clean and dual-boot setup,
then launches fresh QEMU processes through BIOS with no direct kernel loader.
It saves native BASIC programs, creates and uses the real DOS virtual HDD,
then boots with only the physical HDD and verifies both native files and DOS
data/program execution. It checks reserved sectors and synthetic unrelated
partition contents after installation, native saves, DOS writes and reboot.
The DOS initialization ISO contains data only; no boot CD is needed.
See `tests/README.md` for prerequisites and the full sequence.
