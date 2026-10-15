/* Exercise the actual PATA write path with a deterministic IDE device. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ATA_TEST_IO 1
#ifndef CONFIG_ENABLE_ATA
#define CONFIG_ENABLE_ATA 1
#endif
#include "../kernel/ata.c"

static unsigned writes, flushes, disables, words, sectors;
static int reject_disable, reject_data, reject_flush;
static uint8_t status, feature, command;
static uint32_t ticks;
uint32_t timer_get_ticks(void) { return ticks++; }

static uint8_t inb(uint16_t port) {
    if (port==0x1f7 || port==0x3f6) return status;
    return 0;
}
static uint16_t inw(uint16_t port) { (void)port;return 0; }
static void outb(uint16_t port,uint8_t value) {
    if (port==0x1f1) feature=value;
    if (port==0x1f2) sectors=value;
    if (port!=0x1f7) return;
    command=value;
    if (value==ATA_CMD_SET_FEATURES) {
        assert(feature==0x82);++disables;
        status=reject_disable?0x41:0x40;
    } else if (value==ATA_CMD_WRITE) {
        ++writes;words=0;status=reject_data?0x41:0x48;
    } else if (value==ATA_CMD_FLUSH || value==ATA_CMD_FLUSH_EXT) {
        ++flushes;status=reject_flush?0x41:0x40;
    } else assert(!"Unexpected ATA command");
}
static void outw(uint16_t port,uint16_t value) {
    (void)value;assert(port==0x1f0);assert(status==0x48);
    if (++words==256*sectors) status=0x40;
}
static void identify(uint16_t capabilities,uint16_t enabled_valid,uint16_t enabled) {
    uint16_t id[256]={0};
    id[83]=capabilities;id[87]=enabled_valid;id[85]=enabled;
    id[60]=4096;
    id[49]=1u<<9;
    memset(g_drives,0,sizeof(g_drives));
    g_drives[0].drive_index=0;
    pata_parse_identify(id,&g_drives[0]);
    g_drives[0].present=ATA_PRESENT;
    status=0x40;words=writes=flushes=disables=ticks=0;
    reject_disable=reject_data=reject_flush=0;
}
int main(void) {
    uint8_t data[512]={0};
    identify(0x5000,0x4000,0x20); /* modern FLUSH CACHE */
    assert(ata_write_sector(0,0,data)==0);
    assert(writes==1 && flushes==1 && disables==0);
    identify(0,0,0); /* older IDENTIFY, no advertised FLUSH */
    assert(ata_write_sector(0,0,data)==0);
    assert(writes==1 && disables==1 && flushes==0);
    assert(ata_write_sector(0,1,data)==0 && disables==1);
    assert(pata_zero_batch(0,2,2)==0 && flushes==0 && words==512);
    identify(0x4000,0x4000,0); /* explicitly uncached drive */
    reject_disable=1;
    assert(ata_write_sector(0,0,data)==0 && disables==0 && flushes==0);
    identify(0x4000,0x4000,0x20); /* cache enabled, disable first */
    assert(ata_write_sector(0,0,data)==0 && disables==1 && flushes==0);
    identify(0,0,0);reject_disable=1;
    assert(ata_write_sector(0,0,data)==-1 && writes==0 && flushes==0);
    assert(strstr(ata_last_write_failure(),"no data written"));
    identify(0x5000,0x4000,0x20);reject_data=1;
    assert(ata_write_sector(0,0,data)==-1 && flushes==0);
    identify(0x5000,0x4000,0x20);reject_flush=1;
    assert(ata_write_sector(0,0,data)==-1 && flushes==1);
    assert(strstr(ata_last_write_failure(),"flush failed"));
    identify(0x5000,0x4000,0x20);reject_data=1;
    assert(pata_zero_batch(0,0,2)==-1 && flushes==0);
    identify(0x6000,0x4000,0x20); /* FLUSH CACHE EXT only */
    assert(ata_write_sector(0,0,data)==0 && command==ATA_CMD_FLUSH_EXT);
    puts("PASS: legacy/modern/uncached PATA writes, bulk zero, and genuine failure handling");
    return 0;
}
