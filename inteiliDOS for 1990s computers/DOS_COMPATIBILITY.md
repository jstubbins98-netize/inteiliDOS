# DOS programs in LaunchPad

LaunchPad can now run **16-bit DOS COM and DOS MZ EXE programs** in addition
to native inteiliDOS IPGM and i386 ELF programs. It reads programs from the
existing **FAT12 floppy** and **ISO9660 CD-ROM** sources.

This is an initial compatibility layer, **not a complete MS-DOS implementation
or a promise that every DOS application/game will work**. It follows the
Virtual 8086 monitor approach in the supplied guide. No MS-DOS system files
or third-party DOS kernel are bundled.

## Using it

1. Rebuild inteiliDOS with `bash build.sh`, then boot the new OS image.
2. Put a DOS `.COM` or `.EXE` file, and its data files, on a supported floppy
   or ISO9660 CD-ROM. Names should fit the filesystem's supported naming rules.
3. Open LaunchPad, select the source with F2, and navigate to the program.
4. Press Enter, then confirm the launch.
5. **F8 stops a DOS program**, including a program stuck in a loop or waiting
   for input. Normal DOS termination returns to LaunchPad after a key
   acknowledgement.

The guest sees **A:** for floppy or **D:** for the selected CD-ROM. The directory
containing the program is the guest's root; relative and absolute guest paths
resolve underneath it. Subdirectories and `..` inside that root are supported;
paths cannot escape it or switch to a different physical drive. LaunchPad
currently supplies an empty command line, not an argument prompt.

## Supported initially

- Real 80386 v86 execution of ordinary 16-bit x86 instructions.
- Trapped 16-bit `INTO` overflow checks: continue with unchanged flags when
  OF is clear, or deliver the guest's INT 04h handler when OF is set. An
  unhandled overflow is reported as an error, not silently skipped.
- COM loading at PSP:0100, a PSP/environment, and return through INT 20h.
- MZ EXE headers, segment relocations, initial CS:IP and SS:SP, minimum/maximum
  extra allocation, and executable/stack/relocation bounds checks.
- INT 20h and INT 21h/00h/4Ch process termination and DOS exit codes.
- DOS console character/string output, buffered input, input status,
  standard input, and standard output/error handles.
- Eight simultaneous read-only file handles: open, read, seek, close, EOF,
  and basic handle information. Individual files may be up to 4 MB.
- Conventional-memory allocation/free/resize. A COM program must shrink
  its initial allocation before requesting another block.
- Get/set interrupt vectors and calls through saved monitor vectors.
- Current drive, session-root current-directory reporting, DTA get/set,
  PSP queries, basic version/break/switch-character probes, RTC date/time.
- BIOS VGA mode 3 text services: cursor position, character cells, text
  scrolling/clearing, and teletype output. Direct text VGA memory is available.
- INT 10h/1130h font information for the current BIOS ROM font and the
  upper-128-character ROM font. The current text geometry is 80x25, 16 pixels
  per character. The private BIOS data area and cursor registers agree with
  BIOS cursor changes.
- Virtual text-cursor registers 0Ah/0Bh/0Eh/0Fh at ports 3D4h/3D5h, including
  byte and index/data word I/O. Writes move the bounded text cursor; cursor
  shape is retained virtually, not applied to the host display. Read-only
  3DAh status alternates virtual blank/retrace states for CRT polling loops.
  This does not permit raw VGA timing, palette, or graphics-register access.
- BIOS keyboard input/status, conventional-memory/equipment queries,
  BIOS clock/RTC reads, DOS idle/fast output, absence probes for Windows/DPMI.
- VGA display-combination and 64-byte functionality/state queries. The
  capability table advertises only the implemented mode-3 text display.
  Hercules extension and DOS-network installation probes report absence.
- Guest-private PIC mask registers (21h/A1h) and nonspecific EOI commands.
  These never alter the host interrupt controllers.
- Virtual periodic PIT channel 0 and IRQ0/INT 08h delivery, including hooks,
  old-vector chaining, and BIOS INT 1Ch callbacks. Guest IF/masks and EOI
  control virtual delivery while the host timer and F8 continue independently.
- PC-speaker tones through channel-2 divisor writes and the low two gate bits
  of port 61h. Kernel speaker routines handle the actual tone; parity/NMI
  bits are not forwarded. Audio is stopped on normal exit, faults, or F8.

## Not supported

- Windows NE/LE/LX/PE executables, overlays, protected-mode DOS extenders,
  DPMI, EMS, XMS, or nested EXEC/COMMAND.COM.
- Graphics BIOS modes, alternate video pages, sound-card APIs, raw BIOS
  disk services, general hardware port access, or guest IRQ handlers other
  than the virtual timer. Raw keyboard/controller IRQ support is not provided.
