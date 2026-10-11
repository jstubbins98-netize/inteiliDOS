# Native installed-disk layout

Run `python3 tests/run_fs_layout_tests.py` for bounded native saves, DOS-image
I/O and installer partition preflight. Pass a built test kernel path to include
the real setup wizard and BASIC save in QEMU, followed by byte-for-byte checks
that ordinary saves and DOS-image writes preserve boot/kernel/configuration.
See `../NATIVE_STORAGE.md` for the disk format and migration policy.

## Installed HDD cold boot and persistence

From the OS source directory:

```bash
python3 tests/build_test_kernel.py /tmp/hdd-cold-build
python3 tests/run_hdd_cold_boot_test.py /tmp/hdd-cold-build/inteilidOS.elf ../attached_assets
```

With no arguments the runner builds its own temporary kernel and uses the
uploaded DOS disks in `../attached_assets`. Requires GCC/binutils, NASM, QEMU
i386, xorriso and the four user-supplied MS-DOS 6.22 disk images (never
downloaded). All HDDs, installation media and QMP snapshots are temporary;
no user disk or configuration is modified.

Both clean install and dual-boot setup run through the real wizard. Every
subsequent start is a fresh QEMU process using BIOS, the installed MBR/VBR and
saved kernel, **without `-kernel`**. The initial and final cold boots have only
the installed HDD: no CD-ROM or floppy drive. A nonbootable data ISO supplies
DOS files only while creating the private virtual HDD; BIOS still boots C.

The runner saves BASIC files before and after DOS image creation, creates a
DOS directory, copies and executes a DOS program, and writes/reads DOS data.
After a final HDD-only cold boot, it loads and runs both BASIC files, boots
the saved DOS virtual HDD without its install media, reads the saved DOS
file, executes the copied program and verifies F8 returns to a working shell.
Offline checks validate the layout descriptor, directory bounds/nonoverlap,
native file bytes, DOS image size/signatures and unchanged MBR/VBR/kernel/
configuration hashes. The dual-boot fixture starts the native partition at
LBA 6144, preserves a nonzero synthetic foreign partition byte-for-byte and
checks its partition entry (apart from the expected active-flag change).
This verifies preservation, not boot-menu selection or execution of another OS.

# CD-ROM driver tests

Run from the OS directory:

```bash
python3 tests/run_cdrom_tests.py
```

Needs a native C compiler, ELF-capable GCC/binutils, NASM, QEMU
(`qemu-system-i386`), and xorriso. On macOS, the guest build requires the
`i686-elf` cross-toolchain; the task-file model uses native GCC/Clang.

The deterministic model exercises the actual CD-ROM driver with substituted
port I/O. It reproduces 86Box's successful IDENTIFY PACKET response leaving
stale cylinder values, plus all four slots, PCI-native port mappings, busy
status containing stale error bits, UNIT ATTENTION, empty trays, media rescan,
multi-phase/odd-byte transfers, short reads, draining excess data without
overwriting the buffer, and command-completion errors.

The QEMU tests link the real, unmodified port-I/O paths from `ata.c`, `pci.c`,
and `cdrom.c` into a temporary Multiboot guest. They check all four IDE slots
with and without HDD probing, loaded and empty trays, and repeated actual
ISO9660 sector reads. Files and guest images are created only in a temporary
directory. No OS configuration header or installed disk image is modified.

This tests IDE/ATAPI behavior; it does not run 86Box itself or add SCSI,
Mitsumi, or Panasonic proprietary CD-ROM controllers.

## DOS v86 and LaunchPad regression tests

```bash
python3 tests/run_dos_tests.py
python3 tests/build_test_kernel.py /tmp/inteilidos-test-build
python3 tests/run_launchpad_dos_test.py /tmp/inteilidos-test-build/inteilidOS.elf
```

The first command runs synthetic COM and relocated MZ programs read through
the actual FAT12 floppy and ISO9660 CD drivers, then the LaunchPad file bridge
and v86 runtime. It checks virtual flags, interrupt-vector chaining, DOS file
handles, EOF/write denial, memory allocation, unsupported APIs/port access,
guest faults, private BIOS data, repeated launches, state restoration, native
IPGM/ELF loading, and F8 during blocking input or an infinite CLI loop.
All temporary disk images are disposable; existing disks/configuration are
not modified. The second command compiles and links the complete kernel with
a temporary legacy test configuration and checks staging-region separation.
The third command boots that complete kernel, enters LaunchPad, runs COM and
MZ programs from both media through its actual UI, and returns to the shell.

Requires ELF GCC/binutils, NASM, QEMU i386, and xorriso. These are QEMU checks,
not 86Box certification. See [DOS compatibility](../DOS_COMPATIBILITY.md)
for the current API boundaries.

### Creature / Turbo Pascal CRT

```bash
python3 tests/run_creature_dos_test.py /path/to/CREATURE.EXE /tmp/inteilidos-test-build/inteilidOS.elf
```

Uses the supplied EXE without copying it into the test sources. From both
CD-ROM and floppy, it checks actual title/track coordinates and colors,
enters a bet, completes a race, exits normally back to a functioning shell,
then relaunches and tests F8 during Pascal CRT keyboard input.

