# Native MS-DOS 6.22 emulator

Enter with `MS-DOS` in inteiliShell. The real MS-DOS system files from the
user's supplied Disk 1 run on a software 8086/80186 PC, not the previous
partial native INT 21h compatibility layer. Native IPGM and ELF programs
still launch through LaunchPad.

## Build and media

Build normally with `build.sh`; the OS ISO contains no DOS disk images.
Users supply their DOS media separately. Reference images are retained
outside the OS source folder in `../MS-DOS_disks`, not in the OS distribution.
No DOS binaries are downloaded.

To make a separate DOS media CD, put your four licensed images in a folder,
named `Disk1.img`, `Disk2.img`, `Disk3.img`, and `Suppdisk.img`, then run:

```sh
python3 cmake/create_msdos_cd.py /path/to/dos-disks /path/to/dos-media.iso
```

Alternatively insert a 1.44 MB DOS Disk 1 floppy and run `MS-DOS /FLOPPY`.
Keep the separate DOS media inserted for installation access. Images are read on demand, not
embedded in the kernel or loaded entirely into the small native heap.
For USB, put the same image filenames (or DISK1/2/3 and SUPPDISK aliases)
in the drive root, run `USB /MOUNT`, then `MS-DOS /USB [USB0:..USB3:]`.
The default searches mounted drives for Disk 1; F6 loads subsequent images
from the selected drive. USB A: remains read-only and uses bounded random
reads with a 2 KB cache, closing every FAT handle after each operation.
It never allocates a full 1.44 MB floppy in the 2 MB native heap.
Keep the drive connected until exiting with F8; the persistent DOS HDD
remains on native storage. Missing or invalid explicit USB media is an error,
not a fallback to CD or floppy.
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
  and the supplemental disk. F7 selects the physical floppy drive and
  reprobes its media; press it again after changing the mounted disk.
  Missing/unreadable disks produce DOS disk errors.
- `MS-DOS /IMAGE GAME.IMG` mounts a 360 KB, 720 KB, 1.2 MB or 1.44 MB floppy image stored in
  the CD's root as A:, while still booting from the persistent C: drive.
- F8 stops the guest and returns to inteiliShell, even after guest CLI or HLT.
  Close files and save inside DOS programs first: F8 is an immediate stop,
  not an application-specific save command. Completed sector writes are
  committed to the private HDD, which persists across emulator visits and
  host restarts.
- F1-F5 and F9-F12 reach DOS applications. F11/F12 return BIOS codes
  `8500h`/`8600h` through both legacy INT 16h `00h`/`01h` and enhanced
  `10h`/`11h` read/check services. F6/F7/F8 remain reserved host controls.
- PS/2 and USB keys carry event-time Shift/Ctrl/Alt snapshots and physical
  key positions. DOS receives BIOS modified function-key codes (for example
  Shift-F1=`5400h`, Ctrl-F1=`5E00h`, Alt-F1=`6800h`) and correct control-letter,
  punctuation, Tab and arrow combinations. Ctrl takes precedence over Alt,
  then Shift, for function-key combinations. INT 16h `02h`/`12h` and BIOS data
  flags track held/released left/right modifiers, even without a printable key.
  F6/F7/F8 remain reserved with modifiers too.
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

### Keyboard BIOS compatibility overlay

The original ROM in `bios.h` is retained unchanged. `bios_keyboard.asm`
adds INT 7h input delivery, INT 9h BIOS-word insertion and INT 16h enhanced
read/check/shift-state dispatch. The interpreter
checks the original hook instructions and ROM size before applying the
overlay; a different ROM fails explicitly instead of receiving stale hooks.
The extension lives at F000:2000, with a private input mailbox at F000:2400,
past the ROM and its RTC scratch area. It retains physical key make/break
delivery through guest INT 9h hooks and the original ring-pointer wrapping,
without changing the instruction decoder tables or key-release filtering.
The host translates physical positions to BIOS scan:ASCII words separately
from legacy character reads; native `keyboard_poll`/`keyboard_getchar`
consumers keep their old character/shortcut values and skip state-only input.
The original pending-slot and BIOS-buffer capacity limits are unchanged.
Regenerate the checked-in overlay header from the project directory:

```sh
nasm -f bin kernel/msdos/bios_keyboard.asm -o /tmp/msdos-keyboard.bin
python3 cmake/bin2header.py /tmp/msdos-keyboard.bin msdos_bios_keyboard kernel/msdos/bios_keyboard.h
```

`tests/run_msdos_function_keys_test.py` verifies header reproducibility,
real DOS legacy/enhanced peek/read/empty-buffer behavior for both PS/2 and
USB-only profiles, reserved media keys, F8 exit, and native file preservation.
`tests/run_keyboard_modifiers_test.py` checks the actual PS/2 decoder and USB
report translator with the shared queue on the host.
`tests/run_msdos_modifiers_test.py` checks 45 exact BIOS key/state combinations
per keyboard type under real DOS 6.22 in QEMU, including legacy/enhanced
peek/read/empty behavior, physical scans observed by a guest IRQ1 hook,
modifier-only transitions, independent left/right releases, modified host
controls, and return to shell typing/native editor shortcuts.

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
emulator's MIT license does not license DOS itself. The earlier uploaded
DOS 4 source is not required to build or run this DOS 6.22 environment.
