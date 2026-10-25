# MS-DOS 6.22 in inteiliDOS

## Game files from a SATA CD-ROM / DVD-ROM

Insert an ISO 9660 game disc (or mount a game ISO as the emulator's optical
media), then enter `MS-DOS /CDROM`. Supported SATA ATAPI/AHCI and IDE ATAPI
drives expose the first readable ISO as a read-only **D:** drive:

```text
MS-DOS /CDROM
MS-DOS /MOUSE /SOUND2 /CDROM
```

Inside DOS, use the actual directory/executable names shown by `DIR`:

```dos
D:
DIR
CD \GAME
GAME
```

Run game installers from D: and select a directory on the private virtual C:
for installation and saves. F8 still returns to inteiliShell; C: persists.
DOS must already be installed, or its separately supplied boot media must
also be available. A game-only CD is not a DOS boot disk.

Unlike `/IMAGE GAME.IMG`, `/CDROM` reads **the ISO's game files and folders**,
not a floppy image stored on the disc. No IMG file is required. Large game
assets are streamed from the optical drive rather than copied into RAM.
Executables, installers, batch files and their DAT/WAD/VOC/etc. resources are
accessible: restricting the view to executable extensions would break games.

This is a synthesized, read-only FAT16 file-access drive backed by the native
optical driver. It does not require a guest CONFIG.SYS CD driver or MSCDEX.
It is **not MSCDEX emulation**: MSCDEX detection/raw CD APIs, CD-audio tracks,
and disc copy protection are not implemented. The 486 CPU supports protected
mode and DOS extenders; individual games still need compatible devices and memory.

Limits: ISO 9660 with 2048-byte sectors (not UDF-only DVDs), 4096 total
directory/file records including the root, 511 root entries, 32 directory
levels, and about 1.8 GiB of allocated DOS-view clusters. Long filenames
receive stable DOS 8.3 aliases; Joliet/Rock Ridge names are not exposed.
Unsupported layouts and exceeded limits produce an explicit error. D: reports
no writable space, and guest BIOS writes to it are refused.

Keep the disc inserted until F8. Exit DOS before changing it and enter with
`/CDROM` again to rebuild the directory view. SATA/AHCI support must be enabled
in the OS build for SATA drives.

Tests: `tests/test_msdos_cdrom.py` checks directory/asset reads, large files,
FAT chains, aliases and failure handling. `tests/run_msdos_cdrom_test.py`
checks actual DOS game execution, nested assets, seeks, C: saves, write
refusal and F8/re-entry with the optical drive and native HDD sharing AHCI.

The `MS-DOS` command opens a software-emulated 486-compatible PC running the real
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

First-time creation clears about 16 MiB in the reserved private image.
The status line shows progress for clearing, system-file copying and FAT
metadata. SATA clearing uses bounded write batches with a cache flush after
each batch, rather than a flush after every sector. Existing native files
and physical disk partitions are not reformatted.
Press **F8** to cancel during initialization. An incomplete image is not
published as `MSDOS622.HDD`; the next attempt starts initialization again.
Real disk/media errors still abort with the failing phase rather than
continuing into DOS with an incomplete disk.