This test uses bounded QEMU instruction timing. Unrestricted modern-speed
execution can trigger the executable's Turbo Pascal delay-calibration
overflow, even when QEMU is configured with `-cpu 486`. No guest EXE is
patched and no divide exception is suppressed. The synthetic DOS suite also
checks font queries, BIOS/BDA/cursor coherence, bounded virtual cursor ports,
and continued denial of VGA timing/PIC port writes.

### Still Alive / QuickBASIC-era timer and sound

```bash
python3 tests/run_stillalive_dos_test.py /path/to/STILLALI.EXE /tmp/inteilidos-test-build/inteilidOS.elf --full
```

Runs the supplied EXE from CD and floppy in parallel. It checks lyric
progression, natural completion with DOS exit code 0, rejects BASIC runtime
errors, records an actual QEMU PC-speaker waveform after excluding the boot
chime, then relaunches for F8 and returns to a working shell. Omit `--full`
for a shorter startup/audio/abort check. No third-party EXE or WAV is kept.

The synthetic suite also checks guest-private PIC mask readback, actual
virtual timer interrupt delivery/EOI, VGA capability queries, speaker cleanup
on normal return and F8 during masked/CLI execution, continued denial of PIC
initialization, and rejection of timer requests beyond the host-tick limit.

The synthetic suite covers `INTO` with and without overflow, the INT 04h
handler's saved flags/CS/return IP, virtual IF and stack restoration, and an
unhandled overflow. A test-only linker wrapper forces a GP trap on the
non-overflowing case, because successful native execution under QEMU alone
does not exercise that monitor path. The wrapper is not part of the OS build
and never changes supplied EXEs.

### System Health

```bash
python3 tests/run_health_test.py
```

See `../SYSTEM_HEALTH.md` for the tabs, read-only checks and limitations.
This runner includes focused real-i386 SMART/heap/alert tests and complete-OS
keyboard/UI tests with absent, normal, multiple and read-failing HDDs. It
checks live updates and compares full disk-image hashes before/after.

### Setup wizard and legacy ATA writes

From the OS source directory:

```bash
gcc -w -ffunction-sections -fdata-sections tests/ata_write_test.c \
  -Wl,--gc-sections -o /tmp/ata-write-test
/tmp/ata-write-test
python3 tests/build_test_kernel.py /tmp/setup-build
python3 tests/run_setup_test.py /tmp/setup-build/inteilidOS.elf
```

The focused test replaces only ATA port I/O and exercises the actual driver:
advertised FLUSH CACHE/FLUSH CACHE EXT, legacy cache-disable before writing,
known uncached drives, bulk zeroing, and rejected cache-disable/data/flush
commands. Genuine failures must remain failures. The test explicitly enables
the ATA code path; merely supplying an include directory does not enable
kernel configuration macros.

The UI test installs onto a temporary 64 MB QEMU HDD, checks persisted MBR/VBR
signatures, and confirms a usable shell. It never uses a user's disk.
Screenshots go to the ignored repository-level `generated-artifacts/setup`.
It verifies installation completion, not a subsequent BIOS cold boot.
Use `run_hdd_cold_boot_test.py` above for BIOS boot and post-save persistence.

### Native C: on any IDE connector

```bash
ASAN_OPTIONS=detect_leaks=0 python3 tests/run_fs_layout_tests.py
python3 tests/build_test_kernel.py /tmp/native-volume-build
python3 tests/run_small_hdd_test.py /tmp/native-volume-build/inteilidOS.elf
```

The logical-volume host fixtures check all eight ATA indexes, installed-over-
legacy priority, C:/D: separation, unchanged physical APIs, and fail-closed
behavior without formatting or redirecting writes. The emulator test installs
a temporary 10 MiB slave HDD, saves/loads via BASIC's actual C: prompt, and
verifies cold-boot reads and subsequent saves on every IDE connector. It also
covers a primary-master CD-ROM and multiple disks with full-image checksums
for unrelated legacy/foreign disks. BIOS boot priority is explicit when a
lower connector contains a nonbootable data disk. No user disk is touched.
The slave fixture also exercises editor and spreadsheet save/load dialogs
across separate BIOS boots and checks the restored document/cell contents.

### DOS HDD allocation diagnostics

```bash
python3 tests/run_fs_layout_tests.py
python3 tests/build_test_kernel.py /tmp/msdos-space-build
python3 tests/run_msdos_space_test.py /tmp/msdos-space-build/inteilidOS.elf
```

The sanitizer-enabled host fixture runs the real filesystem and DOS platform
adapter: required versus safe appendable space on a 10 MiB native disk,
successful 32 MiB initialization/publication, full directory with ample space,
ATA directory-read and image-write failures, and F8 cancellation. Refusals
before seeding must perform zero writes and zero media reads; whole-disk
comparisons confirm files and reserved sectors are unchanged. Failed/cancelled
initializations remain unpublished and keep native files intact.

The QEMU fixture checks the actual shell messages for a 10 MiB native HDD, a
full directory on 32 MiB, and sufficient space with missing DOS media. All
three preserve the complete disposable disk byte-for-byte and return to a
usable shell. Screenshots are saved under ignored `generated-artifacts/msdos-space`.
