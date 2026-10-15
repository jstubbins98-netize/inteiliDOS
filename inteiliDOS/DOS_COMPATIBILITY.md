# MS-DOS 6.22 in inteiliDOS

The `MS-DOS` command opens a software-emulated 8086/80186 PC running the real
MS-DOS 6.22 system files supplied on the installation disks. DOS has its own
memory, BIOS, command prompt, and persistent virtual hard drive. F8 stops the
emulator and returns to inteiliShell without rebooting inteiliDOS.

This replaces the earlier LaunchPad Virtual 8086 compatibility path. DOS
programs are no longer launched directly through LaunchPad or `RUN /DOS`;
those paths direct users to `MS-DOS`. Native IPGM and ELF applications still
use LaunchPad as before.

## Development status

Disk reading is a **work in progress**. Reading floppy disks and CD-hosted
disk images may not work reliably with every disk format, drive configuration,
or emulator. Compatibility is not guaranteed.

## Build and first launch

1. Build with `bash build.sh` and boot the new inteiliDOS image.
2. Have a safely bounded native hard-disk filesystem with approximately
   16 MB of free space and a free native directory entry. A blank physical disk
   is not automatically formatted by the emulator. New setup installs reserve
   boot/kernel/configuration sectors before native file storage. Recognized
   unpartitioned legacy filesystems remain supported in place. Older installed
   disks without layout metadata are blocked until a backup-safe migration is
   explicitly approved; see [NATIVE_STORAGE.md](NATIVE_STORAGE.md).
3. Supply your licensed DOS disks separately: insert a DOS media CD containing
   Disk 1's image, or a standard 1.44 MB MS-DOS 6.22 Disk 1 floppy in A:.
4. Type `MS-DOS` at inteiliShell. The emulator creates its private DOS HDD,
   installs the boot files and Disk 1 utilities, and starts the DOS `C:\>` prompt.

The inteiliDOS build never bundles DOS images into its OS ISO or kernel.
**MS-DOS installation disk images and game disk images are not included in
this GitHub repository.** Users must supply their own licensed disk images
separately on CD-ROM or floppy disk at runtime.
To optionally make a separate data CD from your four licensed images:

```sh
python3 cmake/create_msdos_cd.py /path/to/your/dos-disks /path/to/dos-media.iso
```

The old `MSDOS_DISK_DIR` OS-build option is no longer used. The separate-CD
tool accepts uploaded filenames with suffixes or standard disk names. It checks
the 1.44 MB FAT12 image layout and Disk 1's system files, then copies the
original images unchanged to the separate DOS CD's root:

| Supplied disk | ISO filename |
|---|---|
| Disk 1 | `D6221.IMG` |
| Disk 2 | `D6222.IMG` |
| Disk 3 | `D6223.IMG` |
| Supplemental disk | `D622S.IMG` |

You can instead create your own ISO9660 data CD with `DISK1.IMG`, `DISK2.IMG`,
`DISK3.IMG`, and `SUPPDISK.IMG` in its root. Only Disk 1 is required for initial
setup; the remaining images provide optional installation utilities.
The DOS CD needs no inteiliDOS kernel or bootloader and is not the OS boot ISO.
In 86Box, mount it in the CD-ROM drive after inteiliDOS has started.

For floppy-only setup, mount/write Disk 1's image as the actual 1.44 MB floppy
medium (not an `.IMG` file inside another floppy filesystem), then type
`MS-DOS /FLOPPY`. Plain `MS-DOS` also tries A: if no DOS Disk 1 CD is available.

The build does not download DOS or require the previously supplied DOS 4
source. First-time creation needs Disk 1. Once `MSDOS622.HDD` has been
successfully created, ordinary `MS-DOS` startup uses that saved disk without
probing the CD-ROM or requiring the ISO to remain inserted. This also applies
after booting inteiliDOS directly from the HDD.

The saved image on native C: takes precedence. If C: has no image, the emulator
looks for a single existing image on another safe native disk and reuses it.
Multiple alternative images, a wrong-sized image, or an I/O error cause a
refusal rather than overwriting data or choosing a disk silently.

Installation A: media is loaded on demand; F6 can retry after inserting the
CD. Missing optional media does not terminate DOS running from its saved C:.
Explicit `MS-DOS /IMAGE ...` still requires the requested image on the CD,
and unreadable media produces an error rather than a substitute.

Rebuilding the ISO does not update a kernel already installed on an HDD.
That installed kernel must also be updated to use changes when booting from
the HDD. Do not erase or clean-install over a disk containing games or saves
just to apply an emulator fix.

## Commands, drives, and controls

At inteiliShell:

