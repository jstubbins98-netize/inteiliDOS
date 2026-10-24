# tiny386 CPU core

Upstream: https://github.com/hchunhui/tiny386

Pinned revision: `1a8f1e064d649fbcea40ee82371be22866e97fdb`.
Copyright 2024–2025 Chunhui He; BSD-3-Clause, retained in `LICENSE`.
Imported CPU sources: `i386.c`, `i386.h`, `i386ins.def`, `simd.inc`.
No upstream PC devices, BIOS ROMs, DOS binaries or upstream FPU code are bundled here.

inteiliDOS selects generation 4 (486DX-style with software x87 by default;
`-DMSDOS_X87=OFF` selects the original no-x87 486SX-style build). The adapter lives in
`../core486.c`; existing disk, graphics, keyboard, mouse and audio services
are retained. The BIOS suggested at https://github.com/b-dmitry1/BIOS is
firmware, not a CPU core, so it is not substituted for this interpreter.
The existing checked 8086tiny ROM and overlay retain their MIT notices.

Port changes:

- Freestanding allocation/memory operations; no libc, host OS or physical I/O.
- Bounded physical accesses, including VGA/A20 boundaries and page-table walks.
- Guest A20 state and a narrow real-mode firmware interception interface.
- Error unwinding to the emulator caller instead of aborting the host.
- Restartable, bounded REP processing so F8 and native audio keep running.
- Correct reset segment limits for switching from real to protected mode.
- Reset leaves CR0.EM clear so no-x87 runtime probes can detect the absent
  coprocessor; guest-enabled EM still raises #NM.
- A masked pending IRQ acknowledgement is not an interrupt vector and does
  not wake HLT or access a protected-mode IDT.
- 486 generation guards for later RDTSC/MSR/SYSENTER/CMPXCHG8B/CMOV instructions;
  CPUID does not advertise unavailable TSC or CMPXCHG8B features; the x87 bit
  is present only after software FPU allocation succeeds.
- INVD/WBINVD invalidate no native caches; no host cache state is exposed.
- Build with `-Os -fno-strict-aliasing -fwrapv` on the i386 host baseline.

This is a compatibility interpreter, not a cycle-accurate 486 implementation.
As upstream notes, hardware tasking and some permission checks are incomplete.
No claim is made that every DOS extender, memory manager or protected-mode
operating system works.

The local `fpu.c`/`fpu.h` are MPL-2.0, not imports of the upstream
host-double FPU. They use the BSD-3-Clause SoftFloat 3e subset in
`../softfloat/` for integer-only 80-bit arithmetic. See its README/LICENSE
and `../../../DOS_COMPATIBILITY.md` for software-x87 coverage and limits.
`python3 inteiliDOS/tests/run_msdos_x87_cpu_test.py --sanitize` exercises
arithmetic, tags/stack, rounding, exceptions, state and paged-memory faults.

CPU-only test (from the repository root):

```sh
nasm -f bin inteiliDOS/tests/msdos_486.asm -o /tmp/msdos486.bin
cc -Os -fno-strict-aliasing -fwrapv inteiliDOS/tests/msdos_486_test.c \
  inteiliDOS/kernel/msdos/tiny386/i386.c -o /tmp/msdos486-test
/tmp/msdos486-test /tmp/msdos486.bin
cc -Os -ffunction-sections -fdata-sections -fno-strict-aliasing -fwrapv \
  inteiliDOS/tests/msdos_486_pic_test.c inteiliDOS/kernel/msdos/tiny386/i386.c \
  -Wl,--gc-sections -o /tmp/msdos486-pic-test
/tmp/msdos486-pic-test
```

The whole-OS test in `tests/run_msdos_486_test.py` uses separately supplied
DOS disks and disposable test storage. An optional third argument is a
user-supplied Doom8088 VGA ZIP containing D386MYH.EXE, CWSDPR0.EXE and DOOM1.WAD.
That optional game check passes in QEMU: it requires changing rendered frames,
not just a BIOS video-mode byte, and tests the game's F10 quit key, restart,
F8, restored text glyphs, persistent C: files and native-file preservation.
For example, from the repository root:

```sh
python3 inteiliDOS/tests/run_msdos_486_test.py path/to/inteilidOS.elf \
  MS-DOS_disks path/to/Doom8088-VGA.zip
```

The default uses the USB keyboard/modern host profile. Append `--ps2` when
testing a legacy PS/2 kernel; that path also selects QEMU's 486 host CPU.
Without a kernel argument, the script builds the matching temporary profile.

The adapter also completes the virtual timer's EOI/tick when an extender
chains to the original BIOS IRQ0 handler, including a real-mode far-call
return path. The ROM's private polling tick is not counted twice.
Compatibility remains experimental beyond these verified fixtures/game;
the default test omits the externally supplied game.
