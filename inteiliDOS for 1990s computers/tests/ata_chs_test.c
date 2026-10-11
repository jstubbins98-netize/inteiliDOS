/* Real ATA implementation, with 86Box-style small-drive IDENTIFY/task file. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_ENABLE_ATA 1
#define ATA_TEST_IO 1
#include "../kernel/ata.c"

static uint8_t task[8], status=0x40, last_command;
static unsigned words, commands;
static uint32_t ticks;
uint32_t timer_get_ticks(void) { return ticks++; }
static uint8_t inb(uint16_t port) {
    return port==0x1f7 || port==0x3f6 ? status : 0;
}
static void outb(uint16_t port, uint8_t value) {
    assert(port>=0x1f0 && port<=0x1f7);
    task[port-0x1f0]=value;
    if (port!=0x1f7) return;
    assert(value==ATA_CMD_READ || value==ATA_CMD_WRITE);
    last_command=value;words=0;commands++;status=0x48;
}
static uint16_t inw(uint16_t port) {
    assert(port==0x1f0 && status==0x48 && last_command==ATA_CMD_READ);
    if (++words==256u*task[2]) status=0x40;
    return 0xa55a;
}
static void outw(uint16_t port,uint16_t value) {
    (void)value;
    assert(port==0x1f0 && status==0x48 && last_command==ATA_CMD_WRITE);
    if (++words==256u*task[2]) status=0x40;
}
static void identify(unsigned d,uint16_t *id) {
    memset(g_drives,0,sizeof(g_drives));memset(task,0,sizeof(task));
    g_drives[d].drive_index=d;g_drives[d].present=1;
    pata_parse_identify(id,&g_drives[d]);
    commands=ticks=0;status=0x40;
}
static void check_chs(unsigned d,unsigned sector,unsigned c,unsigned h,unsigned s) {
    uint8_t buf[512];
    assert(ata_read_sector(d,sector,buf)==0 && words==256);
    assert(buf[0]==0x5a && buf[1]==0xa5);
    assert(task[6]==(0xa0 | (d<<4) | h));
    assert(task[3]==s && task[4]==(c&255) && task[5]==(c>>8));
    assert(ata_write_sector(d,sector,buf)==0 && words==256);
    assert(task[6]==(0xa0 | (d<<4) | h));
    assert(task[3]==s && task[4]==(c&255) && task[5]==(c>>8));
}
int main(void) {
    uint16_t id[256]={0};
    /* 306 * 4 * 17 = 20,808 sectors, 10 MiB class. Small 86Box disks
     * advertise CHS, not LBA; command words explicitly advertise no cache. */
    id[1]=306;id[3]=4;id[6]=17;id[83]=id[87]=0x4000;
    for (unsigned d=0;d<2;d++) {
        identify(d,id);
        assert(g_drives[d].total_sectors==20808 && chs_only[d]);
        check_chs(d,0,0,0,1);
        check_chs(d,17,0,1,1);
        check_chs(d,68,1,0,1);
        check_chs(d,2048,30,0,9);
        check_chs(d,20807,305,3,17);
        unsigned before=commands;
        uint8_t buf[512]={0};
        assert(ata_read_sector(d,20808,buf)==-1);
        assert(ata_write_sector(d,20808,buf)==-1);
        assert(commands==before);
        assert(ata_zero_sectors(d,2048,128)==0 && words==256*128);
        assert(task[6]==(0xa0 | (d<<4)) && task[3]==9 && task[4]==30);
    }
    /* BIOS-programmed current translation takes precedence over defaults. */
    id[53]=1;id[54]=200;id[55]=8;id[56]=13;
    id[57]=20800;
    identify(0,id);
    assert(g_drives[0].total_sectors==20800);
    check_chs(0,104,1,0,1);
    /* Invalid current translation must not erase usable default geometry. */
    id[55]=17;identify(0,id);
    assert(g_drives[0].total_sectors==20808);
    memset(id,0,sizeof(id));id[3]=17;id[1]=306;id[6]=17;
    identify(0,id);assert(g_drives[0].total_sectors==0);
    /* Unadvertised/reserved LBA48 words must not override LBA28. */
    memset(id,0,sizeof(id));id[49]=512;id[60]=20808;
    id[100]=0xffff;id[101]=0xffff;
    id[83]=id[87]=0x4000;
    identify(0,id);assert(g_drives[0].total_sectors==20808 && !chs_only[0]);
    uint8_t buf[512]={0};
    assert(ata_write_sector(0,2048,buf)==0);
    assert(task[6]==0xe0 && task[3]==0 && task[4]==8 && task[5]==0);
    /* LBA48 capacity is bounded to the actual PATA LBA28 command range. */
    id[83]=0x4400;id[100]=0;id[101]=0;id[102]=1;
    identify(0,id);assert(g_drives[0].total_sectors==0x10000000);
    assert(ata_write_sector(0,0x0fffffff,buf)==0);
    assert(task[6]==0xef && task[3]==255 && task[4]==255 && task[5]==255);
    ata_drive_t ahci={0};
    ahci.drive_index=4;
    pata_parse_identify(id,&ahci);
    assert(ahci.total_sectors==0x10000000); /* same command width on SATA */
    puts("PASS: 86Box-style 10MiB CHS capacity, master/slave reads/writes/zeroing, translation, bounds and LBA compatibility");
}
