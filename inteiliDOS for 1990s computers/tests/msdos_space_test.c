/* Real fs.c + platform.c on bounds-checked native disks. Only hardware,
 * display, and read-only CD media are substituted; storage errors originate
 * in the ATA backend, not a mocked reserve/seed return value. */
#define main layout_fixture_main
#include "fs_layout_test.c"
#undef main
#include <stdarg.h>
static uint8_t forbidden_cmos(uint8_t reg) {
    (void)reg;assert(!"storage refusal accessed physical CMOS");return 0;
}
#define MSDOS_TEST_CMOS forbidden_cmos
#include "../kernel/msdos/platform.c"

static char output[8192];
static size_t output_len;
static uint8_t floppy[FLOPPY_BYTES];
static unsigned media_reads;
static int cancel, fail_media;

void *kmalloc(size_t n) { return malloc(n); }
void *kcalloc(size_t n, size_t s) { return calloc(n,s); }
void kfree(void *p) { free(p); }
int kmemcmp(const void *a,const void *b,size_t n) { return memcmp(a,b,n); }
int kstrcmp(const char *a,const char *b) { return strcmp(a,b); }
void vga_puts(const char *s) {
    assert(output_len+strlen(s)<sizeof(output));
    strcpy(output+output_len,s);output_len+=strlen(s);
}
void vga_printf(const char *fmt,...) {
    va_list ap;va_start(ap,fmt);
    int n=vsnprintf(output+output_len,sizeof(output)-output_len,fmt,ap);
    va_end(ap);assert(n>=0 && (size_t)n<sizeof(output)-output_len);
    output_len+=(size_t)n;
}
void vga_set_cursor(int r,int c) { (void)r;(void)c; }
void vga_get_cursor(int *r,int *c) { *r=*c=0; }
uint32_t timer_get_ticks(void) { return 0; }
int keyboard_poll(void) { return cancel ? KEY_F8 : -1; }
int msdos_cpu_run(uint32_t n) { (void)n;assert(!"refused disk booted");return 0; }
void msdos_devices_restore(void) {}
int iso9660_find_file(uint8_t d,const char *name,iso9660_dirent_t *e) {
    ++media_reads;
    if (d || strcmp(name,"D6221.IMG")) return 0;
    memset(e,0,sizeof(*e));e->size=FLOPPY_BYTES;return 1;
}
int32_t iso9660_read_file(uint8_t d,const iso9660_dirent_t *e,
                         uint8_t *b,uint32_t n) {
    (void)d;(void)e;++media_reads;
    if (fail_media) return -1;
    memcpy(b,floppy,n);return n;
}
int cdrom_read_sector(uint8_t d,uint32_t lba,uint8_t *b) {
    (void)d;++media_reads;
    if (fail_media || lba>=FLOPPY_BYTES/2048) return -1;
    memcpy(b,floppy+(size_t)lba*2048,2048);return 0;
}
const cdrom_drive_t *cdrom_drives(void) {
    static const cdrom_drive_t drives[CDROM_MAX_DRIVES]={{.present=1}};
    return drives;
}
uint32_t cdrom_rescan_media(uint8_t d) {(void)d;return FLOPPY_BYTES/2048-1;}

