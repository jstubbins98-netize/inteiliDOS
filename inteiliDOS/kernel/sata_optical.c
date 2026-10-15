/* SATA CD/DVD packet transport. AHCI engine/DMA ownership stays in ata.c
 * so a media rescan cannot invalidate a hard disk's command buffers. */
#include "sata_optical.h"
#include "ata.h"

int sata_optical_present(uint8_t drive) {
    return ata_ahci_optical_present(drive);
}

int sata_optical_packet(uint8_t drive,const uint8_t cdb[12],
                        uint8_t *buffer,unsigned bytes) {
    if (!cdb || !sata_optical_present(drive)) return -1;
    unsigned expected;
    switch (cdb[0]) {
    case 0x03: expected=18; break;   /* REQUEST SENSE */
    case 0x25: expected=8; break;    /* READ CAPACITY(10) */
    case 0x28:                     /* READ(10), exactly one logical block */
        if (cdb[7] || cdb[8]!=1) return -1;
        expected=2048; break;
    case 0x1B: expected=0; break;    /* START STOP UNIT / eject */
    default: return -1;             /* explicitly no write/burn support */
    }
    if (bytes!=expected || (bytes && !buffer) || (!bytes && buffer)) return -1;
    return ata_ahci_packet(drive,cdb,buffer,bytes);
}
