#ifndef LAUNCHPAD_DOS_H
#define LAUNCHPAD_DOS_H
#include "../kernel/dos.h"
/* DOS root is the directory containing the launched program. */
const dos_filesystem_t *launchpad_dos_source(int source, const char *directory);
#endif
