# 2000s and early-2010s hardware

## Architecture and boot

inteiliDOS supports x86-64-capable CPUs **by running its existing 32-bit IA-32
kernel**. It does not enter long mode. Native programs remain ELF32 and existing
DOS-emulator and LaunchPad interfaces are preserved.

Newer PCs still need legacy BIOS or working UEFI CSM, BIOS disk boot services,
VGA text output, a compatible 8259 PIC and PIT. UEFI-only boot, Secure Boot,
native USB-storage boot and APIC/SMP operation are not implemented. Check the
machine's actual interfaces rather than assuming its manufacture year is enough.
Firmware settings can affect another installed OS; do not change SATA mode
or erase disks just to select a profile.

## Willburd profiles

From the repository:

```bash
cd inteiliDOS
./build.sh --profile=2000s       # Pentium 4/Athlon XP; 512 MB preset
./build.sh --profile=2010s      # Core i-series/Phenom/FX; 2048 MB preset
./build.sh --profile=x86_64 --defaults
```

These presets enable PCI, PCI IDE, AHCI and native USB-keyboard probing and
select BIOS CD boot. The interactive wizard lets you adjust those choices.
`--profile=legacy` restores conservative legacy defaults. Existing build
directory/image names containing `_1990s` are retained for script compatibility.
Build a new ISO and update the installed kernel if booting an older HDD install;
renaming the source folder alone does not update that installation.

Configured RAM is descriptive. The kernel uses only available memory below
4 GiB and excludes firmware/device reservations. The full build needs at least
8 MiB; native applications and the DOS emulator have separate requirements in
[SYSTEM_REQUIREMENTS.txt](SYSTEM_REQUIREMENTS.txt).

## USB keyboards

Native drivers support directly connected HID **boot-protocol keyboards**:

- UHCI/USB 1.1 controllers, common on older Intel PCI systems.
- xHCI controllers, including USB 1/2 keyboards on compatible USB 3 ports.

The drivers inspect the actual keyboard interface/configuration instead of
assuming that the first USB device is a keyboard. Caps/Shift/Ctrl, function keys,
arrows and report rollover handling use the shared decoder. System Health's
System Info tab shows the initialized keyboard backend.

The current driver does not implement OHCI, EHCI, external hubs, USB storage,
arbitrary report-protocol keyboards, or keyboard hotplug/reconnection. Keep the
keyboard attached before boot; reboot after moving it. xHCI ports must already
be routed to that controller. Vendor-specific EHCI/xHCI port-routing registers
are not guessed. MMIO/DMA must be accessible below 4 GiB, 4 KiB controller pages
must be supported and scratchpad requirements must fit the bounded allocation.
PS/2 or working BIOS legacy USB emulation is the fallback on unsupported ports.

## SATA mechanical hard disks

Choose the profile matching the **current** BIOS controller mode:

- IDE/compatibility: use PCI IDE or legacy ATA interfaces.
- AHCI: use the AHCI driver, with up to four detected SATA HDDs.

AHCI uses IDENTIFY information, LBA48 read/write where advertised, aligned
single-sector DMA and explicit write-cache flushes. Out-of-range requests and
failed/unsupported durability checks are rejected. 512-byte logical sectors
are required; 512e drives work at the protocol level, native 4Kn drives do not.
The 32-bit sector API addresses just under 2 TiB even on a larger disk.

Support for a controller does not grant support for arbitrary existing
FAT/NTFS/ext partitions. Existing native filesystem/install safety rules still
apply. AHCI optical media, RAID, NVMe and SCSI drivers are not implemented.
A BIOS may boot a SATA CD successfully even though the kernel cannot browse
that AHCI optical drive afterwards. SMART diagnostics unavailable on a
controller remain explicitly unavailable, not a fabricated healthy result.

## Verified coverage

The regression script runs the real ELF32 kernel under QEMU's x86-64 emulator
with Intel Core 2 and AMD Phenom CPU models. The PS/2 driver is disabled in this
test to prove native USB input. It covers UHCI/xHCI, a mouse before the keyboard,
keyboard/event-ring wrap, the health UI, AHCI inventory, reads/writes above
128 GiB, out-of-range rejection, flush and persisted sectors after shutdown.

```bash
python3 tests/run_modern_hardware_test.py
```

Emulation tests do not establish compatibility with every physical motherboard
or 86Box configuration. Physical USB firmware handoff, SATA spin-up/controller
quirks and cold BIOS HDD boot remain hardware-specific verification work.
