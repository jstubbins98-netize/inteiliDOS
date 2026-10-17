inteiliDOS
==========

inteiliDOS is an independent, bare-metal operating system written in C and
NASM assembly. It brings a classic command-line environment to compatible
PCs from the 1990s, 2000s and early 2010s. It is not Linux or MS-DOS.

MAIN FEATURES
-------------

- inteiliShell: command history, file commands and plain-English aliases.
- Built-in applications: iEdit text editor, InteiliBASIC interpreter,
  InteiliSheets spreadsheet, InteiliFile Manager and InteiliTalk speech.
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
  floppy or USB; MS-DOS /USB reads images from a mounted USB drive.
  MS-DOS /USB USB0:\GAME.IMG loads a USB game disk image as DOS drive A:.
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

HARDWARE AND LIMITATIONS
-----------------------

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

GETTING STARTED
---------------

Read the build guide, configure a hardware profile with Willburd, and build
the boot media. Test in an emulator before installing on physical hardware.
Building requires a bare-metal i686-elf toolchain and the dependencies listed
in the build guide.

Useful shell commands:
  HELP          Show available commands.
  HEALTH        Open System Health.
  USB /MOUNT    Mount supported USB drives.
  USB /COPY     Choose one USB file and its destination.
  USB /EJECT    Safely release USB storage.
  MS-DOS        Enter the DOS emulator.
  MS-DOS /USB   Load supplied DOS installation images from USB.

DOCUMENTATION
-------------

Repository paths:
  inteiliDOS/BUILD.md                Build instructions.
  inteiliDOS/SYSTEM_REQUIREMENTS.txt Detailed requirements.
  inteiliDOS/MODERN_HARDWARE.md       Hardware profiles and firmware guidance.
  inteiliDOS/USB_STORAGE.md           USB commands and supported media.
  inteiliDOS/DOS_COMPATIBILITY.md     DOS setup, controls and limitations.
  inteiliDOS/for_developers.md        Development reference.

License and attribution information is included in the source documentation
and the comprehensive manual.
