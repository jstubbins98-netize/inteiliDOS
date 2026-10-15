/* Shared AHCI command/DMA model; no real disk, port I/O or firmware calls. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_ENABLE_ATA 1
#define CONFIG_ENABLE_AHCI 1
#define ATA_TEST_IO 1
#define AHCI_TEST_IO 1
#include "../kernel/ata.c"
#include "../kernel/sata_optical.h"

static uint32_t regs[32],ticks;
static unsigned expected_bytes,issued;
static int fail_condition,short_transfer;
static uint8_t expected_opcode;
static uint8_t inb(uint16_t port) { (void)port; return 0xFF; }
static uint16_t inw(uint16_t port) { (void)port; return 0xFFFF; }
static void outb(uint16_t port,uint8_t val) { (void)port;(void)val; }
static void outw(uint16_t port,uint16_t val) { (void)port;(void)val; }
uint32_t timer_get_ticks(void) { return ticks++; }
void timer_sleep(uint32_t ms) { ticks+=ms; }
static uint32_t ahci_r(uint32_t base,uint32_t off) {
    assert(base==0x1100 && off<sizeof(regs));
    return regs[off/4];
}
static void ahci_w(uint32_t base,uint32_t off,uint32_t val) {
    assert(base==0x1100 && off<sizeof(regs));
    if (off==PxIS || off==PxSERR) { regs[off/4]&=~val; return; }
    regs[off/4]=val;
    if (off==PxCMD && !(val&PXCMD_ST)) {
        regs[PxCI/4]=0; /* stopped engine abandons the failed command */
        return;
    }
    if (off!=PxCI) return;
    issued++;
    uint32_t *cl=(uint32_t *)(void *)ahci_clb[0];
    uint8_t *ct=ahci_ct[0];
    assert(cl[2]==(uint32_t)(uintptr_t)ct && !cl[3]);
    assert((cl[0]&31)==5 && !(cl[0]&(1u<<6))); /* never device-write */
    assert((cl[0]>>16)==(expected_bytes ? 1u : 0u));
    if (expected_opcode) {
        assert(ct[2]==0xA0 && (cl[0]&(1u<<5)));
        assert(ct[0x40]==expected_opcode);
        assert(ct[3]==(expected_bytes ? 1 : 0));
        assert((ct[5]|((unsigned)ct[6]<<8))==expected_bytes);
    } else {
        assert(ct[2]==ATA_CMD_IDENTIFY && !(cl[0]&(1u<<5)));
    }
    if (expected_bytes) {
        unsigned dbc=ct[0x8c]|((unsigned)ct[0x8d]<<8);
        assert(dbc+1==expected_bytes);
        assert(*(uint32_t *)(void *)(ct+0x80)==(uint32_t)(uintptr_t)ahci_data[0]);
    }
    if (fail_condition) {
        regs[PxIS/4]=1u<<30; /* TFES with CI still set, ordinary empty tray */
        fail_condition=0;
        return;
    }
    memset(ahci_data[0],0x5a,sizeof(ahci_data[0]));
    cl[1]=expected_bytes-(short_transfer ? 1 : 0);
    regs[PxCI/4]=0;
}
int main(void) {
    ahci_scanned=1;
    ahci_optical[0]=1;
    g_drives[4].ahci_abar=0x1000;
    g_drives[4].ahci_port=0;
    regs[PxCMD/4]=PXCMD_ST|PXCMD_FRE;
    uint8_t buf[2048],cdb[12]={0x28,0,0,0,0,16,0,0,1};
    ata_drive_t disk;
    assert(sata_optical_present(4) && !sata_optical_present(3));
    assert(!sata_optical_present(8));
    assert(ata_get_drive(4,&disk)==-1 && ata_read_sector(4,0,buf)==-1);
    assert(ata_write_sector(4,0,buf)==-1 && !issued);
    /* A repeated HDD inventory must retain optical ownership and not rebase
     * either active SATA engine. PATA I/O above models a floating empty bus. */
    g_drives[5].present=1;g_drives[5].drive_type=ATA_TYPE_AHCI;
    g_drives[5].drive_index=5;g_drives[5].total_sectors=100;
    ata_drive_t inventory[ATA_MAX_DRIVES];
    assert(ata_detect(inventory)==1 && inventory[5].present && !inventory[4].present);
    assert(sata_optical_present(4) && !issued);
    expected_bytes=2048;expected_opcode=0x28;
    assert(!sata_optical_packet(4,cdb,buf,sizeof(buf)) && buf[2047]==0x5a);
    short_transfer=1;memset(buf,0xa7,sizeof(buf));
    assert(sata_optical_packet(4,cdb,buf,sizeof(buf))==-1 && buf[0]==0xa7);
    short_transfer=0;
    fail_condition=1;
    assert(sata_optical_packet(4,cdb,buf,sizeof(buf))==-1 && !ahci_failed[0]);
    assert(!regs[PxCI/4] && (regs[PxCMD/4]&PXCMD_ST));
    memset(cdb,0,sizeof(cdb));cdb[0]=3;cdb[4]=18;
    expected_opcode=3;expected_bytes=18;
    assert(!sata_optical_packet(4,cdb,buf,18)); /* sense after CHECK CONDITION */
    memset(cdb,0,sizeof(cdb));cdb[0]=0x1b;cdb[4]=2;
    expected_opcode=0x1b;expected_bytes=0;
    assert(!sata_optical_packet(4,cdb,NULL,0));
    unsigned before=issued;
    cdb[0]=0x2a; /* WRITE(10) forbidden */
    assert(sata_optical_packet(4,cdb,buf,2048)==-1 && issued==before);
    cdb[0]=0x28;cdb[8]=2;
    assert(sata_optical_packet(4,cdb,buf,2048)==-1 && issued==before);
    cdb[8]=1;
    assert(sata_optical_packet(4,cdb,buf,512)==-1 && issued==before);
    expected_opcode=0;expected_bytes=512;
    assert(!ahci_issue(0,0x1100,ATA_CMD_IDENTIFY,0,0,buf));
    puts("PASS: SATA ATAPI header/CDB/DMA sizes, no HDD alias, short-read rejection, CHECK CONDITION recovery, sense/eject, forbidden writes and unchanged ATA identify");
}
