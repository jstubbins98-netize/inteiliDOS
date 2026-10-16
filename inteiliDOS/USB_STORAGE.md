# USB drive commands

Rebuild inteiliDOS with **PCI support enabled** and boot the updated kernel.
The `2000s` and `2010s` build profiles enable PCI; use a machine with an xHCI
or EHCI controller for USB storage. Native USB keyboard support is recommended, but
a PS/2 keyboard can also operate these commands. Updating only the MBR/VBR
with `BOOTREPAIR` does not install the new kernel drivers.

```
USB
USB /MOUNT
USB /COPY
DIR USB0:
TYPE USB0:\NOTES.TXT
COPY USB0:\NOTES.TXT USB1:\NOTES.TXT
USB /EJECT
```

- **USB** displays usage and the current support limits, without mounting,
  ejecting or changing a drive.
- **USB /MOUNT** scans the first supported xHCI and first supported EHCI
  controller's direct root ports and mounts detected FAT volumes for reading
  and saving. Up to four mounted storage devices across both controllers,
  plus one UHCI/xHCI boot-protocol keyboard, are supported. Each drive
  receives an explicit `USB0:` through `USB3:` identifier; they never replace
  the installed native `C:` or `D:` volumes. Repeating `/MOUNT` refreshes
  media, without resetting live controller engines or the keyboard.
- **USB /COPY** opens a file chooser. Select the source USB drive, enter a
  file number, choose a destination filename and drive, then confirm with
  **Y**. Only that one file is copied. Directories open for browsing; they
  are never copied recursively. Use N/P for pages, 0 for the parent folder,
  or Esc to cancel. A final warning appears before replacing a destination.
  Mounting a drive does not copy any of its files.
- **USB /EJECT** synchronizes drive caches, disconnects storage slots and
  removes their filesystem mounts. Wait for **“Ejected; safe to remove”**
  for each drive before unplugging it. A failed flush/disconnect is reported
  and is **not** a safe-removal confirmation. Input devices are not ejected.
  Repeating `/MOUNT` can reattach a logically ejected drive without reboot.

Options are case-insensitive. `USB /?` also shows help.
Unrecognized options and extra arguments do not perform drive operations.

## Reading and saving

### MS-DOS disk images

`USB /MOUNT` followed by `MS-DOS /USB` loads user-supplied DOS installation
images from a mounted USB drive. `MS-DOS /USB USB1:` selects a particular
drive. Put `D6221.IMG` through `D6223.IMG` and optional `D622S.IMG` in its
root (`DISK1.IMG`, `DISK2.IMG`, `DISK3.IMG`, `SUPPDISK.IMG` also work).
Disk 1 is required; all must be standard 1.44 MB FAT12 images. F6 swaps them
on the same USB drive. The images are read-only and streamed, so the shell's
64 KiB TYPE/COPY limit does not prevent DOS reading them. DOS's persistent
virtual HDD stays on native storage. See `DOS_COMPATIBILITY.md` for details.

`DIR USBn:\PATH` lists real entries; `TYPE USBn:\FILE.TXT` displays text.
`COPY` accepts explicit USB paths or native `C:\NAME` / `D:\NAME` paths when
at least one side is USB. Copying overwrites an existing destination file.
Files larger than **64 KiB** are rejected, not silently truncated. The same
limit applies to TYPE. Listings show up to 64 entries and report that limit.
Subdirectories created on another computer can be traversed using paths.

All three apps offer **[1] C: HDD, [2] D: HDD, [3] USB drive** after asking
for a filename. Choose 3 and then the USB drive number (0 through 3).
Only mounted drives appear. First run `USB /MOUNT` at the shell; if none are
mounted, the dialog explains how to mount one rather than using another disk.

- **iEdit:** press **F2**, enter `NOTES.TXT`, choose **3**, then
  the USB drive number. **F3** opens a saved text file; **F4** deletes the
  current line; **F10** quits.
- **InteiliBASIC:** type `SAVE`, enter `PROGRAM.BAS`, then choose **3** and the
  drive number. `LOAD` uses the same device selection and restores program
  source, which can then be run normally.
- **InteiliSheets:** press **F2**, enter `GRID.CSV`, then choose **3** and the
  drive number. **F3** reopens the CSV from USB; **F10** quits. Existing CSV export rules
  still apply (numeric/formula cells export their current values).

