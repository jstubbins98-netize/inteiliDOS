# Modern hardware

`python3 tests/run_setup_lock_test.py /tmp/legacy-build/inteilidOS.elf`
installs to a disposable HDD, checks the per-copy lock, cold-boots the real
MBR/VBR twice, verifies SETUP and INSTALL-OS refuse without any disk changes,
and confirms the separate installation-media copy remains usable.
The generated kernel-header directory is required for the focused test:
`gcc -O1 -ffunction-sections -fdata-sections -I/tmp/legacy-build tests/setup_lock_test.c -Wl,--gc-sections -o /tmp/setup-lock-test && /tmp/setup-lock-test`.

`python3 tests/run_launchpad_usb_test.py /tmp/usb-build/inteilidOS.elf`
checks large (>64 KiB) native ELF execution from FAT16/FAT32 USB, nested
folders, source switching, invalid-file refusal, Battery tab navigation,
keyboard preservation and unchanged USB media.

`gcc -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections tests/battery_test.c -Wl,--gc-sections -o /tmp/battery-test && /tmp/battery-test`
checks read-only Smart Battery transactions, capacity units/ratios,
unavailable/invalid/busy/error/timeout cases and freshness invalidation.
`run_health_test.py` additionally checks rendered percentages, low-battery
and wear warnings, stale-data handling, navigation and non-destructive disks.
Physical laptop battery compatibility still requires hardware testing.

Application shortcuts are F2 Save, F3 Load, F10 Quit in iEdit and InteiliSheets.
`python3 tests/run_app_function_keys_test.py /tmp/legacy-build/inteilidOS.elf`
checks PS/2 shortcuts, real native C: saves/loads, active cell edits, cancelled
dialogs, dirty-load/quit confirmation and persistence. The USB storage test
below checks these same shortcuts with an xHCI keyboard and USB destinations.

`python3 tests/run_msdos_usb_test.py /tmp/usb-build/inteilidOS.elf`
uses the locally supplied, licensed `../MS-DOS_disks` images (or `--media DIR`).
Pass `--game /path/to/ZORK.IMG` for the supplied 360 KB Zork Trilogy image.
It checks real MS-DOS setup from multi-cluster USB FAT32 images, automatic
and explicit drive selection, errors for missing/unmounted/corrupt media,
Disk 2 swapping, F8, persistence after re-entry and after removing USB,
USB game directory reading and Zork gameplay, missing/invalid game refusal,
and unchanged USB source images/native user files. Build with `--modern`
to enable the xHCI driver. Only disposable test disks are modified.

USB storage commands and support limits are documented in
[USB_STORAGE.md](../USB_STORAGE.md). Run:

```
python3 tests/build_test_kernel.py /tmp/usb-build --modern
python3 tests/run_usb_storage_test.py /tmp/usb-build/inteilidOS.elf
```

The regression first runs a deterministic host fault model with the real
FatFs adapter (flush/disconnect errors, failed writes, unsupported media,
4Kn rejection and size/LBA bounds). It then tests actual QEMU xHCI BOT
devices and a USB-only keyboard: MBR FAT16 and raw FAT32, real DIR/TYPE,
multi-cluster copies, shorter overwrites, iEdit save/open, blocked access
after eject, remount, persisted data and independently checked FAT mirrors.
Only disposable disk images are modified.

It also covers the one-file USB copy chooser (including cancel and an untouched
unselected file), USB device selection from all three apps, BASIC program
save/load and execution, and Sheets CSV save/load with row positions preserved.
The host dialog test checks native C:/D: selection, mounted-only USB drive
labels, cancel, unavailable drives and filename bounds.

For SATA optical coverage, see [SATA_OPTICAL.md](../SATA_OPTICAL.md). Run the
complete-kernel CDROM test with `--sata` (and `--empty` for insertion after boot),
and the modern hardware test with `--sata-optical` to load its HDD read/write
fixture through a SATA optical drive on the same AHCI controller.
`run_launchpad_floppy_test.py /tmp/legacy-build/inteilidOS.elf` verifies that
expanding the optical slots does not alias the FAT12 floppy source: a native
ELF is read from floppy, executed outside staging memory and returns to shell.

The deterministic AHCI packet model needs no physical hardware:

```sh
gcc -O1 -g -ffunction-sections -fdata-sections -fno-pie -no-pie \
  -DCONFIG_ENABLE_AHCI=1 -DCONFIG_ENABLE_ATA=1 \
  tests/sata_optical_test.c kernel/sata_optical.c kernel/pci.c \
  -Wl,--gc-sections -o /tmp/sata-optical-test
/tmp/sata-optical-test
```

For installed-HDD startup and recovery regressions, see
[BOOT_RECOVERY.md](../BOOT_RECOVERY.md). `run_boot_repair_test.py` covers cancelled
and confirmed boot repair, backups, dirty-RAM cold boots and BIOS drive `0x81`.

