# inteiliDOS

> "the command line of the future!"

inteiliDOS is an independent, bare-metal operating system written in C and
NASM assembly. It brings a classic command-line environment to compatible
PCs from the 1990s, 2000s and early 2010s. It is **NOT** built on Linux or MS-DOS.

## MAIN FEATURES

- inteiliShell: command history, file commands and plain-English aliases.
- **Note: all commands in inteiliShell are case-insensitive meaning you can type them in lowercase and they will be interpreted as uppercase.** 
- Built-in applications: iEdit text editor, InteiliBASIC interpreter,
  InteiliSheets spreadsheet, InteiliFile Manager and InteiliTalk speech.
  inteiliWriter (`WRITER` or `DOCS`) adds word processing and USB TXT/Markdown/PDF/Word
  export. F8 toggles bold and F9 toggles italic; Markdown/PDF preserve styles.
  iEdit and InteiliSheets use F2 to save, F3 to load and F10 to quit.
  In iEdit, F4 deletes the current line.
- LaunchPad: runs native programs from CD-ROM, floppy disk or USB.
  Press U in LaunchPad to select USB; browse folders and press Enter to run.
- Storage: native HDD files, IDE and supported SATA hard drives,
  floppy access, ISO 9660 CDs, and IDE/SATA optical-drive support.
- USB storage: mount supported FAT12/16/32 and exFAT drives, choose individual files
  to copy, save/load in iEdit, BASIC and InteiliSheets, and safely eject.
- MS-DOS 6.22 emulator: a separate software PC with a persistent virtual
  HDD for installed programs and saves. Supply installation images on CD,
  floppy or USB; `MS-DOS /USB` reads images from a mounted USB drive.
  `MS-DOS /USB USB0:\GAME.IMG` loads a USB game disk image as DOS drive A:.
  Press F8 to return to inteiliShell.
- System Health: system, disk, computer-information, cooling and battery tabs,
  with live diagnostics and readings where supported sensors are available.
  Battery percentage and capacity health require a supported Smart Battery.
- Background temperature monitoring and warnings on supported hardware.
- PS/2 and supported USB keyboards, built-in games, demos and audio
  Easter eggs.
- Willburd build configuration and a text-mode installation wizard.
  After a successful installation, SETUP and INSTALL-OS are disabled in that
  HDD's installed copy. Boot installation media to reinstall or install elsewhere.

## HARDWARE AND LIMITATIONS

The kernel is 32-bit IA-32, supporting Intel 80386-compatible CPUs and newer.
Compatible x86-64 computers run this same 32-bit kernel. BIOS or working
legacy/CSM boot, compatible VGA display and PC hardware interfaces are
required. Allow at least 8 MiB RAM; 16 MiB or more is recommended. Applications
and the DOS emulator can need additional memory and storage.

Hardware support depends on the selected build profile and actual devices.
USB storage requires a supported xHCI controller; USB keyboard support alone
does not imply storage support. Pure UEFI-only boot, NVMe and RAID are not
supported. Automatic fan control is not implemented: monitoring does not
replace firmware thermal protection.

MS-DOS disk images are not included in the GitHub repository. Users must
supply their own licensed images. DOS compatibility and disk reading are
works in progress; not every DOS program or hardware configuration will work.
USB installation images remain read-only; the DOS virtual HDD stays on
native storage. Back up existing files before installation or disk changes.

## GETTING STARTED

Read the build guide (`inteiliDOS/BUILD.md`), configure a hardware profile with Willburd, and build
the boot media. Test in an emulator before installing on physical hardware.
Building requires a bare-metal i686-elf toolchain and the dependencies listed
in the build guide.

Useful inteiliShell commands:

```text
  HELP          Show available commands.
  HEALTH        Open System Health.
  USB /MOUNT    Mount supported USB drives.
  USB /COPY     Choose one USB file and its destination.
  USB /EJECT    Safely release USB storage.
  MS-DOS        Enter the DOS emulator.
  MS-DOS /USB   Load supplied DOS installation images from USB.
```

## DOCUMENTATION

Repository paths:

```text
  inteiliDOS/BUILD.md                Build instructions.
  inteiliDOS/SYSTEM_REQUIREMENTS.txt Detailed requirements.
  inteiliDOS/MODERN_HARDWARE.md       Hardware profiles and firmware guidance.
  inteiliDOS/USB_STORAGE.md           USB commands and supported media.
  inteiliDOS/DOS_COMPATIBILITY.md     DOS setup, controls and limitations.
  inteiliDOS/for_developers.md        Development reference.
```

License and attribution information is included in the source documentation.

## CREDITS

The following credits identify the creators and contributor teams of inteiliDOS,
its incorporated software, and its build, installation and testing tools.
Upstream projects maintain their complete individual contributor lists.

### inteiliDOS and incorporated software

- **inteiliDOS** — **Inteilix Software Corporation**, the original author,
  and inteiliDOS contributors.
