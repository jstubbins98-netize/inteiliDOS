/* Deterministic task-file model. In particular, successful IDENTIFY PACKET
 * leaves stale cylinder values, as 86Box's IDE controller does. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/cdrom.h"
#include "../kernel/ata.h"

typedef struct {
    int type, mode, position, size, chunk, phase_left, busy, attention, empty;
    int extra, short_read, reject_eject, words, senses;
    uint8_t status, reason, cdb[12], data[4096];
    uint16_t cylinder, identity;
} device_t;
static device_t dev[4];
static uint8_t selected[2];
static uint16_t bases[2], controls[2];

static device_t *port_device(uint16_t port, int *reg) {
    for (int ch = 0; ch < 2; ch++) {
        if (port >= bases[ch] && port <= bases[ch] + 7) {
            *reg = port - bases[ch];
            return &dev[ch * 2 + selected[ch]];
        }
        if (port == controls[ch]) {
            *reg = 7;
            return &dev[ch * 2 + selected[ch]];
        }
    }
    assert(!"unknown I/O port");
    return NULL;
}

int ata_get_pata_ports(uint8_t d, uint16_t *base, uint16_t *ctrl) {
    if (d >= 4 || !base || !ctrl) return -1;
    *base = bases[d / 2]; *ctrl = controls[d / 2];
    return 0;
}

static void phase(device_t *d) {
    d->phase_left = d->size - d->position;
    if (d->chunk && d->phase_left > d->chunk) d->phase_left = d->chunk;
    d->cylinder = (uint16_t)d->phase_left;
    d->reason = 2; d->status = 0x48; d->mode = 3;
}

void outb(uint16_t port, uint8_t value) {
    int reg;
    device_t *d = port_device(port, &reg);
    if (reg == 6) {
        for (int ch = 0; ch < 2; ch++)
            if (port == bases[ch] + 6) selected[ch] = (value >> 4) & 1;
        return;
    }
    if (reg == 4) d->cylinder = (d->cylinder & 0xFF00) | value;
    if (reg == 5) d->cylinder = (d->cylinder & 0xFF) | ((uint16_t)value << 8);
    if (reg != 7) return;
    d->position = 0;
    if (!d->type) { d->status = 0; return; }
    if (value == 0xA1) {
        if (d->type == 1) { d->status = 0x41; return; } /* HDD abort */
        d->mode = 1; d->status = 0x48; d->busy = 8;
    } else if (value == 0xA0) {
        assert(d->mode != 1); /* identify must have been fully drained */
        d->mode = 2; d->status = 0x48; d->reason = 1;
    }
}

uint8_t inb(uint16_t port) {
    int reg;
    device_t *d = port_device(port, &reg);
    if (reg == 7) {
        if (d->busy > 0) { d->busy--; return 0x81; } /* ERR invalid in BSY */
        return d->status;
    }
    if (reg == 2) return d->reason;
    if (reg == 4) return (uint8_t)d->cylinder;
    if (reg == 5) return (uint8_t)(d->cylinder >> 8);
    return 0;
}

uint16_t inw(uint16_t port) {
    int reg;
    device_t *d = port_device(port, &reg);
    assert(reg == 0);
    d->words++;
    if (d->mode == 1) {
        uint16_t word = d->position == 0 ? d->identity : 0;
        if (++d->position == 256) { d->mode = 0; d->status = 0x40; }
        return word;
    }
    assert(d->mode == 3 && d->phase_left > 0);
    uint16_t word = d->data[d->position++];
    if (--d->phase_left > 0) {
        word |= (uint16_t)d->data[d->position++] << 8;
        d->phase_left--;
    }
    if (d->phase_left == 0) {
        if (d->position < d->size) phase(d);
        else { d->mode = 0; d->status = 0x40; d->reason = 3; }
    }
    return word;
}