static void fixture(uint32_t sectors) {
    total=sectors;
    installed(2048,sectors-2048);
    memset(disk+446,0,sizeof(native_part_t)); /* no overlapping foreign partition */
    fs_volume_count=0;
    output_len=0;output[0]=0;media_reads=0;cancel=fail_media=0;
    uint8_t keep[512];memset(keep,0x79,sizeof(keep));
    assert(fs_write(0,"KEEP.TXT",keep,sizeof(keep))==FS_OK);
    writes=write_attempts=0;
    memset(floppy,0,sizeof(floppy));
    w16(floppy+11,512);floppy[510]=0x55;floppy[511]=0xaa;
    uint8_t *e=floppy+19*512;
    memcpy(e,"IO      SYS",11);w16(e+26,2);w32(e+28,512);
    memcpy(e+32,"MSDOS   SYS",11);w16(e+32+26,3);w32(e+32+28,512);
    memset(floppy+33*512,'I',512);memset(floppy+34*512,'M',512);
    floppy[515]=0xff;floppy[516]=0xff;floppy[517]=0xff;
}
static void kept(void) {
    uint8_t b[512];uint32_t n;
    assert(fs_read(0,"KEEP.TXT",b,sizeof(b),&n)==FS_OK && n==512);
    for (unsigned i=0;i<512;i++) assert(b[i]==0x79);
}
static void refusal(const char *message) {
    size_t bytes=(size_t)total*512;
    uint8_t *before=malloc(bytes);assert(before);memcpy(before,disk,bytes);
    assert(msdos_run(0,NULL)==1);
    assert(strstr(output,message));
    assert(!strstr(output,"Creating private"));
    assert(!write_attempts && !media_reads && !memcmp(before,disk,bytes));
    free(before);
}
int main(void) {
    disk=malloc((size_t)TEST_TOTAL*512);assert(disk);
    fixture(10*2048);
    fs_image_space_t space;
    assert(fs_image_space(0,&space)==FS_OK);
    assert(space.available_sectors==total-allowed_start-1);
    assert(space.free_slots==14 && !write_attempts);
    fs_image_t untouched={7,123,456}, image=untouched;
    assert(fs_image_reserve(0,DISK_SECTORS,&image)==FS_ERR_NOSPC);
    assert(!memcmp(&image,&untouched,sizeof(image)));
    refusal("insufficient disk space");
    char available[80];
    snprintf(available,sizeof(available),"has %u KiB safely available",
             space.available_sectors/2);
    assert(strstr(output,"requires 16380 KiB") && strstr(output,available));
    assert(strstr(output,"32 MiB or larger") && strstr(output,"do not erase"));
    kept();

    /* Full directory with ample space must not be called a space shortage. */
    fixture(32*2048);
    for (int i=1;i<FS_MAX_FILES;i++) {
        char name[24];snprintf(name,sizeof(name),"FILE%d",i);
        assert(fs_write(0,name,(const uint8_t *)"x",1)==FS_OK);
    }
    writes=write_attempts=0;
    assert(fs_image_space(0,&space)==FS_OK && !space.free_slots);
    assert(space.available_sectors>DISK_SECTORS);
    assert(fs_image_reserve(0,DISK_SECTORS,&image)==FS_ERR_FULL);
    refusal("native file directory is full");
    assert(!strstr(output,"insufficient disk space"));kept();

    /* Actual directory read failure refuses without any seed/media writes. */
    fixture(32*2048);fail_read_lba=allowed_dir;
    refusal("disk I/O failure");
    fail_read_lba=-1;kept();
    fixture(32*2048);fail_read_lba=allowed_dir;
    space=(fs_image_space_t){123,9};
    assert(fs_image_space(0,&space)==FS_ERR_IO);
    assert(space.available_sectors==123 && space.free_slots==9);
    fail_read_lba=-1;

    /* Write error in first initialization chunk: no published image. */
    fixture(32*2048);fail_write_lba=allowed_start+1;
    assert(msdos_run(0,NULL)==1 && write_attempts==1 && !writes);
    assert(strstr(output,"disk I/O failure") && !strstr(output,"cancelled"));
    assert(fs_image_open(0,IMAGE_NAME,&image)==FS_ERR_NOTFOUND);kept();

    fixture(32*2048);cancel=1;
    assert(msdos_run(0,NULL)==1 && writes==128);
    assert(strstr(output,"cancelled with F8") && !strstr(output,"disk I/O failure"));
    assert(fs_image_open(0,IMAGE_NAME,&image)==FS_ERR_NOTFOUND);kept();

    /* Enough space: exercise real initialization and publication, without
     * entering the guest or touching physical VGA/CMOS from this host test. */
    fixture(32*2048);
    assert(!reserve_disk(0) && !write_attempts);
    disc=0;assert(!load_media(0,media_names[0]));
    assert(!seed_disk());
    assert(fs_image_open(0,IMAGE_NAME,&image)==FS_OK);
    assert(image.sectors==DISK_SECTORS && image.start==allowed_start+1);
    assert(sector(image.start)[510]==0x55 && sector(image.start)[511]==0xaa);
    assert(!memcmp(sector(image.start+98),floppy+33*512,512));kept();
    puts("PASS: 10MiB refusal, required/available space, 32MiB seeding, full directory, ATA read/write failure, F8 cancellation; native files preserved");
    free(disk);return 0;
}
