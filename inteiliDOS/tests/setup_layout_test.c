/* Installer partition preflight uses the actual wizard implementation. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../shell/setup.c"
static uint8_t mbr[512],legacy[512];
void *kmemcpy(void *d,const void *s,size_t n){return memcpy(d,s,n);}
void *kmemset(void *d,int c,size_t n){return memset(d,c,n);}
int ata_read_sector(uint8_t drive,uint32_t lba,uint8_t *b) {
    assert(!drive);assert(lba==0 || lba==2048);
    memcpy(b,lba?legacy:mbr,512);return 0;
}
static void fixture(void) {
    memset(mbr,0,512);memset(legacy,0,512);
    mbr[510]=0x55;mbr[511]=0xaa;
    sw_drive=0;sw_drives[0].total_sectors=100000;
}
int main(void) {
    fixture();assert(sw_build_mbr(0)==0);
    assert(sw_get_part_lba()==2048);
    assert(native_partition_find(sw_sector,100000)==0);
    fixture();
    native_part_t *p=(native_part_t *)(mbr+446);
    p[0].type=0x0b;p[0].status=0x80;p[0].start=2048;p[0].sectors=50000;
    assert(sw_build_mbr(1)==0);assert(sw_get_part_lba()==52048);
    assert(((native_part_t *)(sw_sector+446))[0].sectors==50000);
    assert(((native_part_t *)(sw_sector+446))[1].sectors==47952);
    assert(!memcmp(mbr+447,sw_sector+447,15)); /* preserve foreign extent */
    fixture();p[0].type=0x0b;p[0].start=2048;p[0].sectors=97000;
    assert(sw_build_mbr(1)==-3);
    fixture();p[0].type=0x0b;p[0].start=2048;p[0].sectors=0xffffffffu;
    assert(sw_build_mbr(1)==-3);
    fixture();p[0].type=0x99;p[0].start=2048;p[0].sectors=50000;
    assert(sw_build_mbr(1)==-3);
    fixture();mbr[510]=0;assert(sw_build_mbr(1)==-3);
    fixture();uint32_t magic=NATIVE_FS_MAGIC;memcpy(legacy,&magic,4);
    assert(sw_build_mbr(1)==-3); /* no implicit migration of flat files */
    fixture();sw_drives[0].total_sectors=2048+NATIVE_DATA_OFFSET;
    assert(sw_build_mbr(0)==-3);
    assert(strstr(sw_layout_error,"too small"));
    fixture();sw_drives[0].total_sectors=0;
    assert(sw_build_mbr(0)==-3);
    assert(strstr(sw_layout_error,"zero capacity"));
    /* A confirmed clean install must not take preservation/migration checks,
     * even when old native partitions/files have an unsafe legacy layout. */
    fixture();p[0].type=0x99;p[0].start=2048;p[0].sectors=50000;
    memcpy(legacy,&magic,4);
    assert(sw_build_mbr(0)==0);
    assert(sw_get_part_lba()==2048);
    assert(((native_part_t *)(sw_sector+446))[0].sectors==100000-2048);
    assert(native_partition_find(sw_sector,100000)==0);
    fixture();mbr[510]=0;memcpy(legacy,&magic,4);
    assert(sw_build_mbr(0)==0); /* explicit erase accepts an unsigned old disk */
    fixture();p[0].type=0x99;p[0].start=2048;p[0].sectors=50000;
    assert(sw_build_mbr(1)==-3);
    assert(strstr(sw_layout_error,"cannot upgrade"));
    puts("PASS: installer clean/dual bounds, foreign extent preservation, legacy/old install refusal");
}
