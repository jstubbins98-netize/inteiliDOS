# Nuked OPL3 1.8

Copyright (C) 2013–2020 Nuke.YKT. LGPL-2.1-or-later; see LICENSE.
Source: https://github.com/nukeykt/Nuked-OPL3
Pinned upstream revision: 765ec962e473aeb767e4cba74ffdc8f588ffbfe8.

Local changes: use stdint.h rather than inttypes.h, remove unused hosted
headers, and map memset to the kernel's kmemset for freestanding builds.
No floating-point stereo extension is enabled. fm.c supplies the isolated
ISA address decoding, OPL2 waveform gate and timer/status model.

This library retains its own license, separate from the project's MPL-2.0.
Distributions containing it must retain its notices and LGPL text and make
the corresponding library source and relinking materials available under
the LGPL. The repository includes the complete source, CMake kernel link
recipe and linker script. Keep the compiled objects and kernel link command
from a release build available so recipients can relink a modified library.
Do not describe the combined kernel as MPL-only.
