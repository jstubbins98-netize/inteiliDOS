#include "../kernel/ata.h"
#include "../kernel/cdrom.h"
#include <stdint.h>

static void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0,%1" :: "a"(value), "Nd"(port));
}
static void log_text(const char *s) {
    while (*s) outb(0xE9, (uint8_t)*s++);
}
static uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static void log_hex(uint16_t value) {
    const char *digits = "0123456789abcdef";
    for (int shift = 12; shift >= 0; shift -= 4)
        outb(0xE9, (uint8_t)digits[(value >> shift) & 15]);
    log_text(" ");
}
static void finish(int success) {
    log_text(success ? "PASS\n" : "FAIL\n");
    /* isa-debug-exit reports (value << 1) | 1 to the host. */
    outb(0xF4, success ? 0x10 : 0x11);
    for (;;) __asm__ volatile ("hlt");
}
void test_main(void) {
    static uint8_t sector[CDROM_SECTOR_SIZE];
#if ATA_FIRST
    static ata_drive_t hdds[ATA_MAX_DRIVES];
    if (ata_detect(hdds) != 1) { log_text("HDD detection: "); finish(0); }
#endif
    /* Reproduce a bootloader's leftover sector byte-count, not reset magic. */
    uint16_t base, ctrl;
    if (ata_get_pata_ports(CD_SLOT, &base, &ctrl) < 0) finish(0);
    outb(base + 6, (uint8_t)(0xA0 | ((CD_SLOT & 1) << 4)));
    outb(base + 4, 0);
    outb(base + 5, 8);
    cdrom_init();
    if (cdrom_count() != 1 ||
        cdrom_drives()[CD_SLOT].present != CDROM_PRESENT) {
        log_text("CD detection: count="); log_hex((uint16_t)cdrom_count());
        log_text("ports="); log_hex(base); log_hex(ctrl);
        outb(base + 6, (uint8_t)(0xA0 | ((CD_SLOT & 1) << 4)));
        for (int i = 0; i < 4; i++) (void)inb(ctrl);
        log_text("status/cyl="); log_hex(inb(base + 7));
        log_hex(inb(base + 4)); log_hex(inb(base + 5));
        finish(0);
    }
#if EMPTY_TRAY
    if (cdrom_drives()[CD_SLOT].last_lba != 0) finish(0);
    finish(1);
#else
    if (cdrom_drives()[CD_SLOT].last_lba == 0) {
        log_text("CD capacity: "); finish(0);
    }
    /* Read an actual ISO9660 primary volume descriptor. */
    for (int repeat = 0; repeat < 3; repeat++) {
        if (cdrom_read_sector(CD_SLOT, 16, sector) < 0 ||
            sector[0] != 1 || sector[1] != 'C' || sector[2] != 'D' ||
            sector[3] != '0' || sector[4] != '0' || sector[5] != '1') {
            log_text("ISO sector read: "); finish(0);
        }
    }
    if (cdrom_rescan_media(CD_SLOT) == 0) finish(0);
    finish(1);
#endif
}
