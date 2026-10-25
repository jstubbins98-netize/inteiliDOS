# Native MS-DOS 6.22 emulator

Enter with `MS-DOS` in inteiliShell. The real MS-DOS system files from the
user's supplied Disk 1 run on a software 486-compatible PC, not the previous
partial native INT 21h compatibility layer. Native IPGM and ELF programs
still launch through LaunchPad.

The generation-4 tiny386 core supports 32-bit protected mode, virtual-8086
mode, paging and 486 instructions. The default 486DX-style build adds an
integer-only software x87; `-DMSDOS_X87=OFF` retains the 486SX-style CPU.
Guest RAM is 16 MiB;
the host needs at least 32 MiB (64 MiB recommended), with 16 MiB contiguous
free RAM above physical 16 MiB. A game's DOS extender/DPMI host is separate
guest software, not a built-in DOS service. See `tiny386/README.md`.

**Experimental:** protected-mode/paging fixtures pass, but the supplied
D386MYH/CWSDPR0 game does not yet reach VGA. DOS-extender game compatibility
is unfinished; see `../../DOS_COMPATIBILITY.md` for the scope and limitations.

## Build and media

Build normally with `build.sh`; the OS ISO contains no DOS disk images.
Users supply their DOS media separately.
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
  Exception: with `/MOUSE` active, plain F12 toggles the emulator's pointer
  instead of reaching DOS. Modified F12 shortcuts still reach the guest.
- PS/2 and USB keys carry event-time Shift/Ctrl/Alt snapshots and physical
  key positions. DOS receives BIOS modified function-key codes (for example
  Shift-F1=`5400h`, Ctrl-F1=`5E00h`, Alt-F1=`6800h`) and correct control-letter,
  punctuation, Tab and arrow combinations. Ctrl takes precedence over Alt,
  then Shift, for function-key combinations. INT 16h `02h`/`12h` and BIOS data
  flags track held/released left/right modifiers, even without a printable key.
  F6/F7/F8 remain reserved with modifiers too.
- Text, PC speaker, CGA, Hercules, EGA and VGA displays are supported. Guest
  graphics are rendered through native VGA; font and DAC palette are restored
  on return. No guest PIC/PIT write changes the host's timer or keyboard IRQs.

## Compatibility boundary

`MS-DOS /MOUSE` installs an optional built-in INT 33h driver. The independent,
case-insensitive switch combines with `/USB`, `/IMAGE` or `/FLOPPY` in either
order. It uses a direct-root xHCI USB HID boot mouse (preferred), a UHCI boot
mouse (including low/full-speed EHCI ports handed to a UHCI companion), or native PS/2
mouse/touchpad input, three buttons, cursor overlays, position/ranges/counters
and far-procedure event callbacks. USB controller ownership remains with the
host, preserving keyboard/storage. UHCI enumeration is boot-only and requires
native USB keyboard support enabled; it never resets ports on DOS entry/exit.
No wheel, hub, OHCI, high-speed EHCI HID, split-transaction mouse support or
complete hardware mouse controller is exposed to the guest.
Without the switch, no resident mouse driver is installed.
The emulator's cursor overlay starts hidden on each launch. F12 toggles
visibility without stopping mouse input. DOS show/hide requests still apply
when the overlay is allowed; guest reset or video-mode changes cannot turn on
the overlay without the user's F12 choice.

The handler is published at DOS's first EXEC, after DOS has initialized its
reserved vectors. A real IVT entry allows normal detection and far-call
chaining. Callbacks return with RETF; the interpreter restores the interrupted
registers and flags and prevents nested callbacks. Native reporting is stopped
on every return to inteiliShell. See `DOS_COMPATIBILITY.md` for the supported
INT 33h subset and input restrictions.

This is a 486SX-compatible CPU with 16 MiB guest RAM and 640 KiB conventional
memory. Protected-mode extenders can use private guest descriptor tables,
paging and extended RAM. A DPMI host or memory manager is separate software;
there is no VESA/SVGA, MPU-401/MIDI or emulated network card. Optional
AdLib/OPL FM shares the Sound Blaster output path. Compatibility
depends on the game's devices and extender, not only its CPU requirement.

### 486SX CPU

`tiny386/i386.c` is the BSD-licensed generation-4 CPU interpreter.
`core486.c` connects it to the existing guest devices, A20 controls and
INT 15h memory BIOS. It tracks protected-mode PIC remapping independently
of whether sound is enabled. Guest I/O never programs native devices directly;
REP processing yields independently of guest IF so F8 remains responsive.
Pending lines are resolved at acknowledgement after any CLI/mode/PIC changes;
a newly masked line is not injected as vector FFh. When an extender chains
IRQ0 to the original BIOS's INT 08h entry, the adapter completes the guest
tick and virtual EOI before the ROM invokes INT 1Ch. The private polling ROM
path does not receive a duplicate tick.

Only exact original firmware sites retain the ROM's private `0F 00`–`0F 03`
host calls. Application code uses the architectural opcode namespace instead.
The checked original BIOS remains unchanged. See `tiny386/README.md` for
provenance, port changes, test commands and remaining core limitations.

### EGA/VGA implementation

