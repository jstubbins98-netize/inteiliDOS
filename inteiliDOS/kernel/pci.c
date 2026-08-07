/*
 * inteilidOS -- kernel/pci.c
 * PCI configuration space access via I/O ports 0xCF8 (address) / 0xCFC (data).
 * Scans buses 0-7, devices 0-31, functions 0-7.
 */

#include "pci.h"
#include <stdint.h>

#define PCI_ADDR_PORT 0xCF8u
#define PCI_DATA_PORT 0xCFCu

/* ---- Port I/O ---- */
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Build a 32-bit PCI config-space address. reg must be 4-byte aligned
 * (hardware ignores bits 0-1, so we just mask them).                    */
static inline uint32_t pci_addr(uint8_t bus, uint8_t dev,
                                 uint8_t fn, uint8_t reg) {
    return 0x80000000u
         | ((uint32_t)bus  << 16)
         | ((uint32_t)dev  << 11)
         | ((uint32_t)fn   <<  8)
         | (uint32_t)(reg & 0xFC);
}

/* ---- Public read/write ---- */

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    outl((uint16_t)PCI_ADDR_PORT, pci_addr(bus, dev, fn, reg));
    return inl((uint16_t)PCI_DATA_PORT);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg,
                  uint32_t val) {
    outl((uint16_t)PCI_ADDR_PORT, pci_addr(bus, dev, fn, reg));
    outl((uint16_t)PCI_DATA_PORT, val);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    /* byte offset within the 32-bit word */
    uint8_t shift = (reg & 2u) << 3;
    return (uint16_t)(pci_read32(bus, dev, fn, reg) >> shift);
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg,
                  uint16_t val) {
    uint8_t  shift = (reg & 2u) << 3;
    uint32_t mask  = ~((uint32_t)0xFFFFu << shift);
    uint32_t old   = pci_read32(bus, dev, fn, reg);
    pci_write32(bus, dev, fn, reg, (old & mask) | ((uint32_t)val << shift));
}

/* ---- Scan ---- */

int pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                    pci_device_t *out) {
    uint8_t bus, dev, fn;

    for (bus = 0; bus < 8; bus++) {
        for (dev = 0; dev < 32; dev++) {
            /* Check function 0 first; skip slot if no device */
            uint32_t id0 = pci_read32(bus, dev, 0, 0x00);
            if ((id0 & 0xFFFF) == 0xFFFF) continue;   /* slot empty */

            uint8_t hdr = (uint8_t)(pci_read32(bus, dev, 0, 0x0C) >> 16);
            uint8_t max_fn = (hdr & 0x80) ? 8 : 1;   /* multi-function? */

            for (fn = 0; fn < max_fn; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0x00);
                if ((id & 0xFFFF) == 0xFFFF) continue;

                uint32_t cls = pci_read32(bus, dev, fn, 0x08);
                uint8_t cc  = (uint8_t)(cls >> 24);
                uint8_t sc  = (uint8_t)(cls >> 16);
                uint8_t pi  = (uint8_t)(cls >>  8);

                if (cc != class_code || sc != subclass || pi != prog_if)
                    continue;

                /* Found — fill the output struct */
                out->bus       = bus;
                out->device    = dev;
                out->function  = fn;
                out->vendor_id = (uint16_t)(id & 0xFFFF);
                out->device_id = (uint16_t)(id >> 16);
                out->class_code = cc;
                out->subclass   = sc;
                out->prog_if    = pi;
                out->revision   = (uint8_t)(cls);
                out->header_type = (uint8_t)(pci_read32(bus, dev, fn, 0x0C) >> 16);

                /* Read BARs 0-5 (offsets 0x10-0x24) */
                uint8_t i;
                for (i = 0; i < 6; i++)
                    out->bar[i] = pci_read32(bus, dev, fn,
                                             (uint8_t)(0x10 + i * 4));

                out->interrupt_line = (uint8_t)pci_read32(bus, dev, fn, 0x3C);
                return 0;
            }
        }
    }
    return -1;   /* not found */
}

