# SATA optical drives

inteiliDOS includes a read-only SATA ATAPI driver for CD/DVD drives on standard
PCI AHCI controllers. It shares AHCI initialization, command slots and DMA
ownership with the HDD driver rather than resetting the controller on a disc
rescan. IDE/PATA optical drives keep their existing PIO path.

## Build and firmware

Use Willburd's 2000s or early-2010s profile with **PCI**, **AHCI** and **CD-ROM**
support enabled. For example:

```sh
cd inteiliDOS
bash build.sh --profile=2010s
```

The controller must expose its standard AHCI interface. SATA drives presented
through an IDE compatibility controller use the existing IDE driver instead.
Vendor RAID interfaces, USB optical drives and UEFI-only boot are not added by
this driver. Do not change a dual-boot computer's SATA mode without considering
the requirements of its other operating systems.

Rebuild the ISO and boot the new kernel. An existing HDD installation also needs
the updated kernel; `BOOTREPAIR` alone changes boot sectors, not kernel drivers.
No disk formatting is necessary just to use SATA optical media.

## Commands

```text
CDROM         Display detected drives and inserted-disc information
CDROM /LIST   List ISO9660 root contents
CDROM /RUN    Browse folders and launch native ELF programs
CDROM /EJECT  Eject the first readable disc, or the first detected empty drive
```

LaunchPad also includes detected SATA optical sources. Its source bar labels
them `SATA4` through `SATA7`; F2 changes source, F3 refreshes media, and F5 ejects.
An empty tray is still a detected drive. Insert a disc, close the tray and
rescan or run `CDROM` again. Media readiness retries are bounded: missing or
unreadable media is reported, not replaced with cached/sample file listings.
The DOS emulator's existing search for user-supplied image files on ISO9660
media uses this same CD-ROM interface.

## Limits and safety

- Four AHCI devices total are tracked across SATA HDDs and optical drives
  (slots 4–7), in addition to four legacy IDE positions (0–3). SATA slots are
  assigned to discovered devices, not identical to the controller's port number.
- Standard 12-byte ATAPI packet devices with DMA support are required.
  The driver reads one 2048-byte logical block at a time.
- ISO9660 data CDs and ISO9660 data DVDs use the existing filesystem reader.
  UDF-only DVDs, audio playback, disc burning, Blu-ray-specific commands,
  Joliet/Rock Ridge naming and controller/drive hotplug are not implemented.
  Disc insertion/removal in a boot-detected drive is supported.
- Optical devices are not published as HDDs, so native setup/filesystem write
  APIs cannot format or write them. Only read/sense/capacity and tray-control
  packet commands are allowed.
- A failed or short DMA transfer is an error; incomplete data is not copied
  into the caller's buffer. Permanent bounce buffers prevent late DMA from
  targeting an expired caller stack. Normal CHECK CONDITION responses (empty
  tray, changed disc) allow REQUEST SENSE and retry; command timeouts quarantine
  the port's descriptor slot.
- Some laptop optical mechanisms cannot open their tray by software.

## Verification

```sh
python3 tests/build_test_kernel.py /tmp/sata-optical --modern
python3 tests/run_cdrom_command_test.py /tmp/sata-optical/inteilidOS.elf --sata
python3 tests/run_cdrom_command_test.py /tmp/sata-optical/inteilidOS.elf --sata --empty
python3 tests/run_modern_hardware_test.py /tmp/sata-optical/inteilidOS.elf --sata-optical
python3 tests/run_cdrom_tests.py
```

Tests use disposable images. They cover ISO reads, subdirectory ELF execution,
USB-keyboard input, empty-tray startup, eject/reinsert, unchanged data media,
SATA HDD reads/writes with an optical drive on the same controller, high SATA
port numbers, and legacy IDE regressions. The deterministic command model in
`tests/sata_optical_test.c` also checks DMA sizes, packet flags, short transfers,
CHECK CONDITION recovery and forbidden writes.

QEMU coverage is not a guarantee for every physical drive/controller; physical
hardware and 86Box SATA-controller compatibility remain unverified.