void outw(uint16_t port, uint16_t value) {
    int reg;
    device_t *d = port_device(port, &reg);
    assert(reg == 0 && d->mode == 2);
    d->cdb[d->position++] = (uint8_t)value;
    d->cdb[d->position++] = (uint8_t)(value >> 8);
    if (d->position != 12) return;
    d->position = 0;
    memset(d->data, 0, sizeof(d->data));
    switch (d->cdb[0]) {
        case 0x25:
            if (d->attention || d->empty) {
                d->mode = 0; d->status = 0x41; d->reason = 3; return;
            }
            d->size = 8; d->data[3] = 127; d->data[6] = 8; break;
        case 0x03:
            d->senses++;
            d->size = 18; d->data[0] = 0x70;
            d->data[2] = d->attention ? 6 : 2;
            d->data[12] = d->attention ? 0x28 : 0x3A;
            d->attention = 0;
            break;
        case 0x28:
            assert(d->cdb[7] == 0 && d->cdb[8] == 1);
            d->size = 2048 + d->extra - d->short_read;
            for (int i = 0; i < d->size; i++) d->data[i] = (uint8_t)i;
            break;
        case 0x1B:
            d->mode = 0; d->reason = 3; d->busy = 8;
            d->status = d->reject_eject ? 0x41 : 0x40;
            return;
        default: assert(!"unexpected CDB");
    }
    phase(d);
}

static void setup(int slot, int native, int attention, int empty) {
    memset(dev, 0, sizeof(dev));
    memset(selected, 0, sizeof(selected));
    bases[0] = native ? 0x1E0 : 0x1F0;
    bases[1] = native ? 0x160 : 0x170;
    controls[0] = native ? 0x3E6 : 0x3F6;
    controls[1] = native ? 0x366 : 0x376;
    dev[slot].type = 2; dev[slot].identity = 0x8580;
    dev[slot].status = 0x40;
    dev[slot].cylinder = 2048; /* BIOS last read's byte-count, not signature */
    dev[slot].attention = attention; dev[slot].empty = empty;
    /* A real HDD must not be counted as a CD. */
    dev[slot ^ 1].type = 1; dev[slot ^ 1].status = 0x40;
}

int main(void) {
    uint8_t bytes[2050];
    for (int native = 0; native <= 1; native++) {
        for (int slot = 0; slot < 4; slot++) {
            setup(slot, native, 1, 0);
            cdrom_init();
            assert(cdrom_count() == 1);
            assert(cdrom_drives()[slot].present == CDROM_PRESENT);
            assert(cdrom_drives()[slot].last_lba == 127);
            assert(cdrom_drives()[slot].block_size == 2048);
            assert(dev[slot].senses == 1);
            assert(cdrom_read_sector(slot, 16, bytes) == 0);
            for (int i = 0; i < 2048; i++) assert(bytes[i] == (uint8_t)i);
            dev[slot].chunk = 3; /* many phases, including odd byte counts */
            assert(cdrom_read_sector(slot, 16, bytes) == 0);
            for (int i = 0; i < 2048; i++) assert(bytes[i] == (uint8_t)i);
            dev[slot].chunk = 0;
            bytes[2048] = 0xA5; bytes[2049] = 0x5A;
            dev[slot].extra = 2;
            assert(cdrom_read_sector(slot, 16, bytes) == -1);
            assert(bytes[2048] == 0xA5 && bytes[2049] == 0x5A);
            assert(dev[slot].mode == 0); /* overlong phase drained */
            dev[slot].extra = 0; dev[slot].short_read = 2;
            assert(cdrom_read_sector(slot, 16, bytes) == -1);
            dev[slot].short_read = 0;
            assert(cdrom_read_sector(slot, 16, bytes) == 0);
            assert(cdrom_eject(slot) == 0);
            dev[slot].reject_eject = 1;
            assert(cdrom_eject(slot) == -1);
            setup(slot, native, 0, 1);
            cdrom_init();
            assert(cdrom_count() == 1 && cdrom_drives()[slot].last_lba == 0);
            dev[slot].empty = 0; dev[slot].attention = 1;
            assert(cdrom_rescan_media(slot) == 127);
            setup(slot, native, 0, 0);
            dev[slot].identity = 0x8180; /* packet tape, not CD */
            cdrom_init();
            assert(cdrom_count() == 0);
        }
    }
    assert(cdrom_read_sector(4, 0, bytes) == -1);
    assert(cdrom_read_sector(0, 0, NULL) == -1);
    puts("PASS: 86Box-style stale signatures, all IDE slots, native ports, "
         "UNIT ATTENTION, empty trays, rescan, multi/odd/short/overlong data, eject errors");
    return 0;
}
