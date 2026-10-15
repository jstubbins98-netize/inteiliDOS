/* Logical C:/D: selection with real fs.c and disposable multi-disk fixtures. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/fs.c"

#define SECTORS 8192u
#define BYTES ((size_t)SECTORS * 512u)
static uint8_t *disks[ATA_MAX_DRIVES];
static unsigned writes[ATA_MAX_DRIVES];
static int read_fail = -1;
void *kmemcpy(void *d, const void *s, size_t n) { return memcpy(d,s,n); }
void *kmemset(void *d, int c, size_t n) { return memset(d,c,n); }
size_t kstrlen(const char *s) { return strlen(s); }
int ata_detect(ata_drive_t out[ATA_MAX_DRIVES]) {
    memset(out,0,sizeof(ata_drive_t)*ATA_MAX_DRIVES);
    int count=0;
    for (unsigned d=0;d<ATA_MAX_DRIVES;++d) if (disks[d]) {
        out[d].present=1;out[d].drive_index=d;out[d].total_sectors=SECTORS;
        ++count;
    }
    return count;
}
int ata_read_sector(uint8_t d, uint32_t lba, uint8_t *b) {
    if (d>=ATA_MAX_DRIVES || !disks[d] || lba>=SECTORS || d==read_fail) return -1;
    memcpy(b,disks[d]+(size_t)lba*512,512);return 0;
}
int ata_write_sector(uint8_t d, uint32_t lba, const uint8_t *b) {
    assert(d<ATA_MAX_DRIVES && disks[d] && lba<SECTORS);
    memcpy(disks[d]+(size_t)lba*512,b,512);++writes[d];return 0;
}
int ata_zero_sectors(uint8_t d, uint32_t lba, uint32_t count) {
    uint8_t zero[512]={0};
    while(count--) if(ata_write_sector(d,lba++,zero))return -1;
    return 0;
}
static void reboot(void) { fs_drives_ready=0;fs_volume_count=0; }
static void fresh(void) {
    for(unsigned d=0;d<ATA_MAX_DRIVES;++d){free(disks[d]);disks[d]=NULL;writes[d]=0;}
    read_fail=-1;reboot();
}
/* kind: 0 blank, 1 recognized flat, 2 installed, 3 active installed */
static void disk(unsigned d,unsigned kind) {
    disks[d]=calloc(1,BYTES);assert(disks[d]);
    if(!kind)return;
    uint32_t dir=2048;
    if(kind>=2) {
        uint8_t *mbr=disks[d];
        mbr[510]=0x55;mbr[511]=0xaa;
        native_part_t *p=(native_part_t *)(mbr+446);
        p->type=NATIVE_PART_TYPE;p->status=kind==3?0x80:0;
        p->start=2048;p->sectors=SECTORS-2048;
        native_layout_init((native_layout_t *)(mbr+(2048+NATIVE_CONFIG_OFFSET)*512u),
                           p->start,p->sectors);
        dir+=NATIVE_DIR_OFFSET;
    }
    uint32_t magic=NATIVE_FS_MAGIC;
    memcpy(disks[d]+dir*512u,&magic,4);
}
static void selected(unsigned volume,unsigned expected) {
    uint8_t drive=0xff;
    assert(fs_volume_drive(volume,&drive)==FS_OK && drive==expected);
}
static void roundtrip(unsigned expected) {
    uint8_t data[]="10 print 1234\n",buf[64];uint32_t len=0;
    unsigned before=writes[expected];
    assert(fs_volume_write(0,"SAVE.TXT",data,sizeof(data))==FS_OK);
    assert(writes[expected]>before);
    for(unsigned d=0;d<ATA_MAX_DRIVES;++d)if(d!=expected)assert(!writes[d]);
    reboot();selected(0,expected);
    assert(fs_volume_read(0,"SAVE.TXT",buf,sizeof(buf),&len)==FS_OK);
    assert(len==sizeof(data) && !memcmp(buf,data,len));
}
int main(void) {
    /* Every physical index, including sole slave and secondary connectors. */
    for(unsigned d=0;d<ATA_MAX_DRIVES;++d) {
        fresh();disk(d,3);selected(0,d);assert(!writes[d]);roundtrip(d);
        uint8_t out;
        assert(fs_volume_drive(1,&out)==FS_ERR_NODRV);
        if(d)assert(fs_read(0,"SAVE.TXT",&out,1,NULL)==FS_ERR_NODRV);
    }
    fresh();disk(0,1);disk(1,3);disk(2,2);disk(3,3);
    selected(0,1);selected(1,3);selected(2,2);selected(3,0);
    uint8_t *other=malloc(BYTES);assert(other);memcpy(other,disks[0],BYTES);
    roundtrip(1);assert(!memcmp(other,disks[0],BYTES));free(other);
    /* Physical index zero still means physical index zero, not logical C:. */
    assert(fs_write(0,"PHYSICAL.TXT",(const uint8_t *)"A",1)==FS_OK);
    uint8_t out;
    assert(fs_read(1,"PHYSICAL.TXT",&out,1,NULL)==FS_ERR_NOTFOUND);
    assert(fs_volume_write(1,"SECOND.TXT",(const uint8_t *)"B",1)==FS_OK);
    assert(writes[3] && !writes[2]);
    /* Full/corrupt/unreadable C: never silently switches to D:. */
    fs_dirent_t *e=(fs_dirent_t *)(disks[1]+(2048+NATIVE_DIR_OFFSET)*512u+4);
    strcpy(e->name,"SAVE.TXT");e->start_lba=2048+NATIVE_DATA_OFFSET;
    e->byte_count=(SECTORS-e->start_lba)*512u;
    assert(fs_volume_write(0,"FULL.TXT",&out,1)==FS_ERR_FULL);
    unsigned second=writes[3];
    disks[1][(2048+NATIVE_CONFIG_OFFSET)*512u]^=1;
    assert(fs_volume_write(0,"NO.TXT",&out,1)==FS_ERR_LAYOUT);
    assert(writes[3]==second);
    read_fail=1;
    assert(fs_volume_write(0,"NO.TXT",&out,1)==FS_ERR_IO);
    assert(writes[3]==second);
    /* Blank/foreign/old/corrupt drives are never initialized to obtain C:. */
    fresh();disk(0,0);disk(1,3);
    disks[1][(2048+NATIVE_CONFIG_OFFSET)*512u]^=1;
    assert(fs_volume_write(0,"NO.TXT",&out,1)==FS_ERR_LAYOUT);
    assert(!writes[0] && !writes[1]);
    /* Failed discovery is not cached: confirmed setup may establish storage. */
    disks[1][(2048+NATIVE_CONFIG_OFFSET)*512u]^=1;
    selected(0,1);roundtrip(1);
    fresh();disk(0,0);disk(1,1);
    disks[0][0]=0xeb;selected(0,1);roundtrip(1);
    fresh();disk(0,3);disk(1,3);
    disks[0][(2048+NATIVE_DIR_OFFSET)*512u]^=1;
    selected(0,1);roundtrip(1);
    fresh();disk(2,1);disk(3,1);selected(0,2);selected(1,3);
    fresh();assert(fs_volume_drive(0,&out)==FS_ERR_NODRV);
    puts("PASS: all ATA slots, native priority, logical D:, physical API compatibility, cold discovery, no redirect/format");
    return 0;
}