- PIT modes other than supported periodic modes, channel-1 RAM refresh
  programming, PIC initialization/remapping, or virtual timer divisors below
  1194 (about 1 kHz maximum). Device emulation is not cycle-accurate.
- File creation/writing/deletion/renaming, changing the current directory,
  directory enumeration (`findfirst`/`findnext`), batch files, or TSR residency.
- Complete DOS/BIOS API coverage or arbitrary BIOS-ROM execution.

Filesystem writes return DOS access-denied errors; they do not pretend to
save data. Other unsupported services/instructions terminate the guest with
an acknowledged diagnostic showing the interrupt and AX/BX/CX/DX, or the
instruction bytes and I/O port, together with the original CS:IP. CPU faults
include the exception/error code and the fault address for page faults.
When reporting a compatibility failure, include this diagnostic **and the
compiled COM/EXE**: source alone does not reveal compiler/runtime startup
calls. Malformed/non-DOS MZ files are rejected before
execution. `.COM` is identified by the complete filename extension because
COM files have no magic header.

## Host safety and hardware requirements

The monitor temporarily uses ordinary 386 page tables. Guest conventional
memory and VGA text are accessible; kernel, DMA buffers, and device registers
are supervisor-only. A private IVT/BIOS-data page prevents guest changes to
host low-memory metadata. Hardware I/O is denied by the TSS, and host timer
and keyboard IRQs continue even after a guest CLI. The monitor virtualizes
PUSHF/POPF/IRET instead of granting IOPL=3.
PIC masks and PIT channel-0 writes affect only private guest state. The host
PIT continues at 1000 Hz for scheduling, floppy drivers, and abort handling.

The guest has 512 KB of conventional process memory, plus its environment.
It does not obtain all of the host's RAM. Native IPGM/ELF execution remains
unchanged and does not use this compatibility layer.

## Turbo Pascal CRT programs

The supplied Creature EXE was tested through a complete race on both floppy
and CD-ROM: its title/track positions and colors, keyboard/bet input, delay
countdown, normal exit back to LaunchPad, and F8 cancellation all passed.

Unpatched Turbo Pascal CRT delay calibration can overflow on fast CPUs
(commonly called Runtime Error 200). This is independent of the missing
BIOS/port support: a QEMU `-cpu 486` setting selects CPU features, **not**
1990s execution speed. The Creature test bounds QEMU's virtual instruction
speed with `-icount shift=4,align=off,sleep=on`; it does not patch the EXE or
ignore guest arithmetic faults. Use an appropriately paced 1990s CPU in
86Box, or a properly CRT-patched/recompiled executable on faster CPUs.
86Box itself has not been verified here.

This runtime expects the existing non-paged kernel and its normal 1000 Hz
timer profile. It rejects an already-paged host rather than replacing unknown
page tables. Use the OS's supported RAM profile; a 16 MB emulated machine is
used by the regression tests.

## Tests

```bash
python3 tests/run_dos_tests.py
python3 tests/build_test_kernel.py /tmp/inteilidos-test-build
python3 tests/run_launchpad_dos_test.py /tmp/inteilidos-test-build/inteilidOS.elf
```

Tests use synthetic, redistributable DOS fixtures, actual v86 execution, the
real interrupt trampolines, actual ISO9660/FAT12 medium reads, and the real
LaunchPad filesystem bridge in a QEMU 486 guest. They cover COM/MZ loading,
relocations, console output, virtual flags and chained interrupt vectors,
file reads/seeks/EOF/write denial, memory allocation, F8 cancellation during
blocking input and a CLI loop, invalid/protected-memory accesses, private BDA
writes, repeated launches, host state restoration, and native IPGM/ELF
execution afterwards. LaunchPad compiles with floppy support enabled/disabled.

The final test boots the complete OS, opens LaunchPad with normal keyboard
input, launches COM and MZ programs from each medium, acknowledges completion,
and returns to a working shell.

To test the supplied Creature executable without bundling third-party code:

```bash
python3 tests/run_creature_dos_test.py /path/to/CREATURE.EXE /tmp/inteilidos-test-build/inteilidOS.elf
```

These checks do **not** certify arbitrary DOS applications or 86Box itself.

### Still Alive

```bash
python3 tests/run_stillalive_dos_test.py /path/to/STILLALI.EXE /tmp/inteilidos-test-build/inteilidOS.elf --full
```

The supplied EXE completed its full playback from both floppy and CD-ROM,
with advancing text, a captured PC-speaker waveform, and exit code 0.
The test rejects BASIC runtime errors, relaunches to test F8, and verifies
that the shell still works. Its WAV captures and disk images are temporary;
the uploaded EXE is unchanged and is not bundled into the test sources.
