/* Host fault model around the real FatFs and USB filesystem adapter.
 * Invoke with disposable FAT16/FAT32 images from run_usb_storage_test.py.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/usb_storage.h"
#include "../kernel/fatfs/ff.h"
#include "../kernel/fatfs/diskio.h"

static FILE *media[4];
static unsigned present[4], releases[4], writes[4], sync_fail[4], write_fail[4];
static unsigned release_fail[4], blocks[4], wrong_size[4], scans;
void *kmemset(void *p,int c,size_t n) {return memset(p,c,n);}
void *kmemcpy(void *p,const void *q,size_t n) {return memcpy(p,q,n);}
int kmemcmp(const void *p,const void *q,size_t n) {return memcmp(p,q,n);}
size_t kstrlen(const char *p) {return strlen(p);}
char *kstrcpy(char *p,const char *q) {return strcpy(p,q);}
char *kstrncpy(char *p,const char *q,size_t n) {return strncpy(p,q,n);}
void timer_sleep(uint32_t ms) {(void)ms;}
int usb_transport_scan(void) {scans++;return present[0]+present[1]+present[2]+present[3];}
void usb_transport_forget_absent(void) {}
int usb_transport_present(unsigned i) {return i<4 && present[i];}
int usb_transport_release(unsigned i) {
    assert(i<4);
    if (release_fail[i]) return -1;
    present[i]=0;releases[i]++;return 0;
}
int usb_transport_command(unsigned i,const uint8_t *cdb,unsigned len,
                             uint8_t *buffer,unsigned bytes,int write) {
    assert(i<4 && present[i] && len<=16);
    if (cdb[0]==0x35) {assert(!bytes);return sync_fail[i] ? -2 : 0;}
    if (cdb[0]==0x25) {
        assert(bytes==8 && !write);
        uint32_t values[]={blocks[i]-1,wrong_size[i] ? 4096 : 512};
        for(unsigned n=0;n<2;n++) for(unsigned j=0;j<4;j++)
            buffer[n*4+j]=(uint8_t)(values[n]>>(24-j*8));
        return 0;
    }
    assert(cdb[0]==0x28 || cdb[0]==0x2A);
    assert(bytes==512 && cdb[7]==0 && cdb[8]==1);
    uint32_t lba=((uint32_t)cdb[2]<<24)|((uint32_t)cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
    assert(lba<blocks[i] && media[i]);
    if (write && write_fail[i]) return -2;
    assert(!fseek(media[i],(long)lba*512,SEEK_SET));
    if (write) {assert(fwrite(buffer,1,512,media[i])==512);writes[i]++;}
    else assert(fread(buffer,1,512,media[i])==512);
    return 0;
}
int main(int argc,char **argv) {
    assert(argc==3);
    for(unsigned i=0;i<2;i++) {
        media[i]=fopen(argv[i+1],"r+b");assert(media[i]);
        assert(!fseek(media[i],0,SEEK_END));
        blocks[i]=(unsigned)(ftell(media[i])/512);present[i]=1;
    }
    media[2]=tmpfile();assert(media[2]);
    uint8_t zero[512]={0};
    for(unsigned n=0;n<128;n++) assert(fwrite(zero,1,512,media[2])==512);
    blocks[2]=128;present[2]=1;
    uint8_t out[512];uint32_t n=0;
    const uint8_t text[]="USB persisted write\n";
    assert(usb_path("usb0:\\HELLO.TXT") && usb_path("USB9:\\BAD.TXT"));
    assert(!usb_path("USBNOTES.TXT") && !usb_path("U") && !usb_path(""));
    assert(usb_file_read("USB0:\\HELLO.TXT",out,sizeof(out),&n)==-101);
    assert(usb_storage_mount_all()==2);
    assert(!writes[0] && !writes[1] && !writes[2]); /* no autoformat */
    assert(!usb_storage_volumes()[2].mounted);
    assert(strstr(usb_storage_volumes()[2].status,"Not FAT"));
    assert(usb_file_read("USB9:\\HELLO.TXT",out,sizeof(out),&n)==-100);
    assert(usb_file_read("USB0:\\HELLO.TXT",out,1,&n)==-102);
    assert(!usb_file_write("USB1:\\HOST.TXT",text,sizeof(text)-1));
    assert(!usb_file_read("USB1:\\HOST.TXT",out,sizeof(out),&n));
    assert(n==sizeof(text)-1 && !memcmp(out,text,n));
    uint32_t full_size=0;
    assert(!usb_file_read_at("USB1:\\HOST.TXT",1,out,3,&full_size));
    assert(full_size==sizeof(text)-1 && !memcmp(out,text+1,3));
    unsigned before=writes[1];
    assert(usb_file_read_at("USB1:\\HOST.TXT",full_size,out,1,&full_size)==-102);
    assert(usb_file_read_at("USB1:\\HOST.TXT",0xffffffff,out,2,&full_size)==-102);
    assert(writes[1]==before);
    before=writes[1];
    assert(usb_file_write("USB1:\\HUGE.TXT",text,USB_FILE_MAX+1)==-102);
    assert(writes[1]==before);
    assert(disk_read(0,out,blocks[0],1)==RES_PARERR);
    assert(disk_write(0,out,blocks[0]-1,2)==RES_PARERR);
    assert(disk_read(0,out,0,0)==RES_PARERR);

    /* A flush failure must not disconnect that drive or report it safe.
     * Other drives must still be processed (eject-all, not fail-fast). */
    sync_fail[0]=1;
    assert(usb_storage_eject_all()==-1);
    assert(present[0] && !releases[0]);
    assert(!present[1] && releases[1]==1);
    assert(strstr(usb_storage_volumes()[0].status,"DO NOT remove"));
    sync_fail[0]=0;release_fail[0]=1;
    assert(usb_storage_eject_all()==-1);
    assert(present[0] && !releases[0]);
    release_fail[0]=0;
    assert(!usb_storage_eject_all() && !present[0]);
    assert(usb_file_read("USB0:\\HELLO.TXT",out,sizeof(out),&n)==-101);

    /* Re-enumerate, reject unsupported 4Kn, and quarantine failed writes. */
    present[0]=present[1]=1;wrong_size[1]=1;
    assert(usb_storage_mount_all()==1);
    assert(!usb_storage_volumes()[1].mounted);
    assert(strstr(usb_storage_volumes()[1].status,"sector size"));
    write_fail[0]=1;
    assert(usb_file_write("USB0:\\FAIL.TXT",text,sizeof(text)-1)<0);
    assert(!usb_storage_volumes()[0].mounted);
    assert(usb_file_read("USB0:\\HELLO.TXT",out,sizeof(out),&n)==-101);
    write_fail[0]=0;
    assert(!usb_storage_eject_all());
    for(unsigned i=0;i<3;i++) fclose(media[i]);
    puts("PASS: actual FatFs read/write; no writes on mount; size/LBA bounds; "
         "flush/release failures never report safe; other drives still eject; "
         "4Kn rejection; failed-write mount quarantine");
    return 0;
}
