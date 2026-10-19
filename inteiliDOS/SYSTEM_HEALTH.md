# System Health

Build/boot the updated inteiliDOS image, then enter `health`.

- **Health dashboard:** tick-based uptime, physical allocatable RAM, kernel
  heap usage/integrity, storage inventory, and actionable problem warnings.
- **Disk health:** select an HDD with Up/Down; inspect its model, capacity,
  sampled sector reads, boot signature, and available SMART results.
- **System info:** CPU vendor/family/model/stepping and brand where CPUID is
  available, RAM, storage counts, live timer ticks and diagnostic refreshes.
- **Cooling:** live readings on supported sensors; unavailable channels are
  labeled explicitly. Automatic fan control is not implemented.
- **Battery:** charge percentage and retained capacity (full-charge capacity
  divided by design capacity), plus discharge/full status and sample age.

Use **Tab**, **Left/Right**, or **1–5** to change tabs, **R** to repeat the
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

CPU load accounting and voltage sensors are not implemented. CPU temperature
and fan readings require supported thermal hardware; see `THERMAL_MONITORING.md`.
Physical allocatable RAM and the separate kernel heap
are reported independently; the app does not claim an exhaustive RAM test
or guarantee that a drive will not fail.

## Battery support

Select **5 Battery**. Read-only Smart Battery word reads at SMBus address
0x0B run every five seconds through firmware-enabled Intel PIIX4 or selected
ICH controllers (ICH through early 2010s 7-series). The driver does not enable
or reconfigure controllers, probe embedded-controller ports, change battery
mode, control charging or execute ACPI firmware methods. It shares access
with thermal monitoring without interrupting active sensor transactions.

Battery percentage is the battery-reported relative state of charge.
Health is the retained full-charge/design capacity ratio, capped at 100%;
it estimates capacity, not electrical safety or remaining service life.
Unknown/invalid capacities and charge readings are unavailable, not zero.
Discharging at 10% or below and retained capacity below 60% produce advisories.
Read errors and stale data hide percentages rather than showing an old value.

Most ACPI-only or vendor-specific embedded-controller laptop batteries are
not supported by this driver. Desktops, unsupported interfaces and failed
reads show **Unavailable**; that does not prove no battery is installed.
Multiple packs and AC adapter detection are not implemented.

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