The inteiliDOS build never bundles DOS images into its OS ISO or kernel.
**MS-DOS installation disk images and game disk images are not included in
this GitHub repository.** Users must supply their own licensed disk images
separately on CD-ROM, floppy disk, or a supported USB drive at runtime.
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
USB /MOUNT
MS-DOS /USB
MS-DOS /USB USB1:
MS-DOS /USB USB0:\ZORK.IMG
```

### Optional mouse

Append `/MOUSE` to the inteiliShell command to enable the built-in DOS
mouse driver for that session:

```text
MS-DOS /MOUSE
MS-DOS /IMAGE GAME.IMG /MOUSE
MS-DOS /MOUSE /USB USB0:\GAME1.IMG USB0:\GAME2.IMG
```

The switch is case-insensitive and can go before or after the media options.
No separate `MOUSE.COM` is required. Without `/MOUSE`, the emulator does not
install its mouse driver or consume native mouse input.

Input prefers a **USB HID boot mouse connected directly to an xHCI root port**,
then a **direct-root UHCI boot mouse**, falling back to a responding **PS/2
mouse or PS/2-compatible touchpad**. USB reports provide relative X/Y and left,
right and middle buttons. The UHCI path requires native USB keyboard support
enabled in the build (as in the modern profiles), PCI I/O/bus mastering, and
the mouse connected **before native OS boot**. Up to eight UHCI controllers
are probed, with two root ports each; one keyboard and one UHCI mouse are
selected, each using one boot interface at alternate setting zero. UHCI
supports low/full-speed (1.5/12 Mb/s) devices, configuration descriptors up to
512 bytes and interrupt packet sizes up to 8 bytes at low speed or 64 bytes
at full speed. Only the three-byte boot mouse report is consumed.

An **EHCI + UHCI companion** arrangement works when EHCI hands the mouse and
keyboard's low/full-speed root ports to UHCI during boot. EHCI itself has
**no HID periodic/split-transaction driver**: high-speed HID devices, EHCI-only
ports without a supported companion, and EHCI + OHCI companions are not mouse
paths. High-speed storage stays owned by EHCI; UHCI storage is unsupported.
Hubs (including internal hubs), non-boot/report-ID devices, Bluetooth mice,
wheel extensions and I2C-only touchpads are not supported by these native
mouse paths. BIOS legacy USB-to-PS/2 translation may work if firmware keeps it
active; this is not guaranteed. If no supported device responds, startup
reports that the mouse is unavailable and DOS continues without a mouse driver.

USB enumeration/polling remains owned by the host; entering/exiting DOS and
storage scans do not reset an active keyboard, mouse or mounted storage port.
F8 stops DOS input consumption, not the shared USB controller. UHCI devices
are enumerated once at OS boot: `/MOUSE` never resets or reprobes their ports.
After unplugging, buttons and pending movement are cleared; reboot to use a
reconnected UHCI mouse. xHCI can discover a mouse on a later `/MOUSE` launch;
connect it before entering that session. Live reconnection is not implemented.

The implemented INT 33h subset includes reset/detection, three buttons,
position/ranges, show/hide, press/release counts, motion counters, movement
ratios/sensitivity and far-procedure event callbacks. A software cursor is
composited over text and graphics without changing guest video memory.
The emulator's pointer starts hidden, even if a DOS program requests that it
be shown. Press **F12** to allow it to appear; press F12 again to hide it.
Movement, clicks and callbacks remain active while it is hidden. DOS programs
can still hide their own driver cursor. This setting resets to hidden on
each new emulator session. It does not hide pointers drawn by a game itself.
With `/MOUSE` active, plain F12 is reserved for this toggle; modified F12
combinations still reach DOS. Without an active mouse driver, F12 reaches DOS
normally.
This is not a full Microsoft mouse-driver implementation: driver-state
save/restore, alternate callback tables and direct PS/2/serial mouse
hardware access are not emulated. Programs that replace the driver with
their own hardware-only driver may not work.

F6/F7 still select media; F8 exits and stops native mouse reporting. A later
plain `MS-DOS` launch does not retain the previous `/MOUSE` setting.

### DOS installation images on USB

Copy your own licensed **1.44 MB FAT12 disk images** into the root of a
supported FAT12/16/32 USB drive. Use `D6221.IMG`, `D6222.IMG`, `D6223.IMG`
and `D622S.IMG`, or the aliases `DISK1.IMG`, `DISK2.IMG`, `DISK3.IMG`
and `SUPPDISK.IMG`. Disk 1 is required; the others are optional.
Do not write Disk 1 over the entire USB drive: these are ordinary image files
inside its FAT filesystem.

Run `USB /MOUNT` first, then `MS-DOS /USB`. It searches mounted drives in
USB0: through USB3: order for Disk 1. Use `MS-DOS /USB USB1:` to choose a
specific drive. Missing files can be skipped during that search, but a
present, corrupt or unreadable image produces an error, not a silent fallback.
An explicitly requested USB installation-media startup requires valid USB Disk 1 even when the
virtual HDD already exists; plain `MS-DOS` still boots the saved HDD without
installation media.

The selected image appears as read-only DOS A:. F6 cycles the four image names
on the **same USB drive**, accepting the aliases above. A failed swap leaves
the previous image selected. F7 still switches to the physical floppy.
Keep the USB drive inserted during use: images are streamed through a small
cache, not loaded entirely into RAM. No image is bundled with the OS or
modified, and there is no automatic bulk copy of the USB drive.

DOS C: still uses `MSDOS622.HDD` on a safe native HDD with enough free space;
this option does not place the virtual HDD or installed games on USB.
F8 returns to inteiliShell as usual. Afterwards, `USB /EJECT` safely releases
USB storage. USB controller/filesystem restrictions are unchanged; see
`USB_STORAGE.md`.

### Game images on USB

After creating the saved DOS C: installation, copy a game floppy image onto
a supported USB drive as an ordinary file. Use a short 8.3 filename, such as
`ZORK.IMG`. Supported image sizes are 360 KB, 720 KB, 1.2 MB and 1.44 MB.
Raw EXE files on the USB filesystem are not mapped directly into DOS; put the
game files inside a FAT floppy image.

```text
USB /MOUNT
MS-DOS /USB USB0:\ZORK.IMG
```

Inside DOS, run:

```text
A:
DIR
ZORK1
```

Replace the USB volume, filename and game command as appropriate. The selected
image becomes read-only A:; it need not contain DOS system files or share a
USB drive with DOS installation images. C: remains the persistent native-HDD
DOS installation. Copy games to C: to install them or store saves there.
Games requiring writes to A: are not supported.

For a multi-disk game, supply up to eight image paths in the required order:

```text
MS-DOS /USB USB0:\DISK1.IMG USB0:\DISK2.IMG USB0:\DISK3.IMG
```

**F6** swaps A: to the next image without restarting DOS or the running
installer/game. After the last image it wraps to the first. DOS is notified
of each successful media change so its cached directory can be refreshed.
Paths may refer to subfolders or other mounted USB volumes; every path
must include its `USB0:`..`USB3:` prefix. Keep the filenames short (8.3).
Only the listed files are used, not every image on the USB drive. The
remaining images are opened when selected; a missing, invalid or unreadable
next image leaves the current disk and position unchanged, allowing a retry.

Keep the USB drives connected while playing. F7 selects the physical floppy;
F6 returns to the next image in a multi-image USB set. F8 exits to inteiliShell.
With only one USB game image, F6 leaves it mounted. Without a game-image
list, F6 continues to cycle the four DOS installation disks as before.
USB images remain read-only; use virtual C: for installation and saves.
Run `USB /EJECT` after exiting DOS.

### CD images and physical floppies

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
| F6 | Cycle installation disks, or advance through the listed USB game images |
| F7 | Select the real 1.44 MB floppy drive as A: |
| F8 | Stop the guest and return to inteiliShell |

F10 is passed through to DOS applications, including installers that require
it to confirm installation. F9 is also passed through; F6, F7 and F8 remain
reserved for the emulator controls above.

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

- The guest provides Intel 486 compatibility: 16/32-bit instructions,
  protected mode, virtual-8086 mode and 4 KiB paging, with 16 MiB guest RAM
  including 640 KiB conventional memory. The default build adds a software
  x87 (486DX-style); `-DMSDOS_X87=OFF` retains the no-FPU 486SX-style build.
  The host remains a native protected-mode inteiliDOS machine; this is a
  software interpreter, not hardware VM86 execution or a host DOS reboot.
- Text output, CGA/Hercules, EGA/VGA graphics, and PC-speaker tones are supported.
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

Not bundled: a DPMI host or EMS/XMS manager. Supply a game's DOS extender
or compatible memory manager separately; CPU support does not install one.
Not implemented: VESA/SVGA graphics, full SB16 peripherals, MPU-401/MIDI and
other sound cards, DOS networking, and native CD-ROM drivers. Optional PCM
and AdLib/OPL FM playback are described below. Windows NE/LE/LX/PE
executables are not supported. Unsupported CPU instructions stop the emulator
with an explicit diagnostic. An unimplemented device is not a promise that
the emulator can run every program probing that device.

### EGA and VGA graphics

The guest has a private 256 KiB VGA framebuffer with four memory planes.
Standard BIOS graphics modes are supported:

| BIOS mode | Guest resolution | Colors |
|---|---|---|
| `0Dh` | 320 x 200 | 16 |
| `0Eh` | 640 x 200 | 16 |
| `0Fh` | 640 x 350 | Monochrome |
| `10h` | 640 x 350 | 16 |
| `11h` | 640 x 480 | Monochrome |
| `12h` | 640 x 480 | 16 |
| `13h` | 320 x 200 | 256 |

Games can use INT 10h graphics services or program the emulated sequencer,
graphics controller, attribute controller, CRTC and DAC. Supported memory
operations include planar read modes 0/1, write modes 0–3, plane masks,
set/reset, raster operations, chain-4 and unchained Mode X addressing.
BIOS pixel, page, cursor, scrolling, graphics-character and palette services
are provided. The VGA identification call reports a VGA color display.

The presenter fits the guest raster to the host's 320 x 200 VGA output.
320 x 200 games retain their pixel resolution; higher-resolution modes are
downsampled, not displayed at native resolution. This is not cycle-accurate
VGA: retrace status is synthesized, not synchronized to a guest pixel clock.
VESA/SVGA modes, VGA state-save/restore extensions and arbitrary adapter-specific
BIOS extensions are not implemented.

No new shell option is necessary: launch games from the DOS prompt as before.
F6/F7 media controls and F8 exit still work while graphics are active.
Graphics, the 486 CPU core and optional digital sound are separate services.
Protected-mode games need their own DOS extender and compatible devices;
do not assume every game works simply because its CPU instructions are available.
Individual games, including specific Mario Teaches Typing releases, still
need application-level testing; graphics support alone is not a compatibility
guarantee.

### Intel 486 compatibility

**Experimental compatibility:** the CPU/protected-mode/paging fixtures and the
separately supplied Doom8088 `D386MYH.EXE` with `CWSDPR0.EXE` pass in QEMU.
The game check verifies changing rendered VGA frames, the game's F10 quit key,
restart, F8/text-font restoration, persistent C: files and native-file
preservation. It installs the game onto disposable writable C: storage; neither
DOS nor game binaries are bundled. This does not certify other DOS extenders,
86Box or physical PCs, or guarantee that arbitrary protected-mode games work.

The BSD-licensed tiny386 CPU core runs in its generation-4 configuration,
replacing the real-mode-only instruction interpreter. It implements 16/32-bit
registers, operand/address-size overrides, FS/GS, descriptor tables, protected
mode, virtual-8086 mode, control registers, page tables and page faults.
486 instructions include BSWAP, XADD, CMPXCHG, INVLPG and cache invalidation
without exposing the native CPU's caches. This is compatibility emulation,
not a cycle-accurate reproduction of a particular 486 stepping.

The default CPU is a **486DX-style guest with an integer-only software x87**.
The optional `-DMSDOS_X87=OFF` CMake setting retains the original 486SX-style
guest without an FPU. Programs requiring hardware task switching or unimplemented
hardware are not guaranteed to work. Protected-mode permission checks are not exhaustive.
Supply any DPMI host required by a game; none is installed automatically.

The optional no-FPU CPU allows runtime coprocessor detection without pre-enabling
software emulation in CR0. Guest software can still enable EM and receive
device-not-available faults. Timer interrupts use the guest PIC's remapped
vectors; chaining back to the original BIOS timer finishes the guest tick and
acknowledges its virtual PIC, without changing the native PIC.

#### Optional software x87 (486DX-style)

`MSDOS_X87` is a CMake build option, **ON by default**. For a smaller no-FPU
build, pass `-DMSDOS_X87=OFF` when configuring the existing build directory.
It does not require a native FPU, SSE, a newer native CPU, libm or a host
operating system. The kernel retains `-march=i386 -msoft-float`, the existing
16 MiB bounded guest allocation, native device bridges, and the linker-enforced
`0x500000` LaunchPad staging boundary. FPU allocation failure stops startup;
it never silently advertises a coprocessor that is absent.

- Eight 80-bit extended registers, TOP, physical tag word, stack overflow/
  underflow, signed zero, infinities, NaNs and integer/real/packed-BCD transfers.
- Add/subtract/multiply/divide, compare/unordered compare, square root,
  remainder, precision control (24/53/64 significand bits), all four rounding
  modes, and software transcendental operations (logarithm, exponential,
  trigonometric, extract/scale).
- Sticky invalid/denormal/divide-by-zero/overflow/underflow/precision status,
  masks and stack-fault reporting. CR0.EM/TS produce #NM; MP/TS govern WAIT.
  Unmasked faults are deferred to the next waiting instruction; no-wait
  status/control/clear/save operations remain usable. CR0.NE selects #MF
  (vector 16) or isolated guest FERR#/IRQ13 through the virtual slave PIC.
  Guest PIC remapping, cascade/slave masks, EOIs and F0h/F1h acknowledgement
  never access the native PIC or FPU.
- FLDCW/FNSTCW, FLDENV/FNSTENV and FRSTOR/FNSAVE support both operand sizes
  (14/28-byte environments, 94/108-byte states), real-mode packed pointers
  and protected-mode selectors/offsets. CPU reset resets the FPU too.
  Faulted loads/stores do not pop or corrupt the x87 stack.
- CPUID reports generation 4, signature `00000400h`, with **only** EDX bit 0
  added when an FPU is actually enabled. BIOS INT 11h/BDA equipment bit 1
  agrees with this choice. TSC/CMPXCHG8B/MMX/SSE/FXSAVE/CMOV and newer
  floating-point instructions are not advertised.

Arithmetic uses the BSD-3-Clause Berkeley SoftFloat **3e** memory-form API,
not tiny386's upstream host-double/libm FPU. See
`kernel/msdos/softfloat/README.md` and `LICENSE` for provenance and licensing.
The x87 state/decoder and software transcendental adapter are MPL-2.0.
Transcendentals are bounded software approximations, **not bit-exact Intel
microcode**, especially near large trigonometric arguments. This is
compatibility functionality, not a guarantee for all FPU-dependent games.
This work does not change or certify any outstanding CWSDPR0 startup issue.

Reproducible tests (from the repository root):

```sh
python3 inteiliDOS/tests/run_msdos_x87_cpu_test.py --sanitize
python3 inteiliDOS/tests/run_msdos_x87_test.py --media MS-DOS_disks
# Or use an already built legacy/PS2 kernel:
python3 inteiliDOS/tests/run_msdos_x87_test.py path/to/inteilidOS.elf --media MS-DOS_disks
nasm -f bin -DDOS=1 inteiliDOS/tests/msdos_x87.asm -o /tmp/X87TEST.COM
```

The original `X87TEST.COM` solves a quadratic (roots 3 and 2), then checks
precision, rounding, formats, tags, faults, comparisons and saved state under
actual DOS. The native test boots with QEMU `-cpu 486,-fpu`, runs this DOS
program twice across F8/re-entry, verifies installed C: persistence, native
file preservation and text-font restoration, and uses disposable disks only.
The host CPU tests additionally check protected-mode decoding, paging-fault
rollback, #NM/FERR# behavior and disassemble freestanding objects to reject
native x87/MMX/SSE instructions. Separately supplied DOS media is still required.

The guest has 16 MiB RAM. INT 15h reports extended memory (88h/E801h/E820h),
provides block moves (87h), and controls A20 (24xxh). Virtual ports 92h and
the keyboard-controller output-port commands also control guest A20.
A20 starts closed for DOS/EXEPACK compatibility and may be enabled by an extender.
These operations affect private guest RAM only.

**Host memory:** at least 32 MiB total, including a contiguous free 16 MiB
region above physical 16 MiB; 64 MiB or more is recommended. The emulator
fails explicitly when this region or its heap workspace cannot be allocated.
Guest RAM is cleared on entry and released on exit; saved DOS HDD files persist.
F8 is polled between instruction batches and during long REP operations,
including when guest interrupts are disabled. Existing media, VGA, sound and
mouse switches remain available.

No new command-line switch is required. Rebuild the OS, then use `MS-DOS`
and the existing floppy/CD/USB media options normally.

### Optional DOS digital sound

When a 2000s, Athlon 64/Core 2, or early-2010s processor family is selected,
Willburd asks whether to enable sound-card emulation immediately after the CPU
question. The default is **No**. Answering **Yes** opens a second question:
**Sound Blaster 2** (the default choice) or **Sound Blaster 16**.
Legacy CPU choices skip both questions.
Non-interactive presets also leave it off.

The generated `configurations/config.h` contains:

```c
#define CONFIG_MSDOS_SOUND 1        /* 0=off by default, 1=on by default */
#define CONFIG_MSDOS_SOUND_MODEL 16 /* 2=Sound Blaster 2, 16=Sound Blaster 16 */
```

Rebuild after changing it. Enabled sound also requires `CONFIG_ENABLE_PCI 1`.
This setting is independent of `CONFIG_AUDIO_MODE`, which selects **CLOAD
capture**, not DOS playback.

Use the configured default with `MS-DOS`, or override it for one session:

```text
MS-DOS /SOUND2
MS-DOS /SOUND16
MS-DOS /MOUSE /SOUND16 /USB USB0:\GAME.IMG
```

The switches enable the chosen device, even if the build default is off.
Native output still requires PCI-enabled firmware support as described below.
Both switches together are rejected before any disk changes. They can appear
before or after media arguments; F8 exits and the next launch uses its own selection.

Supported playback is **PCM plus AdLib/OPL FM**, not every function of the cards:

- SB2: DSP **2.01**, 8-bit unsigned mono, port **220h**, **IRQ5**, **DMA1**.
- SB16: DSP **4.05**, 8/16-bit signed/unsigned mono/stereo PCM, the same port,
  IRQ5 and DMA1 plus word-addressed **DMA5** for 16-bit data.
  Single/auto-init output, explicit sample rate, pause/resume, finish-auto-init,
  separate 8/16-bit interrupt acknowledgements and mixer IRQ-status discovery
  are supported. Master/PCM/FM left/right volume and mixer reset are implemented.
  IRQ/DMA mixer selection registers report the fixed wiring, not other channels.
- Games may read `BLASTER` from their DOS environment or probe the DSP directly:
  reset returns **AAh**, the version query matches the selected card, and E0h/E2h
  identification handshakes support direct-port and DMA-channel detection.
  Probes at other base addresses do not report another sound card.
  E2h writes only to the guest's private memory; it is not recording support.
- DSP reset/version, direct DAC output, time-constant sample rates, single-cycle
  and auto-init 8-bit DMA, pause/continue, speaker control and block interrupts.
- SB2 includes OPL2-compatible 9-channel FM; SB16 includes OPL3 stereo,
  second-bank, 4-operator and rhythm synthesis using integer-only Nuked OPL3.
  AdLib ports **388h/389h**, SB aliases **220h/221h** and **228h/229h**,
  plus SB16's second bank at **38Ah/38Bh** and **222h/223h** are private
  registers, never hardware passthrough. OPL timer 1/2 start, mask, reset and
  overflow status support the normal AdLib detection handshake. Timer status
  does not generate DSP IRQ5. Timer delays use nominal ISA-access time and
  rendered sample time, not cycle-accurate Yamaha/ISA clocks.
  FM is mixed and saturated with PCM at 48 kHz; digital speaker-off and DSP
  reset do not silence music. Session exit/F8 clears both devices.
- For game setup, select Sound Blaster digital effects, address 220, IRQ 5, DMA 1.
  When sound is selected, inteiliDOS updates the private DOS HDD's `AUTOEXEC.BAT`
  with `SET BLASTER=A220 I5 D1 T3` for SB2 or
  `SET BLASTER=A220 I5 D1 H5 T6` for SB16. Choose DMA5/High DMA5 for 16-bit games.
  This applies to new and saved DOS disks, preserves other startup commands,
  and does not duplicate an existing matching line on subsequent launches.
  Switching cards replaces the previous emulator-managed line rather than
  accumulating conflicting settings. Sessions with sound off leave settings alone.
  Some games still require selecting Sound Blaster in their setup program.
  For Mario Teaches Typing, select **Sound Blaster** for **digital voice**,
  address **220h**, IRQ **5**, and enable the Digital Voice option. Its
  displayed factory IRQ default of **7** does not match this emulator.
  Selecting Sound Blaster only for music does not enable digital speech.
  For Mario's SETUP, select **Sound Blaster** separately for the **music card**
  and the **digital voice card**, then enable Music, Sound Effects and
  Digital Voice. Move with arrows, press **Space** to select (checkmark), then
  **Enter** to confirm. BLASTER supplies IRQ5/DMA1 to the tested release.
  Advance the title screens before checking welcome speech. Do not select
  **None** as the music card in that release: it attempts to open an empty
  filename and displays a misleading “Insert Disk #5” prompt, also seen in
  DOSBox with the same installed files.
  Native laptop analog routing is not certified by QEMU recordings.
  This does not install a driver or guarantee an application's compatibility.
- No MPU-401/MIDI interface, recording, DSP/ASP effects or full mixer register
  emulation. FM synthesis does not imply support for a MIDI device or a
  General MIDI/Roland music selection.

Guest DMA reads only private emulator RAM; no guest ISA DMA, PIC or audio-port
writes reach native hardware. Native playback uses bounded 48 kHz stereo
buffers through supported Intel ICH AC'97 controllers or a discoverable analog
Intel HDA pin/DAC path. AC'97 and analog HDA output are verified by QEMU
recordings, including repeated initialization and F8 during playback. Native
HDA interrupts stay masked; response-status polling keeps codec commands
moving, and shutdown disconnects the codec before stopping command DMA.
Native polling refills already-consumed portions of audio buffers rather
than waiting for a complete 10 ms descriptor. This lets short Sound Blaster
DMA-detection transfers complete before game-driver timeouts, while retaining
the full native queue for playback. AC'97 counts are converted from channel
samples to complete stereo frames before any buffer region is reused.
It is not arbitrary PCI sound-card, HDMI, USB-audio or
Bluetooth support. Codec topology and firmware vary; QEMU tests do not certify
a particular physical PC.

After F8, an enabled native output also reports the session's PCM peak,
rendered frames and observed DMA position advances. A zero peak means no
non-silent samples reached the output buffer; it does not establish a speaker
fault. A stalled/error message indicates a native DMA problem. HDA sessions
add the codec ID, selected pin and DAC nodes, stream-error status and codec
command timeout count. A nonzero peak and advancing DMA still do not guarantee
audible output: a physical codec may require board-specific analog routing.
Include these lines when reporting silent playback.

If DOS stops before the first audio poll, the output is reported as
unexercised rather than assumed stalled. An early CPU fault can leave only
the initial silent buffers queued. CPU-fault exits additionally show the
emulator error code, exception number, CS:EIP, CR0/CR2 and EAX/ESP; include
these lines when reporting a crash. A detected codec and zero audio peak
alone do not establish an audio-driver fault.

HDA initialization waits for command-ring reset and supported codec power
transitions, preserves the external amplifier's BTL/channel-swap settings when
enabling EAPD, and checks the selected pin and DAC format/stream assignment
instead of assuming every configuration command succeeded.

If enabled but no supported output is available, an explicit diagnostic reports
silent audio. Guest DMA and interrupt timing still progress, so waiting for an
audio interrupt does not freeze the DOS program. When disabled, DSP detection
fails as an absent device. The existing PC-speaker path remains independent.
F8, faults and normal exit stop native playback; re-entry resets guest audio.

For a backup, stop the guest and back up the host disk image using appropriate
host/emulator tools. Editing or deleting `MSDOS622.HDD` loses the DOS
installation and its saved files; do not treat it as an editable text document.

## Troubleshooting

### Insufficient memory after an HDD-only boot

The 486 emulator requires one contiguous 16 MiB region above physical 16 MiB;
64 MiB of host RAM is recommended. An older installed HDD loader did not pass
the BIOS RAM map to the kernel, so even a PC with plenty of RAM could report
insufficient memory (and show 0 MiB detected at startup).

Rebuild the inteiliDOS ISO from the updated source and boot **that installation
media**, not the HDD. At the inteiliShell prompt run `BOOTREPAIR`, confirm the
displayed target, and type `YES` to apply. The repair backs up and verifies the
old boot sectors and updates only the recognized native bootstrap; it preserves
partitions, native files and the saved `MSDOS622.HDD`. Remove the installation
media and reboot from the HDD. A full erase/reinstall is not required.
Keep a separate backup of important files before any boot repair.

The updated loader supplies the BIOS E820 map; the kernel uses only reported
addressable RAM, not the configured RAM number. Missing/invalid maps still
refuse allocation rather than assuming that memory exists. Genuine low RAM or
no sufficiently large contiguous region can also produce this message.

`DOS audio was silent: no supported output controller` is a separate native
audio-controller issue; updating the memory handoff does not add audio support.

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