`python3 tests/run_modern_hardware_test.py` builds the optional-driver profile
and runs it as ELF32 on x86-64 Intel/AMD QEMU CPU models. It tests USB-only input
without the PS/2 driver, xHCI/UHCI after a mouse, report-ring wrap, the health UI
and SATA/AHCI durable reads/writes beyond 128 GiB. All disk fixtures are temporary
and disposable. The 140 GiB test disks are sparse, not full allocations.

To reuse a build: `python3 tests/run_modern_hardware_test.py /tmp/build/inteilidOS.elf`.
Build that kernel with `python3 tests/build_test_kernel.py /tmp/build --modern`.

# DOS HDD creation on SATA

```sh
gcc -O1 -Wall -Wextra -Werror -DCONFIG_ENABLE_PCI_IDE=1 \
  -ffunction-sections -fdata-sections tests/ahci_zero_test.c \
  -Wl,--gc-sections -o /tmp/ahci-zero-test
/tmp/ahci-zero-test
python3 tests/run_msdos_usb_test.py /tmp/build/inteilidOS.elf --sata
```

The register-model test checks four-sector DMA commands, sector-count and
PRDT lengths, one flush per at-most-128-sector batch, LBA28/LBA48 bounds,
uncached/readonly drives, and data/short-transfer/flush failures. Optical
ports are not writable through this API.
The full-kernel SATA test uses locally supplied DOS images on USB and a
disposable AHCI disk. It cancels during visible clearing, retries, reaches
the real DOS prompt, runs DOS and a USB game, and verifies persistence
without USB plus unchanged native/source data. The host
`msdos_space_test.c` also covers cancellation before writing, after a
partial clear, during system-file copying and during FAT creation, with no
incomplete image published.

# xHCI firmware startup and Intel port routing

```sh
gcc -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  tests/usb_xhci_init_test.c -Wl,--gc-sections -o /tmp/usb-xhci-init-test
/tmp/usb-xhci-init-test
```

This runs the actual driver against a PCI/MMIO register model. It checks
Intel `8086:1e31` routing masks/order and unrelated-bit preservation,
nonmatching-controller isolation, rejected routing writes, initial CNR
readiness, normal ownership release and the Intel-only timeout fallback,
SMI-enable readback and W1C event handling, rejected ownership requests,
firmware ownership reclaim, nonmatching-controller refusal, bounded halt/reset/start failures,
unavailable MMIO, unsupported page sizes, no rescan reset, and DMA timeout
quarantine. It does not prove physical laptop compatibility.
`run_usb_storage_test.py` additionally exercises real emulated xHCI/BOT
read/write/mount/eject operations and USB keyboard isolation with a modern
test kernel.

# Background thermal monitoring

`thermal_test.c` runs the real background service and SMBus state machine
against a deterministic PIIX4/W83781D/W83782D/AS99127F register model:

```sh
mkdir -p /tmp/thermal-config
printf '\n' > /tmp/thermal-config/config.h
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined \
  -I/tmp/thermal-config tests/thermal_test.c -o /tmp/thermal-test
ASAN_OPTIONS=detect_leaks=0 /tmp/thermal-test
```

It checks the 500 ms start cadence, 75/65 C hysteresis, half-degree
rounding, warning retention after failures, vendor IDs, fan divisors,
bank restoration, timeout handling, busy firmware buses, disabled hardware
and tick wrap. It asserts that every sensor write is a bank-selector write:
there must be **no** fan/sensor configuration writes.

`thermal_timer_test.c` exercises the real PIT and PCM IRQ handlers without
privileged I/O, verifying that thermal ticks continue at the actual PIT
millisecond rate through a complete four-second PCM stream and that the
existing USB callback is unaffected:

```sh
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined \
  tests/thermal_timer_test.c -o /tmp/thermal-timer-test
ASAN_OPTIONS=detect_leaks=0 /tmp/thermal-timer-test
```

`run_thermal_test.py` boots the complete kernel in QEMU and checks the
Cooling tab, four-tab navigation and accurate unavailable messages on a
machine without supported sensors. It also checks counter progress outside
the dashboard. It uses temporary media, not user disks:

```sh
python3 tests/build_test_kernel.py /tmp/thermal-kernel-build
python3 tests/run_thermal_test.py /tmp/thermal-kernel-build/inteilidOS.elf
```

Native screenshots are written under ignored
`generated-artifacts/thermal-monitor/`. Hardware readings/control on actual
86Box OEM machines and physical motherboards remain unverified. These tests
do not demonstrate automatic fan activation, which is not implemented.

An optional `--pcm` native ELF fixture records monitoring counters
immediately before and after real PCM playback. That end-to-end check is
**not verified**: high-rate playback stalls QMP memory requests in this QEMU
environment, including after the expected playback duration. Instructing
QEMU to use instruction-count timing did not produce a reliable real-time
baseline. The passing IRQ-handler test does not replace playback verification
on actual 86Box machines.

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
python3 tests/run_hdd_cold_boot_test.py /tmp/hdd-cold-build/inteilidOS.elf ../MS-DOS_disks
```

With no arguments the runner builds its own temporary kernel and uses the
separately supplied DOS disks in `../MS-DOS_disks`. Requires GCC/binutils, NASM, QEMU
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

### Saved DOS startup without the installation CD

```bash
python3 tests/run_msdos_reopen_test.py
python3 tests/run_msdos_function_keys_test.py \
  /tmp/legacy-build/inteilidOS.elf /tmp/modern-build/inteilidOS.elf