Explicit filenames such as `USB0:\NOTES.TXT` still work and skip the device
question. Native C:/D: selection and each app's existing document limits are
preserved. Cancel does not save anything; failed saves do not report success
or clear an editor/spreadsheet's unsaved-change state.

Both iEdit and InteiliSheets show their function-key shortcuts on screen.
With unsaved changes, press F3 again to confirm discarding and loading, or
F10 again to confirm quitting without saving. Another key cancels the warning.
In Sheets, a function key during cell editing commits the cell before the
save/load/quit action. Escape cancels filename/device dialogs.

There is no implicit current USB working directory. Use explicit `USBn:`
paths. USB paths are not connected to the old simulated MOVE/DELETE/MKDIR
commands; those operations report unavailable rather than claiming success.
USB files are not yet integrated into the graphical File Manager.

## LaunchPad programs

Run `LAUNCHPAD`, then press **U** to mount/select USB storage. Press U again
to cycle mounted USB drives, or use **F2** to cycle all media sources.
Use arrows and **Enter** to browse folders or launch a native ELF32/IPGM
program. **Backspace/Left** goes up a folder; **F3** refreshes the source.
Programs are streamed into the existing separate staging buffer, so the
64 KiB document limit does not apply; the LaunchPad limit is 4 MiB.
USB filenames remain 8.3 names, with up to eight folder levels.
**Esc** exits LaunchPad; run `USB /EJECT` before removing the drive.

This runs native inteiliDOS programs, not Windows executables. DOS programs
run through `MS-DOS`; `MS-DOS /USB USB0:\GAME.IMG` mounts a USB-hosted game
floppy image as DOS A:. See `DOS_COMPATIBILITY.md`.

## Supported hardware and media

- Intel 7-series/Panther Point xHCI (`8086:1e31`, including HM76-based
  machines) receives an initial port-routing handoff. Switchable USB 2
  data lines and USB 3 SuperSpeed terminations are enabled using the
  chipset's advertised routing masks. Other controller IDs are not given
  Intel-specific register writes. Routing is not repeated on mount/eject.
- xHCI 1.x controller with MMIO and DMA below 4 GiB, 4 KiB page support and
  bounded scratchpad requirements. One controller is managed.
- EHCI 1.0 controller with MMIO and DMA below 4 GiB. One controller is managed,
  independently of xHCI. Direct **USB 2.0 high-speed (480 Mb/s)** BOT drives
  work without any xHCI controller. USB 1.x/full-speed storage is unsupported.
  EHCI firmware ownership must be released before register/schedule changes.
  Initial port routing and companion handoff happen before UHCI keyboard
  startup; mount/eject never reroute ports or reset the controller. Companion-
  owned ports are left alone. EHCI does not supply a keyboard driver itself:
  use a UHCI companion keyboard, xHCI keyboard on another controller, or PS/2.
- Direct root-port USB mass storage using **SCSI transparent Bulk-Only
  Transport (BOT)**, alternate setting zero, one bulk IN/OUT pair, **LUN 0**.
  USB 2 devices can work on xHCI ports when firmware routes them there.
- **512-byte logical sectors**, READ/WRITE(10) capacity below 2 TiB.
- FAT12, FAT16 and FAT32; an unpartitioned volume or a supported FAT
  partition in an MBR. The FatFs library handles allocation, mirrored FATs,
  directory updates and synchronization.
- **8.3 short filenames**. Use short aliases for files that have long names.

Not supported: UHCI/OHCI **storage** (UHCI keyboards still work),
external hubs, UAS-only devices, other LUNs, GPT, NTFS, exFAT, native 4Kn
sectors, above-4-GiB controller BARs, and filesystem formatting.
Drive IDs can change after physical unplug/replacement; check `/MOUNT`
output instead of assuming a USB number permanently identifies a device.
Moving or reconnecting the keyboard still requires reboot.

### If no drive is detected

`USB /MOUNT` reports the controller's initialization status, PCI location
and vendor/device ID (when found), connected-port count and enumeration
failures. EHCI reports its own status and PCI/port details separately, so an
absent xHCI controller is not a storage failure when EHCI is running.
Failed xHCI command/transfer events also show a completion code;
zero indicates a timeout. These distinguish a controller that is absent
or disabled in the build, BIOS ownership refusal, readiness/reset/start
failure, and a controller that runs but cannot enumerate the device.

