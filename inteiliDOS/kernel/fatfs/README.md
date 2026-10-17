# FatFs

ChaN's FatFs R0.16 with patch 2, vendored from the Zephyr mirror:
https://github.com/zephyrproject-rtos/fatfs/tree/4f60a12c58c3de4246857ad87c40239ecd93349b

`ff.c`, `ff.h`, and `diskio.h` retain their upstream copyright/license.
Local adaptation: ff.c includes ff_port.h instead of the hosted string.h.
The mirror's FATFS window alignment setting is fixed to four bytes.
ff_port.h maps string/memory calls to freestanding kernel helpers.
ffconf.h enables FAT12/16/32 and exFAT read/write, MBR/GPT and unpartitioned
volumes, 512-byte sectors, four USB volumes and synchronous operation.
LFN uses a static working buffer: filesystem calls are serialized and never
run in an IRQ. ffunicode.c is a CP437-only subset of the same pinned upstream
revision, retaining its Unicode uppercase tables and license. Other Unicode
names must be renamed on the host to ASCII/CP437 for reliable access.
Formatting is disabled. No non-USB device is routed through this instance.
The transport remains READ/WRITE(10), below 2 TiB. The adapter checks 64-bit
exFAT sizes before converting to its 32-bit streaming interface; oversized
files are marked in listings rather than wrapping their size. Document
read/write/copy limits remain 64 KiB; complete USB paths remain bounded.
