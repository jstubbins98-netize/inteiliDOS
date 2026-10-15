#ifndef SATA_OPTICAL_H
#define SATA_OPTICAL_H
#include <stdint.h>

/* SATA optical slots 4..7 share the AHCI controller owner with HDDs.
 * No writing/burning commands are exposed. Legacy IDE remains in cdrom.c. */
int sata_optical_present(uint8_t drive);
int sata_optical_packet(uint8_t drive,const uint8_t cdb[12],
                        uint8_t *buffer,unsigned bytes);
#endif