- **SAM (Software Automatic Mouth) text-to-speech** — **s-macke** and the
  [SAM project contributors](https://github.com/s-macke/SAM), whose C port
  provides the basis for the speech code adapted in `shell/sam/`.
  The original Commodore 64 SAM was published by **Don't Ask Software**
  (later **SoftVoice, Inc.**).
- **8086tiny CPU emulator and companion BIOS** — **Adrian Cable** and
  [8086tiny contributors](https://github.com/adriancable/8086tiny).
  The checked companion BIOS and adapted firmware/device bridge are retained.
  The adapted interpreter, BIOS and local
  compatibility work retain their applicable MIT notices; see
  `kernel/msdos/LICENSE.txt` and `kernel/msdos/README.md`.
- **tiny386 CPU core** — **Chunhui He**
  ([upstream project](https://github.com/hchunhui/tiny386)), providing the
  486 protected-mode/paging interpreter. Retain its BSD-3-Clause
  license in `kernel/msdos/tiny386/LICENSE`.
- **Berkeley SoftFloat Release 3e** — **John R. Hauser / The Regents of the
  University of California** ([upstream](https://www.jhauser.us/arithmetic/SoftFloat.html)).
  Integer-only 80-bit arithmetic for the optional DOS x87 interpreter.
  Retain its BSD-3-Clause notice in `kernel/msdos/softfloat/LICENSE`.
- **Nuked OPL3 1.8** — **Nuke.YKT**
  ([upstream](https://github.com/nukeykt/Nuked-OPL3)), integer-only AdLib/OPL
  FM synthesis for DOS games. Retain its LGPL-2.1-or-later license, source and
  relinking materials; see `kernel/msdos/opl3/LICENSE` and its README.
- **FatFs FAT/exFAT filesystem library and Unicode support** — **ChaN**
  ([FatFs project](https://elm-chan.org/fsw/ff/)), with upstream contributor
  and copyright notices retained in `kernel/fatfs/`. The vendored source
  was obtained through the **Zephyr Project's FatFs mirror**.
- **[MS-DOS 6.22](https://en.wikipedia.org/wiki/MS-DOS#MS-DOS_6.x) — Microsoft Corporation**. This is separately supplied
  guest software, not part of inteiliDOS and not included in this repository.
  Users must supply their own licensed disk images.

### Build, installation and testing software

These are host-side tools, not additional operating systems or libraries
running inside the inteiliDOS kernel.

- **GCC, GNU Binutils, GNU Make, GNU GRUB, GDB, GNU Bison, GNU Texinfo,
  GNU Wget and GNU tar** — the **GNU Project**, their respective authors
  and contributor teams, and the **Free Software Foundation**.
  See the [GNU software directory](https://www.gnu.org/software/) for the
  individual projects and their author notices.
- **NASM** — original authors **Simon Tatham** and **Julian Hall**, and
  the [NASM development team](https://www.nasm.us/).
- **CMake** — **Kitware, Inc.** and the
  [CMake contributors](https://cmake.org/).
- **xorriso / libisoburn** — **Thomas Schmitt** and the
  [libburnia project contributors](https://www.gnu.org/software/xorriso/).
- **QEMU** — original author **Fabrice Bellard** and the
  [QEMU contributors](https://www.qemu.org/).
- **DOSBox** — the [DOSBox team and contributors](https://www.dosbox.com/),
  for optional host-side DOS testing.
- **mtools** — original author **Emmet P. Gray**, **Alain Knaff** and
  [mtools contributors](https://www.gnu.org/software/mtools/).
- **Python** — original creator **Guido van Rossum**, the
  **Python Software Foundation** and [Python contributors](https://www.python.org/).
- **GMP, MPFR, MPC and isl** — their respective upstream authors and teams:
  [GMP](https://gmplib.org/), [MPFR](https://www.mpfr.org/),
  [MPC](https://www.multiprecision.org/mpc/) and
  [isl](https://libisl.sourceforge.io/).
- **flex** — **Vern Paxson** and the
  [flex project contributors](https://github.com/westes/flex).
- **curl** — **Daniel Stenberg** and the
  [curl contributors](https://curl.se/).
- **Homebrew** — original creator **Max Howell** and the
  [Homebrew contributors](https://brew.sh/).
- **yay** — **Jguer** and the
  [yay contributors](https://github.com/Jguer/yay).
- **Xcode Command Line Tools** — **Apple Inc.**, with compiler technology
  from the [LLVM/Clang project](https://llvm.org/) and its contributors.
- **exfatprogs** — the
  [exfatprogs authors and contributors](https://github.com/exfatprogs/exfatprogs),
  used for exFAT filesystem validation in the test environment.

## LICENSE

inteiliDOS project code is licensed under the
[Mozilla Public License Version 2.0 (MPL-2.0)](https://www.mozilla.org/MPL/2.0/).

See the `LICENSE` file for details.

Third-party components, tools, guest software and assets retain their own
copyrights and licenses. The inteiliDOS license does not relicense them or
grant rights to distribute MS-DOS disk images. Preserve the applicable
upstream notices, including the 8086tiny MIT license, Nuked OPL3 LGPL license
and FatFs notices.