/* Real AHCI zeroing command/FIS/PRDT construction; no host hardware I/O. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_ENABLE_ATA 1
#define CONFIG_ENABLE_AHCI 1
#define ATA_TEST_IO 1
#define AHCI_TEST_IO 1
#include "../kernel/ata.c"

static uint32_t regs[32],ticks,next_lba;
static unsigned writes,flushes,sectors,commands,fail_data_at;
static int fail_flush,short_write;
static uint8_t inb(uint16_t p) {(void)p;return 0xff;}
static uint16_t inw(uint16_t p) {(void)p;return 0xffff;}
static void outb(uint16_t p,uint8_t v) {(void)p;(void)v;}
static void outw(uint16_t p,uint16_t v) {(void)p;(void)v;}
uint32_t timer_get_ticks(void) {return ticks++;}
static uint32_t ahci_r(uint32_t base,uint32_t off) {
    assert(base==0x1100 && off<sizeof(regs));return regs[off/4];
}
static void ahci_w(uint32_t base,uint32_t off,uint32_t value) {
    assert(base==0x1100 && off<sizeof(regs));
    if (off==PxIS || off==PxSERR) {regs[off/4]&=~value;return;}
    regs[off/4]=value;
    if (off!=PxCI) return;
    ++commands;
    uint32_t *cl=(uint32_t *)(void *)ahci_clb[0];
    const uint8_t *ct=ahci_ct[0];
    assert(!(cl[0]&(1u<<5)) && !cl[3]); /* ATA only, below-4GiB DMA */
    regs[PxTFD/4]=0;regs[PxCI/4]=0;
    if (ct[2]==ATA_CMD_FLUSH || ct[2]==ATA_CMD_FLUSH_EXT) {
        ++flushes;assert(!(cl[0]>>16) && !(cl[0]&(1u<<6)));
        assert(sectors==128 || sectors==130);
        if (fail_flush) regs[PxTFD/4]=1;
        return;
    }
    assert(ct[2]==(ahci_lba48[0] ? 0x35 : ATA_CMD_WRITE));
    assert((cl[0]&(1u<<6)) && (cl[0]>>16)==1);
    unsigned count=ct[12]|((unsigned)ct[13]<<8);
    assert(count>=1 && count<=4);
    uint32_t lba=ct[4]|((uint32_t)ct[5]<<8)|((uint32_t)ct[6]<<16)|
                 ((uint32_t)(ahci_lba48[0] ? ct[8] : ct[7]&15)<<24);
    assert(lba==next_lba && count<=g_drives[4].total_sectors-lba);
    assert((ct[0x8c]|((unsigned)ct[0x8d]<<8))+1==count*512);
    assert(*(const uint32_t *)(const void *)(ct+0x80)==(uint32_t)(uintptr_t)ahci_data[0]);
    for (unsigned i=0;i<count*512;i++) assert(!ahci_data[0][i]);
    ++writes;sectors+=count;next_lba+=count;
    cl[1]=count*512-(short_write ? 1 : 0);
    if (fail_data_at==writes) regs[PxTFD/4]=1;
}
static void fresh(void) {
    memset(regs,0,sizeof(regs));memset(g_drives,0,sizeof(g_drives));
    memset(ahci_failed,0,sizeof(ahci_failed));
    g_drives[4]=(ata_drive_t){.present=1,.drive_type=ATA_TYPE_AHCI,
                            .total_sectors=4096,.ahci_abar=0x1000,.ahci_port=0};
    ahci_lba48[0]=1;ahci_flush[0]=ATA_CMD_FLUSH_EXT;ahci_readonly[0]=0;
    ticks=writes=flushes=sectors=commands=fail_data_at=0;
    fail_flush=short_write=0;next_lba=100;
}
int main(void) {
    fresh();assert(!ata_zero_sectors(4,100,130));
    assert(writes==33 && flushes==2 && sectors==130 && commands==35);
    fresh();ahci_lba48[0]=0;ahci_flush[0]=ATA_CMD_FLUSH;
    assert(!ata_zero_sectors(4,100,130) && writes==33 && flushes==2);
    fresh();ahci_flush[0]=0; /* known uncached, no advertised FLUSH required */
    assert(!ata_zero_sectors(4,100,3) && writes==1 && !flushes && sectors==3);
    fresh();ahci_readonly[0]=1;
    assert(ata_zero_sectors(4,100,128)==-1 && !commands);
    fresh();fail_flush=1;
    assert(ata_zero_sectors(4,100,128)==-1 && writes==32 && flushes==1);
    assert(strstr(ata_last_write_failure(),"flush failed"));
    fresh();fail_data_at=2;
    assert(ata_zero_sectors(4,100,128)==-1 && writes==2 && !flushes);
    fresh();short_write=1;
    assert(ata_zero_sectors(4,100,128)==-1 && writes==1 && !flushes);
    fresh();assert(ata_zero_sectors(4,4095,2)==-1 && !commands);
    assert(ata_zero_sectors(4,100,0)==-1 && !commands);
    assert(ata_zero_sectors(8,100,128)==-1 && !commands);
    fresh();ahci_lba48[0]=0;g_drives[4].total_sectors=0x20000000;
    assert(ata_zero_sectors(4,0x0fffffff,2)==-1 && !commands);
    fresh();g_drives[4].present=0;ahci_optical[0]=1;
    assert(ata_zero_sectors(4,100,128)==-1 && !commands);
    puts("PASS: AHCI four-sector zero commands, bounded batch flushes, LBA28/48, "
         "uncached/readonly drives, short/data/flush errors, bounds, optical isolation");
}
