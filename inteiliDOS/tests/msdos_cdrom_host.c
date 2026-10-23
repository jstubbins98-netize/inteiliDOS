#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/cdrom.h"
static FILE *image;
static cdrom_drive_t devices[CDROM_MAX_DRIVES];
static int fail_sector=-1;
int fixture_open(const char *path) {
    if (image) fclose(image);
    image=fopen(path,"rb");fail_sector=-1;
    memset(devices,0,sizeof(devices));devices[0].present=CDROM_PRESENT;
    return image?0:-1;
}
void fixture_fail(int sector) {fail_sector=sector;}
const cdrom_drive_t *cdrom_drives(void) {return devices;}
uint32_t cdrom_rescan_media(uint8_t drive) {(void)drive;return 1;}
int cdrom_read_sector(uint8_t drive,uint32_t lba,uint8_t *out) {
    (void)drive;
    if (!image || lba==(uint32_t)fail_sector || fseek(image,(long)lba*2048,SEEK_SET))
        return -1;
    return fread(out,1,2048,image)==2048?0:-1;
}
void *kmalloc(size_t n) {return malloc(n);}
void *kcalloc(size_t n,size_t s) {return calloc(n,s);}
void kfree(void *p) {free(p);}
void *kmemcpy(void *d,const void *s,size_t n) {return memcpy(d,s,n);}
void *kmemset(void *d,int c,size_t n) {return memset(d,c,n);}
int kmemcmp(const void *a,const void *b,size_t n) {return memcmp(a,b,n);}
