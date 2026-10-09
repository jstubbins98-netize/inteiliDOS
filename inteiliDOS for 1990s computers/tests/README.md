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
