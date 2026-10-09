/* Verify the actual ATA port resolver with a substituted PCI enumeration. */
#include <assert.h>
#include <stdio.h>
#include "../kernel/ata.h"
#include "../kernel/pci.h"

static pci_device_t controller;
static int available;

int pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t *out) {
    assert(class_code == 1 && subclass == 1);
    if (!available) return -1;
    *out = controller;
    return 0;
}

static void check(uint8_t slot, uint16_t base_expected, uint16_t ctrl_expected) {
    uint16_t base, ctrl;
    assert(ata_get_pata_ports(slot, &base, &ctrl) == 0);
    assert(base == base_expected && ctrl == ctrl_expected);
}

int main(void) {
    uint16_t base, ctrl;
    assert(ata_get_pata_ports(4, &base, &ctrl) == -1);
    assert(ata_get_pata_ports(0, NULL, &ctrl) == -1);
    check(0, 0x1F0, 0x3F6);
    check(3, 0x170, 0x376);
    available = 1; controller.prog_if = 0; /* PCI compatibility mode */
    check(1, 0x1F0, 0x3F6);
    controller.prog_if = 4; /* native secondary, legacy primary */
    controller.bar[2] = 0x161; controller.bar[3] = 0x365;
    check(0, 0x1F0, 0x3F6);
    check(2, 0x160, 0x366);
    check(3, 0x160, 0x366);
    controller.prog_if = 5;
    controller.bar[0] = 0x1E1; controller.bar[1] = 0x3E5;
    check(0, 0x1E0, 0x3E6);
    check(1, 0x1E0, 0x3E6);
    check(2, 0x160, 0x366);
    puts("PASS: actual ATA port resolver, absent/compatibility/mixed/native PCI IDE");
    return 0;
}
