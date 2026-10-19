# FatFs

ChaN's FatFs R0.16 with patch 2, vendored from the Zephyr mirror:
https://github.com/zephyrproject-rtos/fatfs/tree/4f60a12c58c3de4246857ad87c40239ecd93349b

`ff.c`, `ff.h`, and `diskio.h` retain their upstream copyright/license.
Local adaptation: ff.c includes ff_port.h instead of the hosted string.h.
The mirror's FATFS window alignment setting is fixed to four bytes.
ff_port.h maps string/memory calls to freestanding kernel helpers.
ffconf.h enables FAT12/16/32 read/write, 512-byte sectors, four USB volumes,
short (8.3) names and synchronous operation. Formatting, exFAT and long
filenames are disabled. No non-USB device is routed through this instance.
