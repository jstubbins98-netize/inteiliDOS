# System Health

Build/boot the updated inteiliDOS image, then enter `health`.

- **Health dashboard:** tick-based uptime, physical allocatable RAM, kernel
  heap usage/integrity, storage inventory, and actionable problem warnings.
- **Disk health:** select an HDD with Up/Down; inspect its model, capacity,
  sampled sector reads, boot signature, and available SMART results.
- **System info:** CPU vendor/family/model/stepping and brand where CPUID is
  available, RAM, storage counts, live timer ticks and diagnostic refreshes.

Use **Tab**, **Left/Right**, or **1–3** to change tabs, **R** to repeat the
read-only disk checks, and **Esc/Q** to return to the shell. The UI refreshes
every 500 ms; disk samples and SMART data refresh every five seconds.

## What the checks mean

Disk checks read LBA 0 and the last driver-addressable sector. They are
samples, not a full surface scan. A missing boot signature can simply mean
a blank/data disk; it is not treated as hardware failure.

IDE/PATA SMART queries are read-only. Supported, already-enabled drives
can report their failure-prediction status and conventional attributes for
temperature, reallocated, pending and uncorrectable sectors. Attribute
interpretation varies by manufacturer; missing attributes are unavailable,
not zero. Temperatures of 60 C or more produce an explicitly advisory
cooling warning. Large raw sector counts are shown as lower bounds rather
than silently truncated.

SMART failure predictions, sampled read errors, corrupt heap metadata and
inconsistent memory accounting produce red problem alerts. Limited SMART
access, sector advisories and low memory produce warnings. Disabled SMART
is left disabled. AHCI SMART transport is not implemented and is labeled
unavailable; AHCI sample reads still use the existing driver.

The app never writes disk sectors, enables SMART, starts disk self-tests,
or resets/reprobes controllers. Inventory comes from boot detection, so
restart after adding hardware. CD-ROM/floppy-only systems are valid and do
not get a false HDD-failure alert.

CPU load accounting, CPU temperature, fan speed and voltage sensors are
not implemented. Physical allocatable RAM and the separate kernel heap
are reported independently; the app does not claim an exhaustive RAM test
or guarantee that a drive will not fail.

## Verification

```bash
python3 tests/run_health_test.py
# Or use an already-built emulator-test kernel:
python3 tests/run_health_test.py /path/to/inteilidOS.elf
```

The real i386 tests validate heap traversal and corrupted pointers/cycles,
SMART checksums and attribute availability, warning/error UI states,
tab/disk navigation, live refresh, manual recheck, exit/re-entry, and help.
QEMU fixtures cover no HDD, one HDD, multiple HDDs, and injected read errors.
Disk images are hashed before and after to confirm no changes. Fixtures and
configuration are temporary and do not replace the release configuration.
