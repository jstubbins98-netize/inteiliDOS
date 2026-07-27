/*
 * inteilidOS -- kernel/pci.h
 * PCI bus scanner (config space via I/O ports 0xCF8/0xCFC)
 */

#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* Filled in by pci_find_device() */
typedef struct {
    uint8_t  bus, device, function;
    uint16_t vendor_id, device_id;
    uint8_t  class_code, subclass, prog_if, revision;
    uint8_t  header_type;
    uint32_t bar[6];          /* raw BAR values from config space       */
    uint8_t  interrupt_line;  /* INT A–D pin routed to IRQ (0xFF = none)*/
} pci_device_t;

/* Read/write PCI config space (byte-aligned register offset).
 * For 16-bit access, reg must be 2-byte aligned.
 * For 32-bit access, reg must be 4-byte aligned.                        */
uint32_t pci_read32 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val);
uint16_t pci_read16 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
void     pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint16_t val);

/* Scan PCI bus 0-7 for a device matching the given class/subclass/prog_if.
 * On success, fills *out and returns 0.  Returns -1 if not found.        */
int pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                    pci_device_t *out);

/* Set Bus Master Enable (Command register bit 2) so the device can DMA.  */
void pci_enable_busmaster(const pci_device_t *dev);

#endif /* PCI_H */
