# Native MS-DOS 6.22 emulator

Enter with `MS-DOS` in inteiliShell. The real MS-DOS system files from the
user's supplied Disk 1 run on a software 8086/80186 PC, not the previous
partial native INT 21h compatibility layer. Native IPGM and ELF programs
still launch through LaunchPad.

## Build and media

Build normally with `build.sh`; the OS ISO contains no DOS disk images.
Users supply their DOS media separately.

To make a separate DOS media CD, put your four licensed images in a folder,
named `Disk1.img`, `Disk2.img`, `Disk3.img`, and `Suppdisk.img`, then run:

```sh
python3 cmake/create_msdos_cd.py /path/to/dos-disks /path/to/dos-media.iso
```

Alternatively insert a 1.44 MB DOS Disk 1 floppy and run `MS-DOS /FLOPPY`.
Keep the separate DOS media inserted for installation access. Images are read on demand, not
embedded in the kernel or loaded entirely into the small native heap.
An already initialized virtual HDD can boot without the CD.

## Storage and controls

- `C:` is a private, approximately 16 MB FAT16 virtual hard drive.
- On first launch the emulator creates `MSDOS622.HDD` inside an **already
  installed native inteiliDOS filesystem** with sufficient free space.
  It never formats an unrecognized physical disk. Native files are preserved.
- Initial setup copies DOS's boot files and Disk 1 utilities into C:, creates
  a command-prompt startup file, and publishes the image only after successful
  initialization. It excludes the physical-DOS SETUP program and DriveSpace
  driver from this startup environment.
- Install programs using normal DOS commands/installers, directing their
  destination to `C:\GAMES` or another C: directory.
- `A:` is read-only installation/game media. F6 cycles Disk 1, Disk 2, Disk 3,
  and the supplemental disk. F7 mounts the real 1.44 MB floppy drive.
  Missing/unreadable disks produce DOS disk errors.
- `MS-DOS /IMAGE GAME.IMG` mounts a 720 KB or 1.44 MB floppy image stored in
  the CD's root as A:, while still booting from the persistent C: drive.
- F8 stops the guest and returns to inteiliShell, even after guest CLI or HLT.
  Close files and save inside DOS programs first: F8 is an immediate stop,
  not an application-specific save command. Completed sector writes are
  committed to the private HDD, which persists across emulator visits and
  host restarts.
- Text, PC speaker, CGA and Hercules displays are supported. CGA/Hercules
  graphics are rendered through native VGA; font and DAC palette are restored
  on return. No guest PIC/PIT write changes the host's timer or keyboard IRQs.

## Compatibility boundary

This is an 8086/80186-class PC with 640 KB conventional memory. It is **not**
a complete 386/VGA/Sound Blaster machine: protected-mode DOS extenders,
DPMI, EMS/XMS managers, x87, EGA/VGA-specific games, ATAPI/MSCDEX CD drivers,
network cards and sound-card drivers are not implemented. Mount CD-hosted
floppy images rather than expecting DOS to read the native ISO filesystem.
Unsupported CPU instructions stop the emulator with an explicit error.

## Verification

`tests/run_msdos622_test.py` boots the complete OS in QEMU and verifies real
DOS 6.22, an installed/nested COMMAND.COM, DEBUG.EXE, saved files after F8,
re-entry and a host restart, F8 through guest CLI, 80186 instructions, CGA,
speaker register handling, CD-hosted game media, physical floppy access,
disk swaps, and preservation of a native file. Screenshots are generated
outside source control in `generated-artifacts/msdos622`.

## Provenance and licenses

The interpreter is adapted from Adrian Cable's MIT-licensed 8086tiny
revision 1.25, retrieved from
`https://github.com/adriancable/8086tiny`.
The companion BIOS binary (SHA-256
`ba4b2e62246aaadeda8d90bc0928d4f00242c16039982163d7e82740dceb5e31`)
is represented in `bios.h`; retain `LICENSE.txt` with both.
The adaptation explicitly implements the companion ROM's extra 80186
decode entries, bounds disk transfers, separates native device state, and
resets guest RAM for each session.

Microsoft's supplied DOS binaries retain their original license; the
emulator's MIT license does not license DOS itself.