```text
MS-DOS
MS-DOS /FLOPPY
MS-DOS /IMAGE GAME.IMG
```

The `/IMAGE` form mounts a 360 KB, 720 KB, 1.2 MB or 1.44 MB floppy image from the CD's root as
A:, while still booting DOS from C:. Game-image CDs do not need DOS installation
images. `/FLOPPY` selects the physical/emulated floppy drive as A:.

The physical reader detects the disk's BPB geometry and probes 500/250/300
kbps rather than assuming every game uses 18 sectors per track. A 360 KB
disk uses 40 tracks, two heads and nine sectors per track. A real 360 KB
disk requires a compatible 5.25-inch drive; emulator image support also
depends on its configured drive model.

For a saved DOS installation, mount the game disk before `MS-DOS /FLOPPY`,
then use `A:`, `DIR`, and the game's command (for Zork Trilogy, `ZORK1`).
The game disk need not contain DOS system files. First-time creation of the
DOS HDD still requires the supplied 1.44 MB DOS Disk 1.
After swapping physical media during a DOS session, press **F7** to reprobe
it and notify DOS. BIOS drive parameters reflect the active disk, and the
BIOS reports change-line support so DOS invalidates cached media information.

| Drive/key | Meaning |
|---|---|
| C: | Writable, persistent private DOS hard drive |
| A: | Read-only installation/game floppy media |
| F6 | Cycle Disk 1, Disk 2, Disk 3, and the supplemental image on the DOS CD |
| F7 | Select the real 1.44 MB floppy drive as A: |
| F8 | Stop the guest and return to inteiliShell |

The emulator reports media changes to DOS so its cached A: directory is
invalidated after a swap. A missing disk or read error is reported to DOS.
Writes to either image-backed or physical A: media are rejected.

DOS does **not** see the native ISO filesystem as a D: CD drive. Use floppy
images on the CD, or a physical floppy, to supply programs and their data.
ATAPI/MSCDEX CD-ROM drivers are not implemented.

## Install and run programs

Use ordinary DOS commands and compatible installers, choosing a destination
on C:. For example, if A: contains `GAME.EXE`:

```dos
MKDIR C:\GAMES
COPY A:\GAME.EXE C:\GAMES
CD C:\GAMES
GAME
```

Copy required data files too, or run the program's installer from A: and
select C: as its destination. F6 switches the supplied DOS installation
images; F7 selects a physical game floppy. The mounted `/IMAGE` game disk is
replaced when one of these media-selection keys is used.

Files installed on C: remain available on subsequent visits and after a host
restart. Exit applications normally and save/close files before pressing F8.
F8 is an immediate stop, not a program-specific save or DOS shutdown command:
completed sector writes persist, but unsaved application state and pending
guest buffers can be lost, and interrupted filesystem operations can leave
the private DOS volume inconsistent.

## How the emulator works

1. The native command locates an existing `MSDOS622.HDD` on an installed
   inteiliDOS filesystem, or reserves a new extent on a valid filesystem.
2. First-time setup initializes a private MBR and FAT16 partition, copies
   `IO.SYS`, `MSDOS.SYS`, `COMMAND.COM`, and Disk 1 utilities, and writes a small
   `AUTOEXEC.BAT` with a C: command prompt and path. It does not run Microsoft's
   physical-machine SETUP program or enable the DriveSpace driver.
3. A native interpreter executes the companion BIOS in fresh private guest
   RAM. The BIOS boots the virtual HDD, which then starts the actual DOS kernel
   and command interpreter. DOS itself handles COM/MZ loading, directories,
   batch files, nested EXEC, and file operations.
4. BIOS disk transfers are translated to bounded sectors within the native
   image file, or to read-only floppy-image/physical-floppy reads.
5. Guest keyboard, timer, video and speaker state is adapted to native
   services. F8 is polled independently of the guest's interrupt-enable flag.
6. On return, guest audio is stopped and the native display is restored.
   Each later launch resets guest RAM; only the disk contents persist.

The virtual HDD contains 32,760 sectors of 512 bytes (approximately 16 MB)
with a FAT16 partition starting at virtual LBA 1. It is stored as
`MSDOS622.HDD` in the native filesystem, not as a physical DOS partition.
Guest disk operations, including DOS formatting tools, cannot address sectors
outside that private extent.

## Compatibility and host safety

- The guest is an 8086/80186-class PC with 640 KB conventional memory.
  The host remains a native protected-mode inteiliDOS machine; this is a
  software interpreter, not hardware VM86 execution or a host DOS reboot.
- Text output, CGA/Hercules graphics, and PC-speaker tones are supported.
  Native VGA renders the guest graphics, with saved font-plane data and DAC
  palette restored when returning to text mode.