On Intel 7-series systems, firmware can route even the USB 2 wires of a
USB 3 socket to EHCI. The initial handoff above addresses that case; using
a different port alone does not necessarily change the routing.
FAT32/MBR formatting cannot fix a controller initialization failure, so do
not reformat a drive merely because no supported storage was detected.
An update to this driver requires rebuilding and booting the updated OS;
it does not patch an older installed HDD copy automatically.

If BIOS ownership is not released after five seconds, Intel `8086:1e31`
has a one-time firmware-handoff fallback. It disables and verifies legacy
USB SMI enables, acknowledges pending SMI events, sets OS-owned and clears
BIOS-owned, then checks ownership again after a settling interval.
Controller reset, DMA setup and port routing occur only after those checks
succeed. Other controller IDs retain the normal refusal behavior.
If the registers reject the writes, or firmware reclaims ownership, startup
stops with an explicit error. `USB /MOUNT` shows the ownership/control
registers and reports when the Intel fallback was applied.

The fallback affects firmware USB emulation for this OS session; it does
not change saved BIOS settings, disk partitions or files. Native keyboard
drivers handle input after takeover. It is never repeated on mount/eject.

A DMA retirement/command timeout requires reboot; rescanning does
not reset the controller or recycle potentially active DMA buffers. EHCI
waits for its asynchronous schedule to stop before reusing queue heads or
transfer descriptors; a transport error isolates that storage device. Failed
transports cannot be reported safely ejected.
Intel routing and firmware-delay paths are register-model tested; success
on a physical Lenovo G580 has not yet been confirmed.

## Safety and limitations

Mounting is read-only discovery and **never formats or initializes a disk**.
Only explicit file saves/copies write to a mounted USB filesystem. Each write
closes its file and synchronizes metadata and the device before success is
reported. No drive is reported safely ejected after a failed synchronize
command. Devices that reject cache synchronization cannot be confirmed safe.

FAT is not journaled: power loss, unplugging during a write, a full disk or
an I/O failure can leave a partial file or damaged filesystem. A failed save
does not guarantee the old destination survives an overwrite. Back up
important media and use `/EJECT` before removal.

Command/transfer timeouts quarantine affected DMA buffers; controller reset
is not used as a recovery shortcut that would stop the keyboard. Severe
controller failures may require reboot. Transfers are bounded and polled;
keyboard events are dispatched during storage waits. This is not a
multitasking, hub-aware general USB stack.

## Verification

```
python3 tests/build_test_kernel.py /tmp/usb-build --modern
python3 tests/run_usb_controller_tests.py
python3 tests/run_usb_storage_test.py /tmp/usb-build/inteilidOS.elf
python3 tests/run_usb_storage_test.py /tmp/usb-build/inteilidOS.elf --ehci
python3 tests/run_usb_mixed_test.py /tmp/usb-build/inteilidOS.elf
python3 tests/run_modern_hardware_test.py /tmp/usb-build/inteilidOS.elf
```

The USB regression runs on either xHCI or an EHCI-only storage controller
with a companion UHCI USB 1.1 keyboard (no xHCI, PS/2 disabled in the kernel).
It uses disposable QEMU images and a USB-only keyboard:
two real BOT devices, an MBR FAT16 volume and an unpartitioned FAT32 volume,
mount, directory/file reads, multi-cluster copy, shorter overwrite, iEdit
save/open, eject, remount, and independent checks of persisted file contents
and FAT mirrors. It also verifies one-file chooser selection/cancel, untouched
unselected files, USB device dialogs in iEdit/BASIC/Sheets, BASIC execution
after reloading, and spreadsheet CSV round trips. Host controller models
verify BIOS ownership refusal, malformed capabilities, bounded startup
failures, no rescan reset, targeted port eject and DMA retirement quarantine.
Shared BOT tests reject invalid signatures, tags, residue and phase status;
mixed-controller binding tests keep live volumes stable.
The mixed-controller QEMU test copies files in both directions, physically
removes/reinserts the EHCI device, checks honest failed-eject reporting, and
keeps the xHCI keyboard working throughout.
A deterministic filesystem fault model verifies failed flush and
disconnect handling, unsupported media, 4Kn rejection, failed-write quarantine
and size/LBA bounds. Physical controller
handoff, electrical disconnect behavior and real drives remain unverified.
