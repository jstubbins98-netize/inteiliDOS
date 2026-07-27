# inteiliDOS — Version 1.0

```
 Inteilix Software Corporation
```

---

## Table of Contents

1. [What is inteiliDOS?](#1-what-is-inteilidOS)
2. [Features at a Glance](#2-features-at-a-glance)
3. [Getting Started — Running inteiliDOS](#3-getting-started--running-inteilidOS)
   - [Option D — Real 1990s PC via CD-ROM](#option-d--real-1990s-pc-via-cd-rom)
4. [The IntelliShell](#4-the-intellishell)
   - [Command Reference](#command-reference)
   - [Plain-English Input](#plain-english-input)
   - [Command History](#command-history)
5. [Applications](#5-applications)
   - [IEdit — Text Editor](#iedit--text-editor)
   - [InteiliBASIC — BASIC Interpreter](#inteilibasic--basic-interpreter)
   - [InteiliSheets — Spreadsheet](#inteiliSheets--spreadsheet)
   - [InteiliTalk — Text-to-Speech](#intellitalk--text-to-speech)
   - [TOUR — Text Adventure](#tour--text-adventure)
6. [Kernel Internals](#6-kernel-internals)
7. [Building from Source](#7-building-from-source)
8. [Project Structure](#8-project-structure)
9. [Developer's Guide & Open-Source License](#9-developers-guide--open-source-license)
10. [Attribution & Contact](#10-attribution--contact)

---

## 1. What is inteiliDOS?

**inteiliDOS** is a bare-metal x86 operating system written from scratch in **C** and **NASM assembly**. It boots directly on real hardware or inside a virtual machine — no host operating system underneath it. It is not a Linux distribution, not a POSIX layer, and not an emulator. Every line of code, from the first assembly instruction at boot to the BASIC interpreter prompt, is purpose-built for inteiliDOS.

inteiliDOS was designed to be:

- **Educational** — every subsystem is readable, documented C code with no external runtime dependencies.
- **Nostalgic** — a love letter to the text-mode computing era of the 1980s and early 1990s.
- **Hackable** — a clean foundation for anyone who wants to learn OS development or add their own features.
- **Self-contained** — the kernel, shell, editor, and BASIC interpreter ship as a single bootable ISO under 1 MB.

> **Minimum requirements:** Any x86 (32-bit or 64-bit) machine with 4 MB RAM. Works in QEMU, VirtualBox, VMware, and on real PC hardware from the last 30 years.

---

## 2. Features at a Glance

| Feature | Description |
|---|---|
| **Dual build targets** | `--modern` (i686, -O2) for Pentium Pro and later; `--legacy` (i486, -O1) for 486DX / Pentium era. Each has its own build directory and output artefacts. |
| **Modern ISO** | GRUB2 El Torito no-emulation bootable CD-ROM; works on any x86 machine from ~1996 to today |
| **Legacy ISO + floppy** | GRUB2 minimal-module ISO + raw 1.44 MB floppy image (`mbr_legacy.asm` CHS boot sector); targets mid-to-late 1990s BIOSes |
| **Multiboot1 boot** | Boots via GRUB2 (Multiboot1 header in `boot.asm`); compatible with any Multiboot-compliant bootloader |
| **VGA text mode** | Full 80×25 colour terminal with 16 foreground / 8 background colours |
| **GDT & IDT** | Flat 32-bit protected mode with a full interrupt descriptor table |
| **ISR / IRQ handling** | All 32 CPU exception vectors + 16 hardware IRQ lines wired through a remapped PIC |
| **PIT timer** | Intel 8254 programmable timer at 1 kHz; drives `TIME` and tick-based delays |
| **PS/2 keyboard** | Full US QWERTY keyboard driver; scancode set 1; shift, caps lock, special keys |
| **PCI bus scanner** | Scans PCI configuration space (buses 0–7) via ports 0xCF8/0xCFC; locates devices by class, subclass, and prog_if |
| **USB HID keyboard** | UHCI host controller driver; enumerates USB keyboards in boot-protocol mode; shares the keyboard ring buffer with PS/2 |
| **ATA/IDE detection** | Enumerates IDE drives at boot; distinguishes ATA (hard disks) from ATAPI (CD-ROMs) by signature |
| **ATAPI CD-ROM driver** | PIO PACKET command driver; detects up to four IDE positions; `READ 10` for 2048-byte sectors; `START STOP UNIT` eject; `READ CAPACITY` |
| **Memory manager** | Bitmap physical allocator seeded from the Multiboot1 memory map + 2 MB embedded heap |
| **IntelliShell** | Interactive command shell with 35+ built-in commands, history, NLP input, and universal `quit` |
| **IEdit** | Full-screen 80×25 text editor — 200 lines × 79 columns; type `quit` on a blank line to exit |
| **InteiliBASIC** | Complete BASIC interpreter — variables, arrays, loops, functions, string ops, and a REPL |
| **InteiliSheets** | Full-screen spreadsheet — 7 columns (A–G), 50 scrollable rows, `=SUM` and `=AVG` formula support |
| **InteiliTalk** | PC-speaker text-to-speech — full SAM formant synthesiser (English reciter → phoneme parser → PCM render → PIT PWM playback at 22050 Hz) |
| **DEMO command** | Animated 8-section feature showcase; speaks the tagline live via InteiliTalk |
| **Universal QUIT** | Type `quit` at any prompt in any application to return instantly to IntelliShell |
| **TOUR** | A 14-scene text adventure game set inside your own computer |

---

## 3. Getting Started — Running inteiliDOS

### Option A — QEMU (recommended, no hardware needed)

If you have QEMU installed:

```bash
# Modern build (i686, default)
qemu-system-i386 -cdrom build_modern/inteilidOS.iso -m 128

# Legacy build (i486 — emulates a Pentium CPU)
qemu-system-i386 -cdrom build_legacy/inteilidOS_legacy.iso -cpu pentium -m 64

# Boot from the raw floppy image (legacy only)
qemu-system-i386 -drive file=build_legacy/inteilidOS_floppy.img,format=raw,if=floppy -cpu pentium -m 64
```

To use a **USB keyboard** in QEMU (in addition to the default PS/2 keyboard), add the `-usb` and `-device usb-kbd` flags:

```bash
qemu-system-i386 -cdrom build_modern/inteilidOS.iso -m 128 -serial stdio -usb -device usb-kbd
```

inteiliDOS will detect the USB keyboard automatically during boot and print a confirmation message in green. Both the USB keyboard and the PS/2 keyboard feed the same key buffer, so either can be used at any time.

inteiliDOS boots in a few seconds and drops you into the IntelliShell prompt:

```
inteiliDOS v1.0
(C) Inteilix Software Corporation
"The future still has a blinking cursor."

C:\> _
```

### Option B — VirtualBox or VMware

1. Create a new virtual machine, type: **Other / DOS**, architecture: **32-bit**.
2. Allocate at least **32 MB** of RAM.
3. Attach `build_modern/inteilidOS.iso` as the optical drive.
4. Boot the VM.

### Option C — Real hardware (USB boot)

```bash
sudo dd if=build_modern/inteilidOS.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Replace `/dev/sdX` with your USB drive (find it with `lsblk`). Boot the target machine and select the USB drive in the BIOS boot menu.

> ⚠️ `dd` will erase the target drive. Double-check the device path before running.

### Option D — Real 1990s PC via CD-ROM

inteiliDOS runs beautifully on original PC hardware from the mid-to-late 1990s. Two build modes are available — choose based on your hardware:

| Build | Target hardware | CPU | Output |
|---|---|---|---|
| **Legacy** (`./build.sh --legacy`) | 486DX, Pentium, Pentium MMX (1993–1997) | i486, -O1 | `build_legacy/inteilidOS_legacy.iso` + `build_legacy/inteilidOS_floppy.img` |
| **Modern** (`./build.sh --modern`) | Pentium Pro, Pentium II, Celeron, Core… (1995–present) | i686, -O2 | `build_modern/inteilidOS.iso` |

> **Use the legacy build for any machine from roughly 1993 to 1998.** The modern build uses Pentium Pro (i686) instruction scheduling that will crash on plain Pentium and 486 CPUs. If you are unsure, the legacy build runs safely on any hardware the modern build supports.

Both ISOs use the **El Torito** no-emulation CD-ROM boot standard (introduced 1995). The legacy ISO additionally comes with a raw 1.44 MB **floppy image** (`inteilidOS_floppy.img`) as a fallback for machines whose BIOSes predate El Torito no-emulation support.

#### What you need

| Item | Notes |
|---|---|
| A 1990s x86 PC | Any 486DX or better. Most Pentium / Pentium MMX machines work with the legacy build. |
| A CD-ROM or CD-RW drive | Must be attached and recognised by the BIOS. Most drives from 1996 onward support El Torito. |
| A blank CD-R or CD-RW disc | CD-R is recommended — more compatible with older drives than CD-RW. |
| At least 4 MB of RAM | inteiliDOS fits comfortably in the RAM of virtually any 1990s PC. |
| A modern PC to burn the disc | You will burn the ISO on your current machine, then carry the disc to the old one. |

> **No CD-ROM drive, or BIOS predates El Torito (pre-1996)?** Write the raw floppy image to a physical 3.5″ floppy and boot from that instead: `dd if=build_legacy/inteilidOS_floppy.img of=/dev/fd0 bs=512`. The CHS boot sector (`mbr_legacy.asm`) inside the image works on any PC BIOS from the mid-1980s onward.

> **Very early 1990s machines (pre-1993 / 286/386):** inteiliDOS requires a 32-bit (i386 or better) CPU. 8086/8088/80286 machines are not supported.

#### Step 1 — Build for legacy hardware, then burn

Run the build script and choose **Legacy** when prompted (or pass `--legacy`):

```bash
cd inteiliDOS
./build.sh --legacy
```

This produces `build_legacy/inteilidOS_legacy.iso` (GRUB2 ISO) and `build_legacy/inteilidOS_floppy.img` (raw 1.44 MB image).

Burn the ISO to a CD-R on **Linux / macOS:**
```bash
wodim -v dev=/dev/sr0 -dao speed=4 build_legacy/inteilidOS_legacy.iso
```

On **Windows:**
Right-click `inteilidOS_legacy.iso` → **Burn disc image** → select your CD burner → click **Burn**.

> Burn at the slowest speed your burner allows (4× or 8×). Old CD-ROM drives struggle to read discs burned at high speeds.

**Floppy alternative** (for machines without CD boot support):
```bash
sudo dd if=build_legacy/inteilidOS_floppy.img of=/dev/fd0 bs=512
```

#### Step 2 — Enter the BIOS setup

Insert the freshly burned disc into the 1990s machine's CD-ROM drive, then power the machine on (or reboot it). Watch the screen carefully during the first second or two — a prompt will flash briefly telling you which key to press.

| BIOS brand / era | Key to enter setup |
|---|---|
| AMI BIOS (most 486 / early Pentium) | **Del** |
| Award BIOS (very common 1990s) | **Del** |
| Phoenix BIOS (many laptops and brand-name desktops) | **F2** |
| Compaq / HP branded BIOS | **F10** |
| IBM branded BIOS | **F1** |
| Some machines | **Esc** or **F1** during the memory count |

If the machine boots straight into an old OS before you can react, reboot and try holding the key down continuously from the moment you switch the machine on.

#### Step 3 — Set the CD-ROM as the first boot device

BIOS layouts vary, but the boot order is almost always found under one of these menu names:

- **BIOS Features Setup** (Award BIOS)
- **Boot** tab (Phoenix / modern Award)
- **Advanced CMOS Setup** (AMI BIOS)

Look for a setting called **Boot Sequence**, **Boot Order**, or **First Boot Device**.

**Award / AMI BIOS (text menus):**
1. Use the arrow keys to navigate to **BIOS Features Setup** or **Advanced CMOS Setup** and press **Enter**.
2. Find **First Boot Device** (or **Boot Sequence**).
3. Press **Page Up** / **Page Down** (or `+` / `-`) to cycle through the options until `CDROM` appears as the first device.
4. Set **Second Boot Device** to `HDD-0` (the hard drive) so the machine still boots normally when no disc is inserted.
5. Press **Esc** to return to the main menu.

**Phoenix BIOS (tabbed interface):**
1. Use the left/right arrow keys to select the **Boot** tab.
2. Use the arrow keys to highlight **CD-ROM Drive**.
3. Press `+` to move it to the top of the boot order list.
4. Use the arrow keys to highlight **Hard Drive** and move it to second position with `+`.

#### Step 4 — Save and exit

- **Award / AMI:** Press **Esc** until you reach the main menu, then select **Save & Exit Setup** and press **Enter**. Confirm with **Y** if prompted.
- **Phoenix:** Press **F10** from anywhere in the BIOS. Confirm with **Enter** or **Y**.

The machine will reboot. With the inteiliDOS disc still in the drive, the BIOS will detect it, load GRUB from the CD, and inteiliDOS will start in a few seconds.

#### Step 5 — What you should see

On the **legacy ISO** the GRUB menu reads:

```
GRUB  —  inteiliDOS

  * inteiliDOS (Legacy Mode)
    inteiliDOS (Legacy Mode — Recovery)
```

On the **modern ISO** it reads:

```
GRUB  —  inteiliDOS

  * inteiliDOS 1.0
    inteiliDOS 1.0 (Recovery Mode)
```

Select the first entry with the arrow keys and press **Enter** (or wait — it boots automatically). A `[LEGACY]` or `[MBR]` debug stamp appears briefly at the top left; the kernel clears it when it initialises the VGA driver. You will then see the boot progress screen followed by the inteiliDOS welcome banner and the `C:\>` prompt.

#### Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| Machine boots old OS, ignores disc | Boot order not saved correctly — re-enter BIOS and confirm CD-ROM is first |
| `BOOT FAILURE` or `No boot device` | The disc was burned incorrectly; re-burn at a slower speed |
| GRUB loads but hangs | The CD-ROM drive is too slow or the disc has read errors; try a different disc |
| Screen is blank after GRUB | The VGA mode is not initialised yet; wait 5–10 seconds — the kernel takes a moment to start on slow hardware |
| Machine freezes on memory count | Unrelated hardware issue; try reseating RAM |
| BIOS does not list CDROM as a boot option | The BIOS pre-dates El Torito (pre-1996) — boot from the floppy image instead |
| Machine crashes or reboots immediately after GRUB | You used the modern (i686) build on a Pentium or 486 CPU — rebuild with `./build.sh --legacy` |
| `[LEGACY]` stays on screen permanently | The kernel crashed before `vga_init()` cleared it — check RAM, try reducing QEMU memory |

---

## 4. The IntelliShell

The **IntelliShell** is the command-line interface of inteiliDOS. It reads a line of text, interprets it, and either runs a built-in command or tells you what went wrong.

The prompt looks like this:

```
C:\>
```

Type a command and press **Enter** to run it. Commands are **case-insensitive** — `DIR`, `dir`, and `Dir` all work the same way.

---

### Command Reference

#### Files & Directories

| Command | Usage | Description |
|---|---|---|
| `DIR` | `DIR` | List files and directories in the current path |
| `LS` | `LS` | Alias for `DIR` |
| `CD` | `CD foldername` | Change the current directory |
| `CHDIR` | `CHDIR foldername` | Alias for `CD` |
| `MKDIR` | `MKDIR foldername` | Create a new directory |
| `COPY` | `COPY src dst` | Copy a file |
| `MOVE` | `MOVE src dst` | Move or rename a file |
| `DELETE` | `DELETE filename` | Delete a file |
| `TYPE` | `TYPE filename` | Print the contents of a file to the screen |
| `TREE` | `TREE` | Display the directory tree |

> **Note:** Persistent file I/O requires an ATA/IDE disk driver, which is on the roadmap. In the current release, file operations operate on a simulated in-memory directory structure.

#### Storage

| Command | Usage | Description |
|---|---|---|
| `CDROM` | `CDROM` | List all detected ATAPI CD-ROM drives and disc capacity |
| `CDROM INFO` | `CDROM INFO [n]` | Show detailed capacity info for drive n (default: drive 0) |
| `CDROM EJECT` | `CDROM EJECT [n]` | Eject the disc tray on drive n (default: drive 0) |

> **Drive index:** Drive 0 = Primary Master, 1 = Primary Slave, 2 = Secondary Master, 3 = Secondary Slave. Only ATAPI positions are shown; ATA hard disks are detected at boot but not listed here.

#### System

| Command | Usage | Description |
|---|---|---|
| `CLS` | `CLS` | Clear the screen |
| `MEM` | `MEM` | Display memory usage (total, used, free) |
| `SYSINFO` | `SYSINFO` | Detailed system information: CPU, RAM, shell version |
| `TIME` | `TIME` | Display the current system time (ticks since boot) |
| `DATE` | `DATE` | Display the current system date |
| `HISTORY` | `HISTORY` | Show the last commands you typed |
| `SHUTDOWN` | `SHUTDOWN` | Power off the machine (ACPI S5 shutdown) |
| `RESTART` | `RESTART` | Reboot the machine |

#### Applications

| Command | Usage | Description |
|---|---|---|
| `IEDIT` | `IEDIT` or `IEDIT filename` | Open the IEdit full-screen text editor |
| `BASIC` | `BASIC` | Open the InteiliBASIC interpreter |
| `IBASIC` | `IBASIC` | Alias for `BASIC` |
| `SHEETS` | `SHEETS` | Open InteiliSheets full-screen spreadsheet |
| `ISHEETS` | `ISHEETS` | Alias for `SHEETS` |
| `TALK` | `TALK` | Open InteiliTalk text-to-speech REPL |
| `TALK` | `TALK Hello world` | Speak a phrase directly without entering the REPL |
| `ITALK` | `ITALK` | Alias for `TALK` |
| `DEMO` | `DEMO` | Run the animated inteiliDOS feature showcase |
| `QUIT` | `QUIT` | Return to IntelliShell (works anywhere; also `EXIT`) |

#### Games & Extras

| Command | Usage | Description |
|---|---|---|
| `TOUR` | `TOUR` | Play the text adventure: *A Tour of inteiliDOS* |
| `ABOUT` | `ABOUT` | Show the inteiliDOS version and tagline |
| `HELLO` | `HELLO` | Friendly greeting |
| `COFFEE` | `COFFEE` | Error: coffee module not found |
| `HELP` | `HELP` | Show this command list inside the shell |

---

### Plain-English Input

IntelliShell includes a built-in **NLP (natural-language) translator**. You do not have to memorise exact command names — you can describe what you want in plain English and the shell will translate it.

Examples:

| What you type | What IntelliShell runs |
|---|---|
| `show files` | `DIR` |
| `list files` | `DIR` |
| `make a folder named WORK` | `MKDIR WORK` |
| `where am i` | `CD` |
| `clear the screen` | `CLS` |
| `how much memory` | `MEM` |
| `what time is it` | `TIME` |
| `turn off` | `SHUTDOWN` |
| `quit` | `QUIT` |
| `go back` | `QUIT` |
| `return to shell` | `QUIT` |

When IntelliShell translates a phrase it prints the resolved command in brackets before running it:

```
C:\> show files
  [IntelliShell] -> DIR
  ...
```

---

### Command History

Use the **↑** and **↓** arrow keys to scroll through previously entered commands. The shell stores the last 64 commands in a circular buffer. Type `HISTORY` to see them all printed to the screen.

---

## 5. Applications

### IEdit — Text Editor

**IEdit** is a full-screen text editor modelled after classic DOS editors. It takes over the entire 80×25 display and lets you create and edit text files entirely within inteiliDOS.

**Launching IEdit:**

```
C:\> IEDIT
C:\> IEDIT NOTES.TXT
```

**What you can do:**

- Type and edit text across up to **200 lines**, each up to **79 characters** wide.
- The editor scrolls vertically when you move past the visible window.
- A **line-number gutter** is always visible on the left.

**Keyboard shortcuts:**

| Key | Action |
|---|---|
| Arrow keys | Move the cursor up, down, left, right |
| `Enter` | Insert a new line below the cursor |
| `Backspace` | Delete the character to the left; merge lines at column 0 |
| `Delete` | Delete the character under the cursor |
| `Ctrl + S` | Save the current buffer |
| `Ctrl + K` | Delete (kill) the current line |
| `Ctrl + Q` | Quit IEdit and return to the shell |
| Type `quit` on a blank line | Exit IEdit immediately and return to IntelliShell |

> **Note:** In the current release, `Ctrl+S` saves the buffer to an in-memory store. Disk persistence will be enabled once the ATA driver is complete.

---

### InteiliBASIC — BASIC Interpreter

**InteiliBASIC** is a complete, self-contained BASIC interpreter inspired by classic home-computer BASICs of the 1980s. It supports a wide subset of the language and includes its own interactive REPL.

**Launching InteiliBASIC:**

```
C:\> BASIC
```

You will see:

```
InteiliBASIC v1.0
(C) Inteilix Software Corporation
READY.

OK
```

Type `HELP` at the `OK` prompt for a quick-reference card, or `BYE` / `QUIT` to return to the shell.

---

#### Writing Programs

You can type a program line by line. Lines must start with a **line number**:

```basic
10 PRINT "Hello from inteiliDOS!"
20 FOR I = 1 TO 5
30   PRINT I
40 NEXT I
50 END
```

Then type `RUN` to execute it:

```
OK
RUN
Hello from inteiliDOS!
1
2
3
4
5
OK
```

Use `LIST` to view your program, and `NEW` to clear it and start fresh.

---

#### REPL Commands

| Command | Description |
|---|---|
| `RUN` | Execute the current program from line 1 |
| `RUN 50` | Execute starting from line 50 |
| `LIST` | Print all program lines |
| `LIST 10-50` | Print lines 10 through 50 |
| `NEW` | Erase the program and all variables |
| `SAVE` | Save the program to the in-memory bank |
| `LOAD` | Restore the program from the in-memory bank |
| `DEMO 1`–`4` | Load and run a built-in demo program |
| `HELP` | Show a quick-reference card |
| `BYE` / `QUIT` | Exit InteiliBASIC and return to the shell |

---

#### Language Reference

**Variables**

| Type | Names | Max size |
|---|---|---|
| Integer | `A` – `Z` (single letter) | 32-bit signed |
| String | `A$` – `Z$` | 79 characters |
| Array | `A(n)` – `Z(n)` | Up to 255 elements (use `DIM` first) |

**Statements**

| Statement | Example | Notes |
|---|---|---|
| `PRINT` | `PRINT "Hi"; A; TAB(20); B` | `;` suppresses newline, `,` tabs to next column |
| `INPUT` | `INPUT "Name: "; N$` | Prompts for and reads a value |
| `LET` | `LET A = B * 2 + 1` | `LET` keyword is optional |
| `IF…THEN…ELSE` | `IF A > 10 THEN PRINT "big" ELSE PRINT "small"` | |
| `GOTO` | `GOTO 100` | Jump to a line number |
| `GOSUB` / `RETURN` | `GOSUB 500` | Call a subroutine at line 500; `RETURN` comes back |
| `FOR` / `NEXT` | `FOR I = 1 TO 10 STEP 2` … `NEXT I` | `STEP` defaults to 1 |
| `DIM` | `DIM A(20)` | Declare an array of 20 elements |
| `DATA` / `READ` / `RESTORE` | `DATA 1,2,3` / `READ X` / `RESTORE` | Sequential data list |
| `ON…GOTO` | `ON X GOTO 100, 200, 300` | Branch based on value |
| `ON…GOSUB` | `ON X GOSUB 100, 200, 300` | Call based on value |
| `SWAP` | `SWAP A, B` | Exchange two variables |
| `POKE` | `POKE 0xB8000, 65` | Write a byte directly to memory |
| `CLS` | `CLS` | Clear the screen |
| `END` / `STOP` | `END` | End program execution |
| `REM` / `'` | `REM this is a comment` | Comment line |

**Operators**

| Category | Operators |
|---|---|
| Arithmetic | `+`  `-`  `*`  `/`  `MOD` |
| Comparison | `=`  `<>`  `<`  `>`  `<=`  `>=` |
| Logical | `AND`  `OR`  `NOT` |
| Hex literal | `&H1F` → decimal 31 |

**Numeric Functions**

| Function | Description |
|---|---|
| `ABS(x)` | Absolute value |
| `SGN(x)` | Sign: −1, 0, or 1 |
| `INT(x)` | Truncate toward zero |
| `RND(n)` | Random integer from 1 to n |
| `LEN(a$)` | Length of string |
| `VAL(a$)` | Convert string to number |
| `ASC(a$)` | ASCII code of first character |
| `POS(0)` | Current cursor column |
| `FRE(0)` | Free heap bytes (approximate) |
| `PEEK(addr)` | Read a byte from memory |

**String Functions**

| Function | Description |
|---|---|
| `CHR$(n)` | Character with ASCII code n |
| `STR$(n)` | Convert number to string |
| `LEFT$(a$, n)` | First n characters |
| `RIGHT$(a$, n)` | Last n characters |
| `MID$(a$, s, n)` | n characters starting at position s |
| `SPACE$(n)` | String of n spaces |
| `STRING$(n, c)` | String of n copies of character c |
| `INKEY$` | Read one key (non-blocking; `""` if none) |
| `INPUT$(n)` | Read exactly n characters |

**Built-in Demo Programs**

| Command | What it shows |
|---|---|
| `DEMO 1` | Fibonacci sequence |
| `DEMO 2` | Guess-the-number game (uses `RND`) |
| `DEMO 3` | 10 × 10 times table (nested `FOR/NEXT`) |
| `DEMO 4` | String function showcase |

---

### InteiliSheets — Spreadsheet

**InteiliSheets** is a full-screen spreadsheet application modelled after the column-and-row spreadsheets of the 1980s DOS era. It takes over the entire 80×25 display and writes directly to the VGA buffer for smooth, flicker-free rendering.

**Launching InteiliSheets:**

```
C:\> SHEETS
C:\> ISHEETS
```

**What you can do:**

- Edit a grid of **7 columns** (A–G) and **50 scrollable rows**.
- Type integer values directly into any cell.
- Type `=SUM(A1:A10)` or `=AVG(B2:B8)` to insert calculated formulas.
- Formulas update when their source cells change.

**Keyboard controls:**

| Key | Action |
|---|---|
| Arrow keys | Move the cursor between cells |
| `Enter` | Open the edit bar for the current cell; confirm edits |
| `Escape` | Cancel the current cell edit without saving |
| `Ctrl + Q` | Exit InteiliSheets and return to IntelliShell |
| Type `quit` then `Enter` in a cell | Exit InteiliSheets immediately |

> **Note:** Data is held in memory and lost on reboot until ATA disk I/O is implemented.

---

### InteiliTalk — Text-to-Speech

**InteiliTalk** is a PC-speaker text-to-speech engine that produces real synthesised speech — not a series of beeps. It is a bare-metal port of **SAM (Software Automatic Mouth)**, the formant speech synthesiser from the Commodore 64, running entirely in software with no audio hardware beyond the standard PC speaker.

**Launching InteiliTalk:**

```
C:\> TALK
C:\> ITALK
```

You can also speak a phrase directly from the shell without entering the REPL:

```
C:\> TALK Hello, welcome to inteiliDOS
```

**In the InteiliTalk REPL:**

- Type any sentence and press **Enter** to speak it aloud through the PC speaker.
- Type `EXIT` or `quit` to return to IntelliShell.

**How it works:**

InteiliTalk runs a four-stage pipeline every time you speak a phrase:

```
English text
    │
    ▼
1. Reciter (sam_reciter.c)
   Converts English spelling to SAM phoneme notation using
   letter-to-sound rule tables — the same rules the C64 SAM used.
    │
    ▼
2. Phoneme parser (sam_phoneme.c)
   Parses the phoneme string, applies stress, adjusts durations,
   inserts breath boundaries, and assembles the frame sequence.
    │
    ▼
3. Formant synthesiser (sam_render.c)
   Mixes two sinusoidal formants (F1, F2) and one rectangular
   formant (F3) with pitch contour, amplitude rescaling, and
   sampled consonant bursts into a 131 KB buffer of 8-bit PCM
   audio at 22 050 Hz.
    │
    ▼
4. PIT PWM playback (timer.c → speaker_play_pcm)
   IRQ0 is temporarily reprogrammed to 22 050 Hz. Each interrupt
   loads the next sample's duty cycle into PIT channel 2 (mode 0
   one-shot), which gates the PC speaker on and off to approximate
   the target amplitude. The system timer is corrected for the
   elapsed time after playback completes.
```

The resulting audio sounds like intelligible synthetic speech — the same voice that spoke on Commodore 64 software in the 1980s — produced entirely through a single-bit speaker driven by a reprogrammed hardware timer.

**Voice parameters** (set before speaking, via the `SetSpeed`, `SetPitch`, `SetMouth`, and `SetThroat` API in `shell/sam/sam_phoneme.h`):

| Parameter | Default | Effect |
|---|---|---|
| Speed | 72 | Higher = slower speech |
| Pitch | 64 | Fundamental pitch (0–255) |
| Mouth | 128 | Mouth formant scaling |
| Throat | 128 | Throat formant scaling |

> **No extra hardware required.** InteiliTalk uses only the standard PC speaker (present on virtually all x86 machines since the IBM PC) and the Intel 8254 PIT timer (used by the system clock). The speech synthesiser runs entirely in software on the main CPU.

---

### TOUR — Text Adventure

**TOUR** is a 14-scene interactive text adventure in which **you have been shrunk down to microscopic size and injected into a real PC**. Explore the motherboard, collect components, survive hazards, and make it to the keyboard controller to escape.

**Starting the game:**

```
C:\> TOUR
```

The game displays a scene description and a list of options. Type the number of your choice and press **Enter**.

**Items to collect:** CLOCK crystal, RAM chip, BIOS ROM, boot sector, pixel fragment, IRQ line.

**To win:** Reach the keyboard controller while carrying the **CLOCK** and **BOOT** items.

The game has multiple endings depending on which items you carry and which paths you take. Good luck.

---

## 6. Kernel Internals

This section is for readers who want to understand how inteiliDOS works under the hood.

### Boot Sequence

1. **GRUB2** loads the ISO, finds the Multiboot2 header in `boot.asm`, and jumps to `_start`.
2. `_start` (NASM) sets up a temporary stack, clears `.bss`, and calls `kernel_main()`.
3. `kernel_main()` (C) initialises each subsystem in order, then calls `shell_run()`.

### Subsystems

| Subsystem | Source | What it does |
|---|---|---|
| **VGA driver** | `kernel/vga.c` | Writes characters and attributes directly to the VGA text buffer at `0xB8000`. Supports 16 colours, scrolling, and cursor positioning via I/O ports `0x3D4`/`0x3D5`. |
| **GDT** | `kernel/gdt.c` | Sets up a flat 32-bit protected-mode memory model with code, data, and TSS segments. |
| **IDT** | `kernel/idt.c` | Installs interrupt gate descriptors for all 256 interrupt vectors. |
| **ISR / IRQ** | `kernel/isr.c`, `boot/isr_stubs.asm` | NASM stubs push registers and call C handlers. The PIC (8259A) is remapped so IRQs start at vector 32. |
| **PIT timer** | `kernel/timer.c` | Programs the Intel 8254 PIT to fire IRQ0 at 1 kHz. Maintains a global tick counter used by `TIME`. Supports one registered secondary callback (called on every tick) used by the USB polling loop. |
| **PS/2 keyboard** | `kernel/keyboard.c` | Services IRQ1. Translates US QWERTY scancode set 1 to ASCII; handles shift, caps lock, and special keys. Provides both blocking (`keyboard_getchar`) and non-blocking (`keyboard_poll`) reads. Exposes `keyboard_inject()` so external drivers can share the same ring buffer. |
| **PCI bus scanner** | `kernel/pci.c` | Scans PCI config-space ports 0xCF8/0xCFC across buses 0–7. `pci_find_device(class, sub, prog_if)` returns the BDF of the first matching function; `pci_enable_busmaster()` sets Command register bits 0–2. |
| **USB HID keyboard** | `kernel/usb.c` | UHCI host controller driver. Locates the UHCI controller via PCI, resets it, probes both USB ports, and runs a synchronous control-transfer enumeration sequence (SET_ADDRESS → GET_DESCRIPTOR Device → GET_DESCRIPTOR Config → SET_CONFIGURATION → SET_PROTOCOL boot → SET_IDLE). An interrupt-endpoint TD is then polled every 8 ms through the timer secondary callback; decoded keycodes are injected into the shared keyboard ring buffer. |
| **ATA/IDE detection** | `kernel/ata.c` | Probes all four IDE positions (Primary/Secondary × Master/Slave) via ports 0x1F0/0x170. Identifies ATA hard disks by `IDENTIFY DEVICE` response; ATAPI positions (signature bytes `0x14`/`0xEB`) are skipped here and handled by `cdrom.c`. |
| **ATAPI CD-ROM driver** | `kernel/cdrom.c` | ATAPI PIO driver using the `PACKET` command (`ATA 0xA0`). Detects drives by ATAPI signature at all four IDE positions. Implements `cdrom_init()`, `cdrom_read_sector()` (READ 10, 2048-byte sectors), `cdrom_eject()` (START STOP UNIT), `cdrom_count()`, and `cdrom_drives()`. The `CDROM` shell command exposes all three sub-functions. |
| **Memory manager** | `kernel/memory.c` | Reads the Multiboot1 memory map to build a page-frame bitmap. Also manages a 2 MB statically embedded heap with `kmalloc` / `kfree`. |
| **IntelliShell** | `shell/shell.c` | The REPL: reads a line with inline editing and history, strips whitespace, calls the NLP translator, then dispatches to a command handler. |
| **Commands** | `shell/commands.c` | Implements all 35+ built-in commands, the QUIT/EXIT universal-return command, and the NLP phrase-to-command table. |
| **IEdit** | `shell/iedit.c` | Bypasses the sequential VGA state machine and writes directly to `0xB8000` for full-screen cursor positioning. Detects `quit` typed alone on any line and exits cleanly. |
| **InteiliBASIC** | `shell/basic.c` | Recursive-descent expression parser with full statement execution, string heap, and REPL. All arithmetic is 32-bit integer to avoid 64-bit division intrinsics. |
| **InteiliSheets** | `shell/sheets.c` | Full-screen spreadsheet — 7 columns (A–G), 50 scrollable rows. Direct VGA buffer writes for cell and formula-bar rendering. Integer `=SUM`/`=AVG` formula evaluation. Ctrl+Q or typing `quit` in a cell exits. |
| **InteiliTalk** | `shell/talk.c`, `shell/sam/` | PC-speaker text-to-speech using a full SAM formant synthesiser. English text → reciter (`sam_reciter.c`) → phoneme parser (`sam_phoneme.c`) → formant render (`sam_render.c`) → 22 050 Hz PCM → `speaker_play_pcm()` (PIT channel 2 PWM, IRQ0 at 22 050 Hz). REPL accepts `quit` or `exit` to return to the shell. |
| **TOUR** | `shell/tour.c` | Scene table + item bitmask state machine driving the text adventure. Pressing `q`/`Q` exits at any choice prompt. |

### Memory Map (at runtime)

```
0x00000000 – 0x000FFFFF   Low memory (BIOS, VGA buffer, real-mode IVT)
0x00100000 – ...          Kernel image (loaded by GRUB at 1 MB)
                            ├── .text   (code)
                            ├── .rodata (read-only data)
                            ├── .data   (initialised globals)
                            └── .bss    (zero-initialised globals)
0xB8000                   VGA text framebuffer (80×25 × 2 bytes)
```

### Key Design Decisions

- **No 64-bit division.** The kernel is compiled freestanding without `libgcc`'s 64-bit helper `__udivmoddi4`. All integer arithmetic uses `int32_t` / `uint32_t`.
- **No dynamic linking.** Everything is statically linked into a single ELF binary by `linker.ld` and then stripped into a flat binary for the ISO.
- **No floating point.** The FPU is never initialised. InteiliBASIC uses integer arithmetic only.
- **Direct VGA writes in IEdit and InteiliSheets.** The sequential-write VGA API is bypassed so full-screen applications can position characters arbitrarily without repainting the whole screen from top to bottom on every keystroke.
- **Static allocation only.** All major data structures (keyboard buffer, shell history, BASIC program store, IEdit line buffer, InteiliSheets cell grid) are fixed-size static arrays — no heap-fragmentation risk.
- **Universal `quit` keyword.** Every interactive application recognises the word `quit` at any input prompt as a signal to return cleanly to IntelliShell. The check is implemented per-application inside each event loop — there is no global signal mechanism.
- **Full SAM formant synthesis.** InteiliTalk runs a bare-metal port of the SAM (Software Automatic Mouth) speech synthesiser. An English reciter converts text to phoneme notation, a phoneme parser assembles the frame sequence, and a formant synthesiser mixes sinusoidal and rectangular waveforms into 8-bit PCM. Playback uses PIT channel 2 as a pulse-width modulator with IRQ0 reprogrammed to 22 050 Hz — no audio hardware beyond the standard PC speaker is required.

---

## 7. Building from Source

See **BUILD.md** for the full step-by-step build guide. A quick summary:

### Prerequisites

| Tool | Purpose |
|---|---|
| `i686-elf-gcc` | Cross-compiler (must be built from source — see BUILD.md) |
| `i686-elf-ld` | Cross-linker (comes with `i686-elf-binutils`) |
| `nasm` | Assembles the boot stubs |
| `cmake` ≥ 3.18 | Build system |
| `xorriso` | Creates the bootable ISO |
| `grub-mkrescue` | Installs GRUB2 into the ISO |
| `qemu-system-i386` | Runs the OS in a virtual machine (optional) |

### One-shot build

```bash
cd inteiliDOS
chmod +x build.sh
./build.sh          # interactive — asks Modern or Legacy
./build.sh --modern # non-interactive modern build (i686, -O2)
./build.sh --legacy # non-interactive legacy build (i486, -O1)
```

The script checks and auto-installs all dependencies, then produces:

| Build | Output |
|---|---|
| Modern | `build_modern/inteilidOS.iso` |
| Legacy | `build_legacy/inteilidOS_legacy.iso` + `build_legacy/inteilidOS_floppy.img` |

### Run immediately

```bash
# Modern
qemu-system-i386 -cdrom build_modern/inteilidOS.iso -m 128

# Legacy (Pentium CPU emulation)
qemu-system-i386 -cdrom build_legacy/inteilidOS_legacy.iso -cpu pentium -m 64

# Legacy floppy image
qemu-system-i386 -drive file=build_legacy/inteilidOS_floppy.img,format=raw,if=floppy -cpu pentium -m 64
```

Or use the CMake convenience targets after building:

```bash
cmake --build build_modern -- run-iso    # modern ISO in QEMU
cmake --build build_legacy -- run-legacy # legacy ISO in QEMU
cmake --build build_legacy -- run-floppy # floppy image in QEMU
```

### Debug with GDB

```bash
# Terminal 1 — start QEMU and wait for debugger
qemu-system-i386 -cdrom build_modern/inteilidOS.iso -m 128 -s -S

# Terminal 2 — attach GDB
i686-elf-gdb build_modern/inteilidOS.elf
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

---

## 8. Project Structure

```
inteiliDOS/
├── boot/                     Assembly stubs + boot sectors
│   ├── boot.asm              Multiboot1 header + _start entry point
│   ├── gdt_flush.asm         lgdt + far jump to reload CS
│   ├── idt_load.asm          lidt helper
│   ├── isr_stubs.asm         ISR/IRQ trampolines (32 exceptions + 16 IRQs)
│   ├── mbr.asm               Modern MBR boot sector (LBA48 extended reads, 1996+)
│   └── mbr_legacy.asm        Legacy CHS boot sector (INT 13h AH=02h, 486/Pentium era)
├── kernel/                   Core kernel subsystems
│   ├── kernel.c              kernel_main() — boot sequence
│   ├── vga.c / vga.h         VGA text-mode driver
│   ├── gdt.c / gdt.h         Global Descriptor Table
│   ├── idt.c / idt.h         Interrupt Descriptor Table
│   ├── isr.c / isr.h         ISR/IRQ dispatch + PIC remapping
│   ├── timer.c / timer.h     PIT 8254 timer driver (+ secondary callback)
│   ├── keyboard.c / keyboard.h  PS/2 keyboard driver + keyboard_inject()
│   ├── pci.c / pci.h         PCI config-space bus scanner
│   ├── usb.c / usb.h         UHCI USB HID keyboard driver
│   ├── ata.c / ata.h         ATA/IDE drive detection (enumerates HDD positions)
│   ├── cdrom.c / cdrom.h     ATAPI CD-ROM driver (PACKET, READ 10, EJECT)
│   ├── memory.c / memory.h   Physical allocator + heap
│   └── multiboot.h           Multiboot1 structure definitions
├── shell/                    IntelliShell + applications
│   ├── shell.c / shell.h     REPL: readline, history, dispatch
│   ├── commands.c / commands.h  All built-in commands (incl. QUIT, DEMO, CDROM) + NLP
│   ├── iedit.c / iedit.h     IEdit full-screen text editor
│   ├── basic.c / basic.h     InteiliBASIC interpreter
│   ├── sheets.c / sheets.h   InteiliSheets spreadsheet (7 cols, 50 rows, =SUM/=AVG)
│   ├── talk.c / talk.h       InteiliTalk shell wrapper (calls SAM pipeline + speaker_play_pcm)
│   ├── sam/                  SAM text-to-speech engine
│   │   ├── sam_reciter.c     English text → SAM phoneme notation (letter-to-sound rules)
│   │   ├── sam_phoneme.c/h   Phoneme parser chain + PrepareOutput() PCM pipeline driver
│   │   ├── sam_render.c/h    SAM formant synthesiser → 8-bit PCM in sam_pcm_buf[]
│   │   ├── RenderTabs.h      Render-side lookup tables (formant, sinus, sampleTable…)
│   │   └── SamTabs.h         Parser-side lookup tables
│   └── tour.c / tour.h       TOUR text adventure
├── grub/
│   ├── grub.cfg              GRUB2 boot menu (modern build — i686)
│   └── grub_legacy.cfg       GRUB2 legacy menu (legacy build — i486, text-only)
├── cmake/
│   ├── i686-elf.cmake        CMake toolchain file
│   └── bin2header.py         Converts MBR binary to a C header (mbr_data.h)
├── linker.ld                 Kernel linker script (loads at 0x100000)
├── CMakeLists.txt            CMake build definition (supports BUILD_TARGET=modern|legacy)
├── build.sh                  Interactive build script (--modern / --legacy flags)
├── BUILD.md                  Full build guide
└── README.md                 This file
```

---

## 9. Developer's Guide & Open-Source License

### inteiliDOS is Open Source

inteiliDOS is released as **open-source software**. You are free to:

- **Read** and study the source code.
- **Modify** the code to add new features, fix bugs, or experiment.
- **Distribute** your modified or unmodified copies — as source code, as a compiled ISO, or in any other form.
- **Use** inteiliDOS as a base for your own operating system project.

There are no restrictions on commercial or non-commercial use.

**The only condition is attribution:**

> Any distribution — modified or unmodified — must clearly credit the **Inteilix Software Corporation** as the original author of inteiliDOS. Attribution must appear in at least one of: the documentation, an `ABOUT` screen, a boot splash, or a credits file included with the distribution.

---

### How to Contribute

Contributions are welcome. The best places to start:

1. **ATA disk I/O** — PIO-mode read/write to an IDE hard disk (detection already works; disk I/O is the next step). Enables real file persistence for IEdit, InteiliSheets, and BASIC SAVE/LOAD.
2. **InteiliBASIC BEEP statement** — wire `speaker_beep(freq_hz, duration_ms)` (already in `timer.h` and used by InteiliTalk) into the `BEEP` statement in `shell/basic.c`, which is currently a no-op.
3. **More BASIC statements** — `SCREEN`, `COLOR`, `LOCATE`, `LINE INPUT`, `OPEN`/`CLOSE` (once disk is available).
4. **NLP expansion** — add more plain-English phrases to the translator table in `commands.c`.
5. **New shell commands** — add a handler function in `commands.c` and wire it into the dispatch table. The `CDROM` command is a good example to follow; remember to also add `quit` detection if your command has an interactive loop.
6. **Multitasking scheduler** — a simple round-robin task switcher using the PIT tick would make a great next kernel feature.
7. **Network stack** — a minimal UDP/IP stack on top of an RTL8139 or NE2000 driver.
8. **OHCI/EHCI USB support** — extend `kernel/usb.c` to support non-UHCI host controllers (prog_if `0x10`/`0x20`). See §4.7 of for_developers.md.

---

### Adding a New Shell Command

1. Write your handler in `shell/commands.c` as a static function:

```c
static int cmd_mycommand(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    println("Hello from my command!");
    return 0;
}
```

2. Add it to the dispatch table (around line 620):

```c
if (kstrcmp(cmd, "MYCOMMAND") == 0) return cmd_mycommand(argc, argv);
```

3. Add a line to `cmd_help()` so users can discover it:

```c
println("  MYCOMMAND - Does something cool");
```

4. Add the source file to `CMakeLists.txt` if you split it out:

```cmake
set(SHELL_C_SOURCES
    ...
    shell/mycommand.c
)
```

---

### Adding a New InteiliBASIC Statement

All statement parsing lives in the `exec_statement()` function in `shell/basic.c`. To add a new statement:

1. Add a string match for your keyword:

```c
} else if (kstrcmp(tok, "BEEP") == 0) {
    /* PC speaker: write to port 0x61 */
    /* beep_tone(500); */
}
```

2. If your statement takes arguments, call `next_token()` to consume them, then `eval_expr()` to evaluate any numeric sub-expression.

3. String arguments can be evaluated with `eval_str()`.

---

### Porting to Real Hardware

inteiliDOS is compiled with the `i686-elf` toolchain, which produces a flat 32-bit ELF with no OS dependencies. It should run on any hardware that:

- Is **x86 compatible** (32-bit or 64-bit in legacy mode).
- Has at least **4 MB of RAM**.
- Supports a **Multiboot2-compliant bootloader** (GRUB2 is the default).
- Has a **VGA-compatible text display** (virtually all PC hardware since 1987, and most VM configurations).

If you are porting to a machine without a PS/2 keyboard, inteiliDOS now includes a UHCI USB HID keyboard driver (`kernel/usb.c`) that runs alongside the PS/2 driver. Both sources share the same keyboard ring buffer transparently — no changes to the shell or applications are required to support USB input.

---

### Feature Status

| Feature | Status |
|---|---|
| Multiboot1 boot (GRUB2) | ✅ Complete |
| Modern ISO (GRUB2, El Torito no-emulation) | ✅ Complete |
| Legacy ISO (GRUB2 minimal modules, i486 kernel) | ✅ Complete |
| CHS boot sector — `mbr_legacy.asm` | ✅ Complete |
| Raw 1.44 MB floppy boot image | ✅ Complete |
| VGA text mode (80×25) | ✅ Complete |
| GDT / IDT / protected mode | ✅ Complete |
| ISR / IRQ / PIC remapping | ✅ Complete |
| PIT timer (1 kHz) | ✅ Complete |
| PS/2 keyboard driver | ✅ Complete |
| PCI bus scanner | ✅ Complete |
| USB HID keyboard driver (UHCI) | ✅ Complete |
| ATA/IDE drive detection | ✅ Complete |
| ATAPI CD-ROM driver (READ, EJECT, capacity) | ✅ Complete |
| CDROM shell command | ✅ Complete |
| Physical memory manager | ✅ Complete |
| Heap (kmalloc/kfree, 2 MB) | ✅ Complete |
| IntelliShell + NLP | ✅ Complete |
| Command history (↑/↓) | ✅ Complete |
| 35+ built-in shell commands | ✅ Complete |
| Universal QUIT / EXIT from any app | ✅ Complete |
| IEdit text editor | ✅ Complete |
| InteiliBASIC interpreter | ✅ Complete |
| InteiliSheets spreadsheet (=SUM, =AVG) | ✅ Complete |
| InteiliTalk text-to-speech (SAM formant synthesis, PIT PWM, 22 050 Hz) | ✅ Complete |
| DEMO feature showcase | ✅ Complete |
| TOUR text adventure | ✅ Complete |
| ATA disk I/O (persistent reads/writes) | 🔧 Planned |
| Persistent file system | 🔧 Planned (requires ATA disk I/O) |
| InteiliBASIC BEEP statement (PC speaker) | 🔧 Planned |
| Multitasking scheduler | 🔧 Planned |
| Network stack (TCP/IP) | 🔧 Planned |
| User accounts / permissions | 🔧 Planned |

---

## 10. Attribution & Contact

```
inteiliDOS
Version 1.0

Developed by the Inteilix Software Corporation.

"The future still has a blinking cursor."
```

inteiliDOS was built from scratch — every line of boot assembly, every kernel subsystem, every shell command, every line of the BASIC interpreter — by the Inteilix Software Corporation.

If you fork, extend, redistribute, or build upon this work, please preserve this credit in your documentation or software.

---

*inteiliDOS — because the command line never stopped evolving.*