- Guest port I/O affects private emulated state. It cannot reprogram the
  host PIC, system PIT channel, or keyboard controller. Speaker output uses
  bounded native speaker routines and is stopped on exit.
- DOS disk access is restricted to the private C: image. The emulator does
  not format an unrecognized physical disk, and refuses a wrongly sized
  existing image instead of overwriting it.
- Image creation publishes the native directory entry only after successful
  initialization. Failed setup or cancellation leaves existing native files
  intact; ordinary native document saves cannot truncate the large DOS image.
- The interpreter is not cycle-accurate. Software timing and Turbo Pascal CRT
  calibration can depend on execution speed; selecting a QEMU 486 CPU is not
  by itself a guarantee of historical CPU timing.

Not implemented: 286/386 protected-mode software, DOS extenders, DPMI,
EMS/XMS managers, x87, EGA/VGA-specific graphics, Sound Blaster or other sound
cards, DOS networking, and native CD-ROM drivers. Windows NE/LE/LX/PE
executables are not supported. Unsupported CPU instructions stop the emulator
with an explicit diagnostic. An unimplemented device is not a promise that
the emulator can run every program probing that device.

For a backup, stop the guest and back up the host disk image using appropriate
host/emulator tools. Editing or deleting `MSDOS622.HDD` loses the DOS
installation and its saved files; do not treat it as an editable text document.

## Troubleshooting

| Message/problem | Action |
|---|---|
| Needs an installed inteiliDOS HDD | Install the native OS/filesystem on the intended disk first; the DOS command deliberately will not format it |
| DOS boot disk missing | Supply a separate DOS Disk 1 CD, or insert the DOS floppy and use `MS-DOS /FLOPPY`; rebuilding the OS ISO does not supply DOS |
| Cannot initialize the floppy controller / cannot read A: boot sector | Enable the floppy controller and configure A: as a 3½-inch 1.44 MB drive; mount Disk 1 directly. The message distinguishes timeout, CRC, and seek/read failures |
| Incomplete DMA transfer | The floppy transfer did not supply a full sector. Report the FDC status and DMA count line; do not format or erase the HDD |
| A: has no valid boot signature / unsupported floppy format | Mount a standard 1.44 MB DOS Disk 1 image, not a floppy containing an `.IMG` file, a CD image, or Disk 2 |

Physical floppy reads explicitly enable the AT DMA cascade and reject
abnormal controller status, incomplete result bytes, and short sector
transfers. The boot-signature check is never skipped. A failed boot-sector
read includes ST0/ST1/ST2 and the channel-2 DMA counter for diagnosis.
| Could not create DOS HDD | Check native free space, directory capacity, disk I/O, and whether initialization was cancelled |
| Invalid virtual HDD size | Preserve/back up the image and investigate; it will not be automatically replaced |
| Cannot read that floppy image | Check its supported size and CD-root filename; DOS installation images are not required on a game CD |
| Unsupported CPU instruction | Check whether the program needs a newer CPU, protected mode, or an FPU |
| A: write failure | A: is deliberately read-only; install programs and store saves on C: |
| Disk I/O failed | Stop and investigate the underlying native storage rather than assuming writes succeeded |

## Verification and developer reference

From the OS source directory:

```sh
python3 tests/build_test_kernel.py /tmp/msdos622-build
python3 tests/run_msdos622_test.py /tmp/msdos622-build/inteilidOS.elf
```

The integration test needs the separately supplied images in `../MS-DOS_disks` and
GCC/binutils, NASM, Python 3, xorriso, and QEMU. It checks real DOS 6.22 boot,
installed/nested COMMAND.COM, DEBUG.EXE, persistence across F8, re-entry and a
host restart, F8 through guest CLI, 80186 instructions, CGA, speaker register
handling, CD-hosted game media, physical floppy reads, media swaps, preservation
of a native file, and byte-for-byte preservation of unformatted/too-small
physical disks. Native DOS and returned-shell screenshots are written under
the repository's ignored `generated-artifacts/msdos622` directory.

Older `run_launchpad_dos_test.py`, Creature, and Still Alive v86 tests describe
the retired LaunchPad DOS path; their historical results do not certify this
emulator. The current test reuses the QEMU guest helper from the old test file,
not its old direct-launch assertions. QEMU testing does not certify 86Box or
physical hardware.

See [for_developers.md](for_developers.md#ms-dos-622-emulator) for implementation
details and extension rules, and [kernel/msdos/README.md](kernel/msdos/README.md)
for upstream provenance. The 8086tiny interpreter and companion BIOS are
MIT-licensed; retain `kernel/msdos/LICENSE.txt`. Microsoft's DOS binaries
retain their original license, independently of the emulator's license.
