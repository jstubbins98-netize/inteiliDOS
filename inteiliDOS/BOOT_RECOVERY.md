# Power-off / BIOS-return boot recovery

An unbootable HDD does not necessarily mean its files were corrupted. The
installed boot path previously depended on firmware/RAM state:

- The MBR read the VBR over its own executing instructions at `0x7C00`.
- The VBR hardcoded BIOS drive `0x80` and relied on BIOS register contents.
- Kernel startup assumed BSS RAM was zero without explicitly clearing it.

The installed MBR now relocates before the VBR read. The NASM VBR initializes
segments/stack/direction, uses an aligned disk-address packet, preserves the
actual BIOS drive and retries reads a bounded number of times. Kernel startup
explicitly clears BSS before using its stack. The VBR also clears the established
1–5 MiB native-kernel RAM reservation to support older installed kernels that
lacked their own BSS initialization. The full OS requires at least 8 MiB.
No boot path formats a disk or writes files.

If the startup chime plays but the BIOS splash/setup prompt remains visible,
the kernel may be running with the firmware's display mode or active page still
selected. The installed VBR now requests BIOS text mode 3/page 0, initializing
the font and palette before loading the kernel. Rebuild the ISO and run
`BOOTREPAIR` as below to update an existing installation's VBR; this display
fix does not require replacing the installed kernel.

## Repair an existing supported installation without reinstalling

1. Preserve an offline backup of the HDD/image, especially if actual file
   corruption or failing hardware is suspected.
2. Rebuild a **new inteiliDOS ISO** containing these fixes, using the correct
   IDE/AHCI and keyboard profile for the computer.
3. Boot that ISO, leaving the installed HDD attached.
4. Run `BOOTREPAIR`.
5. Check the displayed physical ATA slot and partition. Type `YES` and press
   Enter only if this is the intended installation. Any other answer cancels
   without writing the disk.
6. Wait for `Boot repair verified`. Do not power off during the repair.
7. Remove the ISO and boot the HDD.

The command targets the validated installed native **C:** volume. It validates
the partition extents, active native partition, versioned layout descriptor,
kernel-load metadata and Multiboot header. It refuses unsupported/unsafe layouts
and unrecognized boot managers; it does not migrate legacy layouts.

Before changing boot sectors it saves and verifies their exact original bytes
as `C:\boot-backup0.bin` (then 1–9 if earlier names exist). The first 512 bytes
are the old MBR, the next 512 the old VBR. Existing backup names are never
overwritten. If the backup cannot be saved/verified, no boot sectors are written.

Only the MBR bootstrap and VBR are replaced. The partition table, installed
kernel, configuration reserve and existing files are retained. This command
does **not** replace/update the installed kernel or reconstruct damaged file
contents. A failed write requires keeping the ISO and preserving the backup;
boot-sector writes are not a power-loss-atomic transaction.

Fresh installations automatically receive the corrected boot sectors.

## BIOS and hardware checks

Keep the same legacy BIOS/CSM boot mode and SATA IDE/AHCI mode used by the
installation. After leaving BIOS Setup, check that the intended HDD remains
first in the boot order. Changing controller mode or boot priority can prevent
booting without altering the disk's contents.

If booting still fails, capture the exact screen/error and motherboard/emulator
settings. Genuine damaged sectors, controller faults and arbitrary power cuts
during file writes are separate problems; successful emulator tests are not a
guarantee against those failures.

## Regression checks

```bash
python3 tests/build_test_kernel.py /tmp/boot-test
python3 tests/run_boot_repair_test.py /tmp/boot-test/inteilidOS.elf

# AHCI disk and native xHCI keyboard variant:
python3 tests/build_test_kernel.py /tmp/boot-modern --modern
python3 tests/run_boot_repair_test.py /tmp/boot-modern/inteilidOS.elf --ahci
```

These use disposable disks. Coverage includes real setup, repair cancellation,
verified backup, unchanged kernel/configuration/partition extents, existing
native files, repeated BIOS-only cold boots with deliberately dirty RAM, and an
explicit BIOS `DL=0x81` graphics-mode chainload with an unrelated first HDD.
The graphics-mode test verifies BIOS text mode/page zero and captures the
rendered shell to check that the font and display are actually visible.