`video.c` owns four heap-allocated 64 KiB planes, VGA registers, read latches,
the attribute palette and 256-entry six-bit DAC. CPU memory operands in
`tiny.c` use byte/word bus helpers; MOV/REP, stack, arithmetic, exchange and
shift operations therefore exercise the same VGA memory semantics. Write
modes 0–3, read modes 0/1, chain-4, unchained Mode X, map masks, set/reset,
logical operations, bit masks and basic odd/even addressing are implemented.
Word IN/OUT handles both indexed-register ports, in order.
Aligned disk BIOS reads/writes targeting the VGA aperture use a bounded sector
bounce buffer and the same bus, not an unused conventional-RAM shadow.

INT 10h mode setting supports 0Dh–13h, with their standard 320/640-pixel
rasters. Pixel/page/font/graphics-character/scrolling and palette operations
are handled privately. The unchanged upstream BIOS handles legacy text/CGA
calls. Both INT dispatch and chained PUSHF/FAR CALL entry at F000:0742h
reach the graphics service; the original entry bytes are checked at startup.
No upstream ROM bytes or interpreter decoding tables are replaced.
Graphics BIOS font data is derived from the saved native text font.

`devices.c` fits EGA/VGA output to the physical 320x200 presenter; 640-pixel
modes lose display detail but retain full-resolution guest memory and BIOS
coordinates. Retrace reads are synthesized. This is not cycle-accurate VGA,
VBE, a complete adapter option ROM, or VGA state-save/restore support.
See `../../DOS_COMPATIBILITY.md` for user-facing modes and limits.

## Verification

`tests/run_msdos622_test.py` boots the complete OS in QEMU and verifies real
DOS 6.22, an installed/nested COMMAND.COM, DEBUG.EXE, saved files after F8,
re-entry and a host restart, F8 through guest CLI, 80186 instructions, CGA,
speaker register handling, CD-hosted game media, physical floppy access,
disk swaps, and preservation of a native file. Screenshots are generated
outside source control in `generated-artifacts/msdos622`.

`tests/msdos_video_test.c` runs without hardware and checks plane/latch modes,
DAC programming, BIOS pixels, page selection, mode preservation and fonts.
Build it with `kernel/msdos/video.c` using the host compiler and optionally
AddressSanitizer/UndefinedBehaviorSanitizer. `tests/run_msdos_video_test.py`
uses the normal test kernel and separately supplied DOS disks to run
`msdos_video.asm` under real DOS. It checks visible EGA, VGA and Mode X output,
CPU bus instructions, F8 directly from graphics, rendered native font recovery
and native-file preservation. Screenshots go to `generated-artifacts/msdos-video`.

`tests/msdos_real286_test.c` checks MSW/descriptor state and shift/rotate
results and defined flags, including zero and oversized counts. On x86-64,
compile with `-mno-red-zone` to enable comparison with native x86 instructions.
`tests/run_msdos_real286_test.py` runs the NASM CPU fixture under real DOS,
including relocated IDT dispatch, fault return IPs, string I/O, protected-mode
refusal, 386 rejection, persistence and F8 during REP with CLI.

## Provenance and licenses

### Sound Blaster FM synthesis

`sound.c` mixes private PCM with `fm.c`'s AdLib/OPL model. Both CPU paths
reach it through the shared port dispatcher; no new native I/O permission
or guest DMA/IRQ scheduling is introduced. SB2 exposes OPL2-compatible
registers, while SB16 also exposes OPL3's second bank and stereo/4-op mode.
DSP reset and speaker commands affect PCM only; F8/exit clears both.
FM timers model status polling, not a native IRQ or MPU-401/MIDI device.

`opl3/` contains Nuked OPL3 1.8, Copyright Nuke.YKT, under
**LGPL-2.1-or-later**. Keep its headers, LICENSE and source/relinking materials
when distributing a linked kernel. `opl3/README.md` records the pinned
upstream revision, minimal freestanding changes and distribution requirements.
The adapter remains MPL-2.0; this does not relicense Nuked OPL3.

`python3 tests/run_msdos_fm_test.py --sanitize` runs the PCM/SB16 regressions
and isolated FM checks (timers/aliases, independent DSP, short IRQ probes,
mixing/clipping, volumes, stereo, disabled sound, cleanup and re-entry).
`tests/run_msdos_sound_test.py KERNEL hda --sound2 --fm` and the equivalent
`ac97 --sound16 --fm` record FM alone and FM+PCM together under real DOS,
check both frequencies, then F8 and silent re-entry. `--disabled --fm`
uses a sound-off kernel; `none --sound16 --fm` tests the silent backend.
`tests/run_msdos_mario_fm_test.py KERNEL INSTALLED_NATIVE_DISK` optionally
selects both music and voice through Mario's SETUP and records HDA output.
The supplied disk is copied, never changed; no game files are bundled.

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
[`https://github.com/adriancable/8086tiny`](https://github.com/adriancable/8086tiny).
The companion BIOS binary (SHA-256
`ba4b2e62246aaadeda8d90bc0928d4f00242c16039982163d7e82740dceb5e31`)
is represented in `bios.h`; retain `LICENSE.txt` with both.
The adaptation explicitly implements the companion ROM's extra 80186
decode entries, bounds disk transfers, separates native device state, and
resets guest RAM for each session.

Microsoft's supplied DOS binaries retain their original license; the
emulator's MIT license does not license DOS itself. The earlier uploaded
DOS 4 source is not required to build or run this DOS 6.22 environment.