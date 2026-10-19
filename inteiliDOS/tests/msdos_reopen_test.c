/* Exercise saved-image selection with real startup code, no physical VGA or
 * CD hardware. Image seeding and the CPU are not executed by this fixture. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
static uint8_t forbidden_cmos(uint8_t reg) {
    (void)reg;assert(!"disk preparation accessed physical CMOS");return 0;
}
#define MSDOS_TEST_CMOS forbidden_cmos
#include "../kernel/msdos/platform.c"

static int images[ATA_MAX_DRIVES], cd_calls, boot_calls, reserve_calls;
static int key=-1, expected_drive, fdc_calls;
static char output[4096];
void *kmalloc(size_t n) {return malloc(n);}
void *kcalloc(size_t n,size_t s) {return calloc(n,s);}
void kfree(void *p) {free(p);}
void *kmemcpy(void *d,const void *s,size_t n) {return memcpy(d,s,n);}
void *kmemset(void *d,int c,size_t n) {return memset(d,c,n);}
int kmemcmp(const void *a,const void *b,size_t n) {return memcmp(a,b,n);}
int kstrcmp(const char *a,const char *b) {return strcmp(a,b);}
size_t kstrlen(const char *s) {return strlen(s);}
char *kstrcpy(char *d,const char *s) {return strcpy(d,s);}
static usb_volume_info_t usb_volumes[USB_STORAGE_MAX];
const usb_volume_info_t *usb_storage_volumes(void) {return usb_volumes;}
int usb_file_read_at(const char *p,uint32_t o,uint8_t *b,uint32_t n,uint32_t *s) {
    (void)p;(void)o;(void)b;(void)n;(void)s;
    assert(!"unexpected USB read");return -1;
}
const char *usb_storage_error(int rc) {(void)rc;return "USB error";}
void vga_puts(const char *s) {assert(strlen(output)+strlen(s)<sizeof(output));strcat(output,s);}
void vga_printf(const char *f,...) {
    va_list a;va_start(a,f);size_t n=strlen(output);
    vsnprintf(output+n,sizeof(output)-n,f,a);va_end(a);
}
void vga_get_cursor(int *r,int *c) {*r=*c=0;}
void vga_set_cursor(int r,int c) {(void)r;(void)c;}
uint32_t timer_get_ticks(void) {return 0;}
int keyboard_poll(void) {int k=key;key=-1;return k;}
void msdos_devices_restore(void) {}
int msdos_devices_graphics(void) {return 0;}
void msdos_devices_update(const uint8_t *a,const uint8_t *b) {(void)a;(void)b;}
int msdos_cpu_run(uint32_t n) {
    (void)n;assert(hard.drive==expected_drive);
    assert(!cd_calls && !reserve_calls);++boot_calls;return 0;
}
int fs_volume_drive(uint8_t v,uint8_t *d) {assert(!v);*d=0;return 0;}
int fs_image_open(uint8_t d,const char *n,fs_image_t *i) {
    assert(!strcmp(n,IMAGE_NAME));
    if (!images[d]) return d ? FS_ERR_NODRV : FS_ERR_NOTFOUND;
    if (images[d]==3) return FS_ERR_IO;
    *i=(fs_image_t){.drive=d,.start=100,.sectors=images[d]==2?1:DISK_SECTORS};
    return 0;
}
int fs_image_space(uint8_t d,fs_image_space_t *s) {
    (void)d;*s=(fs_image_space_t){.free_slots=1,.available_sectors=DISK_SECTORS};return 0;
}
int fs_image_reserve(uint8_t d,uint32_t n,fs_image_t *i) {
    *i=(fs_image_t){.drive=d,.start=100,.sectors=n};++reserve_calls;return 0;
}
int fs_image_publish(const char *n,const fs_image_t *i) {
    (void)n;(void)i;assert(!"unexpected image publication");return FS_ERR_IO;
}
int fs_image_zero(const fs_image_t *i,uint32_t s,uint32_t n) {
    (void)i;(void)s;(void)n;assert(!"unexpected image clearing");return FS_ERR_IO;
}
int fs_image_sector(const fs_image_t *i,uint32_t s,uint8_t *b,int w) {
    (void)i;(void)s;(void)b;(void)w;assert(!"unexpected image write");return FS_ERR_IO;
}
const cdrom_drive_t *cdrom_drives(void) {
    static const cdrom_drive_t d[CDROM_MAX_DRIVES]={{.present=1}};
    ++cd_calls;return d;
}
uint32_t cdrom_rescan_media(uint8_t d) {(void)d;++cd_calls;return 0;}
int iso9660_find_file(uint8_t d,const char *n,iso9660_dirent_t *e) {
    (void)d;(void)n;(void)e;++cd_calls;return 0;
}
int32_t iso9660_read_file(uint8_t d,const iso9660_dirent_t *e,uint8_t *b,uint32_t n) {
    (void)d;(void)e;(void)b;(void)n;assert(!"unexpected CD read");return -1;
}
int cdrom_read_sector(uint8_t d,uint32_t lba,uint8_t *b) {
    (void)d;(void)lba;(void)b;assert(!"unexpected CD read");return -1;
}
int fdc_init(void) {++fdc_calls;return -1;}
void fdc_last_read_status(fdc_read_status_t *out) {
    *out=(fdc_read_status_t){-1,-1,-1,0xffff};
}
int fdc_read_sector(uint8_t d,uint32_t lba,uint8_t *b) {
    assert(!"read after controller initialization failed");
    (void)d;(void)lba;(void)b;return -1;
}
static void fixture(void) {
    memset(images,0,sizeof(images));output[0]=0;
    cd_calls=boot_calls=reserve_calls=fdc_calls=0;key=-1;
}
int main(void) {
    fixture();images[0]=1;expected_drive=0;
    assert(!prepare_disk(0,NULL) && hard.drive==expected_drive && !cd_calls);
    /* Prefer the current C: image, even if another native disk has one. */
    fixture();images[0]=images[1]=1;expected_drive=0;
    assert(!prepare_disk(0,NULL) && hard.drive==expected_drive);
    fixture();images[2]=1;expected_drive=2;
    assert(!prepare_disk(0,NULL) && hard.drive==2 && !cd_calls);
    fixture();images[1]=images[2]=1;
    assert(prepare_disk(0,NULL)==1 && strstr(output,"multiple saved"));
    assert(!boot_calls && !reserve_calls && !cd_calls);
    fixture();images[1]=2;
    assert(prepare_disk(0,NULL)==1 && strstr(output,"invalid virtual HDD size"));
    assert(!reserve_calls && !cd_calls);
    fixture();images[1]=3;
    assert(prepare_disk(0,NULL)==1 && strstr(output,"disk I/O failure"));
    assert(!reserve_calls && !cd_calls);
    fixture();images[0]=1;expected_drive=0;
    assert(!prepare_disk(0,NULL));
    uint8_t ram[0x500]={0},ports[1]={0};
    key=KEY_F1+5;assert(!msdos_pc_poll(ram,ports) && !io_failed);
    assert(strstr(output,"could not load next"));
    /* Explicit /IMAGE still requires readable media; no replacement HDD. */
    fixture();images[0]=1;
    const char *a[]={"/IMAGE","GAME.IMG"};
    assert(prepare_disk(2,a)==1 && !boot_calls && !reserve_calls);
    /* Selecting A: must not require it to be present to boot an existing C:.
     * Its controller is initialized only on an actual guest floppy read. */
    fixture();images[0]=1;
    const char *f[]={"/FLOPPY"};
    assert(!prepare_disk(1,f) && !fdc_calls && !cd_calls);
    uint8_t sector[512];
    assert(!msdos_pc_disk(1,0,sector,sizeof(sector),0) && fdc_calls==1);
    assert(!io_failed); /* optional A: failure does not terminate DOS C: */
    fixture();images[0]=1;
    assert(!prepare_disk(0,NULL) && !fdc_calls && !cd_calls);
    fixture();images[0]=1;
    const char *u[]={"/USB"};
    assert(prepare_disk(1,u)==1 && strstr(output,"USB /MOUNT"));
    assert(!cd_calls && !fdc_calls && !reserve_calls);
    fixture();images[0]=1;
    const char *bad_usb[]={"/USB","USB4:"};
    assert(prepare_disk(2,bad_usb)==1 && strstr(output,"Usage:"));
    puts("PASS: CD-independent saved DOS startup, other-volume recovery, ambiguity/size/I/O protection and optional floppy failure");
}
