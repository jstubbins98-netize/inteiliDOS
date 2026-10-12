MS-DOS 6.22 installation media
============================

This folder contains the four supplied disk images:
  Disk1.img
  Disk2.img
  Disk3.img
  Suppdisk.img

The default ISO build validates these images and copies them unchanged to
the ISO root as D6221.IMG, D6222.IMG, D6223.IMG and D622S.IMG.
The MS-DOS emulator reads those ISO files, not this source directory.

Keep this folder with the inteiliDOS source when building a downloaded copy.
To use a different set of supplied images, configure MSDOS_DISK_DIR explicitly.
