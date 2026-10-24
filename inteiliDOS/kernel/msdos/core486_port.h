#ifndef MSDOS_CORE486_PORT_H
#define MSDOS_CORE486_PORT_H
/* Freestanding tiny386 port: no libc, host OS, threads, or physical I/O. */
#include "../memory.h"
#define malloc kmalloc
#define free kfree
#define memset kmemset
#define memcpy kmemcpy
#define memcmp kmemcmp
#endif