/* ---- Class-only scan helpers ---- */

/* Internal: fill *out from (bus,dev,fn) and return 0. */
static void pci_fill_device(uint8_t bus, uint8_t dev, uint8_t fn,
                             pci_device_t *out) {
    uint32_t id  = pci_read32(bus, dev, fn, 0x00);
    uint32_t cls = pci_read32(bus, dev, fn, 0x08);
    out->bus        = bus;  out->device    = dev;  out->function = fn;
    out->vendor_id  = (uint16_t)(id & 0xFFFF);
    out->device_id  = (uint16_t)(id >> 16);
    out->class_code = (uint8_t)(cls >> 24);
    out->subclass   = (uint8_t)(cls >> 16);
    out->prog_if    = (uint8_t)(cls >>  8);
    out->revision   = (uint8_t)(cls);
    out->header_type = (uint8_t)(pci_read32(bus, dev, fn, 0x0C) >> 16);
    uint8_t i;
    for (i = 0; i < 6; i++)
        out->bar[i] = pci_read32(bus, dev, fn, (uint8_t)(0x10 + i * 4));
    out->interrupt_line = (uint8_t)pci_read32(bus, dev, fn, 0x3C);
}

int pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t *out) {
    uint8_t bus, dev, fn;
    for (bus = 0; bus < 8; bus++) {
        for (dev = 0; dev < 32; dev++) {
            if ((pci_read32(bus, dev, 0, 0x00) & 0xFFFF) == 0xFFFF) continue;
            uint8_t hdr  = (uint8_t)(pci_read32(bus, dev, 0, 0x0C) >> 16);
            uint8_t maxfn = (hdr & 0x80) ? 8 : 1;
            for (fn = 0; fn < maxfn; fn++) {
                if ((pci_read32(bus, dev, fn, 0x00) & 0xFFFF) == 0xFFFF) continue;
                uint32_t cls = pci_read32(bus, dev, fn, 0x08);
                if ((uint8_t)(cls >> 24) != class_code) continue;
                if ((uint8_t)(cls >> 16) != subclass)   continue;
                pci_fill_device(bus, dev, fn, out);
                return 0;
            }
        }
    }
    return -1;
}

int pci_find_class_after(uint8_t class_code, uint8_t subclass,
                          const pci_device_t *prev, pci_device_t *out) {
    uint8_t bus, dev, fn;
    int past = 0;
    for (bus = 0; bus < 8; bus++) {
        for (dev = 0; dev < 32; dev++) {
            if ((pci_read32(bus, dev, 0, 0x00) & 0xFFFF) == 0xFFFF) continue;
            uint8_t hdr   = (uint8_t)(pci_read32(bus, dev, 0, 0x0C) >> 16);
            uint8_t maxfn = (hdr & 0x80) ? 8 : 1;
            for (fn = 0; fn < maxfn; fn++) {
                if ((pci_read32(bus, dev, fn, 0x00) & 0xFFFF) == 0xFFFF) continue;
                if (!past) {
                    if (bus == prev->bus && dev == prev->device && fn == prev->function)
                        past = 1;
                    continue;
                }
                uint32_t cls = pci_read32(bus, dev, fn, 0x08);
                if ((uint8_t)(cls >> 24) != class_code) continue;
                if ((uint8_t)(cls >> 16) != subclass)   continue;
                pci_fill_device(bus, dev, fn, out);
                return 0;
            }
        }
    }
    return -1;
}

/* ---- Bus Master Enable ---- */

void pci_enable_busmaster(const pci_device_t *dev) {
    /* PCI Command register is at config offset 0x04.
     * Bit 0 = I/O space enable, bit 1 = memory space enable,
     * bit 2 = bus master enable.  Set bits 0-2.                         */
    uint16_t cmd = pci_read16(dev->bus, dev->device, dev->function, 0x04);
    cmd |= 0x0007u;
    pci_write16(dev->bus, dev->device, dev->function, 0x04, cmd);
}
