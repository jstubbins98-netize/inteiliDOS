/* Private FAT16 AUTOEXEC migration, without any physical disk access. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/msdos/startup.c"

static uint8_t disk_data[520*63*512];
static unsigned writes,fail_sector;
void *kmalloc(size_t n) {return malloc(n);}
void kfree(void *p) {free(p);}
void *kmemcpy(void *d,const void *s,size_t n) {return memcpy(d,s,n);}
void *kmemset(void *d,int c,size_t n) {return memset(d,c,n);}
int kmemcmp(const void *a,const void *b,size_t n) {return memcmp(a,b,n);}
size_t kstrlen(const char *p) {return strlen(p);}
int fs_image_sector(const fs_image_t *image,uint32_t n,uint8_t *buf,int write) {
    assert(n<image->sectors);
    if (write) {
        if (n==fail_sector) return -1;
        ++writes;memcpy(disk_data+n*512,buf,512);
    } else memcpy(buf,disk_data+n*512,512);
    return 0;
}
static fs_image_t image;
static void fixture(const char *text) {
    memset(disk_data,0,sizeof(disk_data));
    memset(&image,0,sizeof(image));image.sectors=520*63;
    writes=0;fail_sector=~0u;
    uint8_t *boot=disk_data+512,*fat=disk_data+2*512;
    set16(boot+11,512);boot[13]=4;set16(boot+14,1);boot[16]=2;
    set16(boot+17,512);set16(boot+22,32);set32(boot+28,1);
    set16(boot+19,image.sectors-1);boot[510]=0x55;boot[511]=0xaa;
    set16(fat,0xfff8);set16(fat+2,0xffff);
    if (text) {
        unsigned size=strlen(text),count=(size+2047)/2048;
        uint8_t *e=disk_data+66*512;
        memcpy(e,"AUTOEXECBAT",11);e[11]=32;
        set16(e+26,count?2:0);set32(e+28,size);
        memcpy(disk_data+98*512,text,size);
        for (unsigned i=0;i<count;i++) set16(fat+(i+2)*2,i+1<count?i+3:0xffff);
    }
    memcpy(disk_data+34*512,fat,16384);
}
static void expect(const char *text) {
    uint8_t *e=disk_data+66*512,*fat=disk_data+2*512;
    assert(get32(e+28)==strlen(text));
    unsigned cluster=get16(e+26),offset=0,size=strlen(text);
    while (offset<size) {
        unsigned bytes=size-offset;if (bytes>2048) bytes=2048;
        assert(!memcmp(disk_data+(98+(cluster-2)*4)*512,text+offset,bytes));
        offset+=bytes;cluster=get16(fat+cluster*2);
    }
    assert(cluster>=0xfff8);
    assert(!memcmp(fat,disk_data+34*512,16384));
}
int main(void) {
    const char *line=MSDOS_BLASTER_LINE "\r\n";
    fixture(NULL);
    assert(!msdos_startup_sound(&image));expect(line);
    writes=0;assert(!msdos_startup_sound(&image) && !writes);
    assert(!msdos_startup_sound_for(&image,1));
    expect(MSDOS_BLASTER16_LINE "\r\n");
    writes=0;assert(!msdos_startup_sound_for(&image,1) && !writes);
    assert(!msdos_startup_sound_for(&image,0));
    expect(line);
    fixture("@ECHO OFF\r\nPATH C:\\\r\n");
    assert(!msdos_startup_sound(&image));
    expect("@ECHO OFF\r\nPATH C:\\\r\n" MSDOS_BLASTER_LINE "\r\n");
    assert(get16(disk_data+2*512+4)==0); /* old cluster reclaimed */
    fixture("REM custom startup\x1a");
    assert(!msdos_startup_sound(&image));
    expect("REM custom startup\r\n" MSDOS_BLASTER_LINE "\r\n");
    fixture("set blaster=a220 i5 d1 t3\r\n");
    assert(!msdos_startup_sound(&image) && !writes);
    char large[5001],expected[5100];
    memset(large,'x',5000);large[5000]=0;
    snprintf(expected,sizeof(expected),"%s\r\n%s",large,line);
    fixture(large);assert(!msdos_startup_sound(&image));expect(expected);
    fixture("REM protected\r\n");
    disk_data[66*512+11]|=1;
    assert(msdos_startup_sound(&image) && !writes);
    fixture("REM full disk\r\n");
    for (unsigned c=3;c<8167;c++) set16(disk_data+2*512+c*2,0xffff);
    assert(msdos_startup_sound(&image) && !writes);
    fixture("REM corrupt chain\r\n");set16(disk_data+2*512+4,2);
    assert(msdos_startup_sound(&image) && !writes);
    fixture("REM original\r\n");fail_sector=102;
    assert(msdos_startup_sound(&image));
    expect("REM original\r\n"); /* failure before FAT/directory publication */
    fixture("REM original\r\n");disk_data[512+13]=8;
    assert(msdos_startup_sound(&image) && !writes);
    puts("PASS: AUTOEXEC sound setting, saved scripts, idempotence, multi-cluster, refusal and I/O safety");
}
