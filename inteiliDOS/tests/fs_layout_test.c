/* Real filesystem APIs against disposable, bounds-checked ATA fixtures.
 * Optional disk path exercises the same save/image APIs on a QEMU-installed
 * HDD and writes it back for the Python installer regression to inspect. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/fs.c"

#define TEST_TOTAL 100000u
static uint8_t *disk;
static uint32_t total = TEST_TOTAL, writes, allowed_dir, allowed_start, allowed_end;
static int64_t fail_read_lba = -1, fail_write_lba = -1;
static uint32_t write_attempts;
void *kmemcpy(void *d, const void *s, size_t n) { return memcpy(d,s,n); }
void *kmemset(void *d, int c, size_t n) { return memset(d,c,n); }
size_t kstrlen(const char *s) { return strlen(s); }
int ata_detect(ata_drive_t out[ATA_MAX_DRIVES]) {
    memset(out,0,sizeof(ata_drive_t)*ATA_MAX_DRIVES);
    out[0].present=1; out[0].total_sectors=total; return 1;
}
int ata_read_sector(uint8_t d, uint32_t lba, uint8_t *b) {
    if (d || lba>=total || lba==fail_read_lba) return -1;
    memcpy(b,disk+(size_t)lba*512,512); return 0;
}
int ata_write_sector(uint8_t d, uint32_t lba, const uint8_t *b) {
    assert(!d && lba<total);
    assert(lba==allowed_dir || (lba>=allowed_start && lba<allowed_end));
    ++write_attempts;
    if (lba==fail_write_lba) return -1;
    ++writes; memcpy(disk+(size_t)lba*512,b,512); return 0;
}
int ata_zero_sectors(uint8_t d, uint32_t lba, uint32_t n) {
    uint8_t zero[512]={0};
    for (uint32_t i=0;i<n;++i)
        if (ata_write_sector(d,lba+i,zero)) return -1;
    return 0;
}
static uint8_t *sector(uint32_t n) { return disk+(size_t)n*512; }
static void fresh(void) {
    memset(disk,0,(size_t)total*512); writes=0; fs_drives_ready=0;
    fail_read_lba=fail_write_lba=-1;write_attempts=0;
    allowed_dir=allowed_start=allowed_end=0;
}
static void directory(uint32_t n) {
    uint32_t m=NATIVE_FS_MAGIC; memset(sector(n),0,512); memcpy(sector(n),&m,4);
}
static void installed(uint32_t start,uint32_t count) {
    fresh();
    disk[510]=0x55;disk[511]=0xaa;
    native_part_t *p=(native_part_t *)(disk+446);
    p[0].type=0x0b;p[0].start=64;p[0].sectors=2000;
    p[1].type=NATIVE_PART_TYPE;p[1].start=start;p[1].sectors=count;
    native_layout_init((native_layout_t *)sector(start+NATIVE_CONFIG_OFFSET),start,count);
    allowed_dir=start+NATIVE_DIR_OFFSET;
    allowed_start=start+NATIVE_DATA_OFFSET;allowed_end=start+count;
    directory(allowed_dir);
    memset(sector(start),0xa5,NATIVE_CONFIG_OFFSET*512u);
    uint32_t marker=NATIVE_VBR_V2_MAGIC;
    memcpy(sector(start)+0x25,&marker,4);
}
static void saves_and_image(void) {
    uint8_t data[1024], readback[1024];
    memset(data,0x79,sizeof(data));
    assert(fs_write(0,"NOTE.TXT",data,10)==FS_OK);
    assert(fs_write(0,"NOTE.TXT",data,sizeof(data))==FS_OK);
    assert(fs_write(0,"EMPTY.TXT",NULL,0)==FS_OK);
    assert(fs_write(0,"NEXT.TXT",data,10)==FS_OK);
    uint32_t len;
    assert(fs_read(0,"NOTE.TXT",readback,sizeof(readback),&len)==FS_OK);
    assert(len==sizeof(data) && !memcmp(readback,data,len));
    fs_image_t image;
    assert(fs_image_reserve(0,65536,&image)==FS_OK);
    assert(image.start>=allowed_start && image.start+image.sectors<=allowed_end);
    assert(fs_image_zero(&image,0,image.sectors)==FS_OK);
    assert(fs_image_sector(&image,0,data,1)==FS_OK);
    assert(fs_image_sector(&image,image.sectors-1,data,1)==FS_OK);
    assert(fs_image_publish("MSDOS.HDD",&image)==FS_OK);
    fs_image_t reopened;
    assert(fs_image_open(0,"MSDOS.HDD",&reopened)==FS_OK);
    assert(reopened.start==image.start && reopened.sectors==65536);
    assert(fs_image_sector(&reopened,65535,readback,0)==FS_OK);
    assert(!memcmp(data,readback,512));
    writes=0;
    assert(fs_image_sector(&image,65536,data,1)!=FS_OK);
    assert(fs_image_zero(&image,65535,2)!=FS_OK);
    image.start=allowed_start-1;
    assert(fs_image_sector(&image,0,data,1)!=FS_OK);
    assert(fs_image_zero(&image,0,1)!=FS_OK);
    image.start=allowed_end-1;image.sectors=2;
    assert(fs_image_sector(&image,0,data,1)!=FS_OK);
    image.start=0xffffffffu;image.sectors=2;
    assert(fs_image_sector(&image,1,data,1)!=FS_OK);
    assert(!writes);
}
static void blocked(void) {
    uint8_t b[512]={0}; fs_image_t image; uint32_t len;
    fs_image_space_t space;
    writes=0;
    assert(fs_write(0,"NO.TXT",b,1)!=FS_OK);
    assert(fs_read(0,"NO.TXT",b,sizeof(b),&len)!=FS_OK);
    assert(fs_image_open(0,"NO.HDD",&image)!=FS_OK);
    assert(fs_image_reserve(0,10,&image)!=FS_OK);
    assert(fs_image_space(0,&space)!=FS_OK);
    image.drive=0;image.start=allowed_start;image.sectors=1;
    assert(fs_image_publish("NO.HDD",&image)!=FS_OK);
    assert(!writes);
}
static void tests(void) {
    fresh(); blocked(); /* no first-write formatting */
    fresh(); disk[0]=0xeb; directory(2048); blocked(); /* unknown VBR */
    installed(4096,80000);
    uint8_t *prefix=malloc((size_t)allowed_dir*512);
    memcpy(prefix,disk,(size_t)allowed_dir*512);
    saves_and_image();
    assert(!memcmp(prefix,disk,(size_t)allowed_dir*512)); free(prefix);
    /* Version 1 partitions keep their original descriptor/directory/data.
     * No in-place migration and no writes into their 512 KiB boot reserve. */
    installed(4096,80000);
    memset(sector(4096+NATIVE_CONFIG_OFFSET),0,512);
    memset(sector(4096)+0x25,0,4);
    native_layout_t *old=(native_layout_t *)sector(4096+1025);
    native_layout_init(old,4096,80000);
    old->version=1;old->kernel_sectors=1024;old->dir_offset=1033;
    old->checksum=native_layout_checksum(old);
    allowed_dir=4096+1033;allowed_start=allowed_dir+1;
    directory(allowed_dir);
    prefix=malloc((size_t)allowed_dir*512);
    memcpy(prefix,disk,(size_t)allowed_dir*512);
    saves_and_image();
    assert(!memcmp(prefix,disk,(size_t)allowed_dir*512));free(prefix);
    /* A v2 marker must never authorize using a v1 descriptor. */
    uint32_t marker=NATIVE_VBR_V2_MAGIC;
    memcpy(sector(4096)+0x25,&marker,4);
    blocked();
    /* Legacy files retain their physical addresses and contents. */
    fresh();directory(2048);allowed_dir=2048;allowed_start=2049;allowed_end=total;
    fs_dirent_t *e=(fs_dirent_t *)(sector(2048)+4);
    strcpy(e->name,"KEEP.TXT");e->start_lba=2049;e->byte_count=4;
    memcpy(sector(2049),"KEEP",4);
    saves_and_image(); assert(!memcmp(sector(2049),"KEEP",4));
    /* No layout descriptor on an old native install: fail without writes. */
    installed(4096,70000);
    memset(sector(4096+NATIVE_CONFIG_OFFSET),0,512);
    blocked();
    fs_image_t forged={0,allowed_start,1};uint8_t b[512]={0};
    assert(fs_image_sector(&forged,0,b,1)==FS_ERR_LAYOUT);
    assert(fs_image_zero(&forged,0,1)==FS_ERR_LAYOUT);
    assert(!writes);
    installed(4096,70000);
    sector(4096+NATIVE_CONFIG_OFFSET)[28]^=1;blocked();
    installed(4096,70000);
    ((native_part_t *)(disk+446))[2]=((native_part_t *)(disk+446))[1];blocked();
    installed(4096,70000);
    ((native_part_t *)(disk+446))[1].sectors=0xffffffffu;blocked();
    installed(4096,70000);
    ((native_part_t *)(disk+446))[0].sectors=6000;blocked();
    installed(4096,70000);
    disk[510]=0;blocked();
    installed(4096,70000);
    ((native_part_t *)(disk+446))[1].type=0x0b;directory(2048);blocked();
    /* Directory magic does not authorize boot/kernel or foreign extents. */
    for (int kind=0;kind<6;++kind) {
        installed(4096,70000);
        e=(fs_dirent_t *)(sector(allowed_dir)+4);
        strcpy(e->name,"NO.TXT");e->byte_count=1;e->start_lba=allowed_start;
        if(kind==0)e->start_lba=4097;
        if(kind==1)e->start_lba=allowed_end;
        if(kind==2)e->byte_count=0xffffffffu;
        if(kind==3)e->name[23]='x';
        if(kind==4){e[1]=e[0];strcpy(e[1].name,"OTHER.TXT");}
        if(kind==5){e->start_lba=0;e->byte_count=1;}
        blocked();
    }
    installed(4096,NATIVE_DATA_OFFSET+1);
    assert(fs_write(0,"ONE.TXT",b,1)==FS_OK);writes=0;
    assert(fs_write(0,"TWO.TXT",b,1)==FS_ERR_FULL);
    assert(fs_image_reserve(0,1,&forged)==FS_ERR_NOSPC);
    assert(!writes);
    /* Re-resolve after layout changes within the same boot. */
    installed(4096,70000);assert(fs_write(0,"ONE.TXT",b,1)==FS_OK);
    memset(sector(4096+NATIVE_CONFIG_OFFSET),0,512);blocked();
    puts("PASS: installed/legacy saves, 32MB DOS image, reserved regions, bounds, and fail-closed layouts");
}
int main(int argc,char **argv) {
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");assert(f);
        assert(!fseek(f,0,SEEK_END));long bytes=ftell(f);assert(bytes>0 && bytes%512==0);
        total=(uint32_t)(bytes/512);rewind(f);
        disk=malloc((size_t)bytes);assert(disk);
        assert(fread(disk,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);
        fs_ensure_drives();assert(fs_resolve_layout(0)==FS_OK);
        allowed_dir=fs_dir_lba;allowed_start=fs_data_start;allowed_end=fs_limit;
        saves_and_image();
        f=fopen(argv[1],"wb");assert(f);
        assert(fwrite(disk,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);
        puts("PASS: native saves and 32MB DOS image on actual installer output");
    } else {
        disk=malloc((size_t)total*512);assert(disk);tests();
    }
    free(disk);return 0;
}
