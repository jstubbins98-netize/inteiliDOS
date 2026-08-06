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

/*
 * pci_find_class — like pci_find_device but matches only class+subclass.
 * Useful when prog_if varies across hardware revisions (e.g. AHCI 0x00/0x01).
 * On success fills *out and returns 0.  Returns -1 if not found.
 */
int pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t *out);

/*
 * pci_find_class_after — same as pci_find_class but starts scanning after
 * the device described by *prev.  Used to iterate multiple controllers of
 * the same class (e.g. two AHCI HBAs).
 * Returns 0 and fills *out on success; -1 when no further match exists.
 */
int pci_find_class_after(uint8_t class_code, uint8_t subclass,
                         const pci_device_t *prev, pci_device_t *out);

#endif /* PCI_H */