python3 tests/run_keyboard_modifiers_test.py
python3 tests/run_msdos_modifiers_test.py \
  /tmp/legacy-build/inteilidOS.elf /tmp/modern-build/inteilidOS.elf
```

`run_msdos_function_keys_test.py` uses locally supplied DOS images and
disposable disks to run a COM fixture under real DOS 6.22. It checks exact
INT 16h scan/ASCII results for F10, F9, F1 and F5 with PS/2 and xHCI USB
keyboards, plus F8 exit and continued shell keyboard operation. It does not
require or redistribute Mario Teaches Typing.

`run_keyboard_modifiers_test.py` runs the real PS/2 decoder/shared queue
and USB report translator without port I/O. It checks modifier snapshots,
independent sides, E0/Pause/PrintScreen handling, mixed input sources,
unchanged legacy reads and BIOS encoding. `run_msdos_modifiers_test.py`
runs 45 exact BIOS-key/held-state/release cases on each keyboard profile,
alternating legacy/enhanced INT 16h peek/read APIs and checking `02h`/`12h`
flags. A DOS INT 9h hook independently observes physical make codes.
It also checks modifier-only transitions, modified reserved controls,
shell typing, native editor F10 and protected native disk bytes.
Screenshots go to `generated-artifacts/msdos-modifiers/`.

This host test checks CD-independent disk selection, recovery from another
native volume, ambiguous/invalid/unreadable-image refusal, and nonfatal
optional-floppy failure. It never accesses physical CMOS or VGA.
`run_msdos622_test.py` also ejects the ISO between actual DOS sessions and
checks saved files. `run_hdd_cold_boot_test.py` checks real BIOS HDD-only
startup without a kernel loader or CD.

### Separately supplied DOS CD and floppy media

```bash
python3 tests/run_msdos_external_media_test.py /tmp/hdd-cold-build/inteilidOS.elf
```

This complete-OS test starts real DOS using only a supplied 1.44 MB floppy,
using both `/FLOPPY` and automatic fallback after deliberately resetting the
controller into incompatible firmware-leftover settings through QTest,
including disabled ISA DMA and its cascade. It verifies saved files across
sessions; runs `/IMAGE` on a game-only CD without DOS installation markers;
and initializes another private HDD from a separate `DISK1.IMG` data CD.
It checks native files and the original DOS floppy image are unchanged.

The register-level `fdc_read_test.c` fixture checks AT DMA cascade setup,
all 512 transferred bytes, abnormal/CRC/timeout errors, ISA address bounds,
and PIC restoration. This supplements QEMU, which can mask firmware/DMA
setup differences:

```bash
gcc -O1 -fsanitize=address,undefined -ffunction-sections -fdata-sections \
  -Wl,--gc-sections tests/fdc_read_test.c -o /tmp/fdc-read-test
ASAN_OPTIONS=detect_leaks=0 /tmp/fdc-read-test
```

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

### CDROM command

```bash
gcc -O1 -fsanitize=address,undefined tests/cdrom_command_test.c -o /tmp/cdrom-command-test
ASAN_OPTIONS=detect_leaks=0 /tmp/cdrom-command-test
python3 tests/run_cdrom_command_test.py /tmp/test-kernel/inteilidOS.elf
```

The host fixture covers all options, case-insensitive arguments, multiple drives,
empty/absent discs, read errors and eject rejection. The complete-kernel QEMU
fixture creates a disposable ISO and an ELF linked outside the staging window.
It checks actual volume/size listings, ELF-only browsing, nested program
execution, wrong-format rejection, ordinary LaunchPad restoration, tray
ejection and media reinsertion. It verifies the ISO is unchanged and saves a
listing screenshot under ignored `generated-artifacts/cdrom-command`.

### Legacy 360 KB game media

Run with a user-supplied Zork Trilogy FAT12 image (not bundled into the OS):

```bash
python3 tests/run_msdos_zork_test.py /tmp/test-kernel/inteilidOS.elf /path/to/Zork.img \
  --media ../MS-DOS_disks/Disk1.img
```

This checks the actual directory, ZORK1.BAT launch, West of House and opening
the mailbox through both physical-floppy and CD-hosted image paths. The original
image and existing native file are preserved. Screenshots go to ignored
`generated-artifacts/msdos-zork`. QEMU needs an explicit double-sided 40-track
profile: its default drive can interpret 360K as single-sided 80-track media.
The fixture also declares the model's actual 40-track mechanism in CMOS,
because QEMU's 1.2M-labelled profile does not emulate physical double stepping.
The register-level FDC fixture separately checks 80-track drive double stepping,
all four supported geometries, invalid BPBs, and incomplete/failed seeks.
