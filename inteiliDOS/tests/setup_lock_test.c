/* Sector-level lock commit tests against the real installer helper. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../shell/setup.c"
static uint8_t disk_sector[512],before[512];
static unsigned reads,writes;
static int read_fail,write_fail,corrupt;
static uint32_t expected_lba;
void *kmemcpy(void *d,const void *s,size_t n) {return memcpy(d,s,n);}
int ata_read_sector(uint8_t drive,uint32_t lba,uint8_t *data) {
    assert(drive==2 && lba==expected_lba);reads++;
    if (read_fail) return -1;
    memcpy(data,disk_sector,512);
    if (corrupt && writes) data[0]^=1;
    return 0;
}
int ata_write_sector(uint8_t drive,uint32_t lba,const uint8_t *data) {
    assert(drive==2 && lba==expected_lba);writes++;
    if (write_fail) return -1;
    memcpy(disk_sector,data,512);return 0;
}
static void fixture(unsigned offset) {
    memset(disk_sector,0xa5,512);
    uint32_t allowed=SW_SETUP_ALLOWED;
    memcpy(disk_sector+offset%512,&allowed,4);
    memcpy(before,disk_sector,512);
    sw_drive=2;reads=writes=0;read_fail=write_fail=corrupt=0;
    expected_lba=2048+1+offset/512;
}
int main(void) {
    unsigned offset=17*512+508;
    fixture(offset);
    assert(!sw_commit_setup_lock(2048,offset));
    uint32_t state;memcpy(&state,disk_sector+508,4);
    assert(state==SW_SETUP_LOCKED && writes==1 && reads==2);
    assert(!memcmp(before,disk_sector,508));
    assert(setup_install_state==SW_SETUP_ALLOWED); /* installer RAM unchanged */
    fixture(offset);read_fail=1;
    assert(sw_commit_setup_lock(2048,offset)<0 && !writes);
    fixture(offset);write_fail=1;
    assert(sw_commit_setup_lock(2048,offset)<0 && !memcmp(before,disk_sector,512));
    fixture(offset);corrupt=1;
    assert(sw_commit_setup_lock(2048,offset)<0);
    fixture(offset);memset(disk_sector,0,512);
    assert(sw_commit_setup_lock(2048,offset)<0 && !writes);
    fixture(offset);
    assert(sw_commit_setup_lock(2048,SW_KERNEL_SECTORS*512u)<0 && !reads);
    assert(sw_commit_setup_lock(2048,509)<0 && !reads);
    puts("PASS: target-only persistent lock commit, unchanged installer, preserved sector bytes, read/write/verify/bounds refusals");
}
