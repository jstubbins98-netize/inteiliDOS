/* Real FatFs plus disposable, fault-injected USB disks. No native disk backend. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/usb_storage.c"
#include "../shell/usb_command.c"

static FILE *media[4];
static unsigned present[4],sectors[4],writes[4],syncs[4],scans;
static unsigned fail_write_at[4],fail_sync_at[4];
static const char *keys="";
static unsigned key_index,disconnect_at;
static char output[32768];

void *kmemset(void *p,int c,size_t n) {return memset(p,c,n);}
void *kmemcpy(void *p,const void *q,size_t n) {return memcpy(p,q,n);}
int kmemcmp(const void *p,const void *q,size_t n) {return memcmp(p,q,n);}
size_t kstrlen(const char *p) {return strlen(p);}
char *kstrcpy(char *p,const char *q) {return strcpy(p,q);}
int kstrcmp(const char *p,const char *q) {return strcmp(p,q);}
void timer_sleep(uint32_t ticks) {(void)ticks;}
void vga_puts(const char *s) {
    assert(strlen(output)+strlen(s)<sizeof(output));strcat(output,s);
}
void vga_putchar(char c) {
    char text[2]={c,0};vga_puts(text);
}
void vga_printf(const char *fmt,...) {
    char text[1024];va_list ap;va_start(ap,fmt);
    vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);vga_puts(text);
}
int keyboard_getchar(void) {
    assert(keys[key_index] && "unexpected interactive prompt");
    if (disconnect_at && key_index==disconnect_at) present[0]=0;
    unsigned c=(unsigned char)keys[key_index++];
    return c==27?KEY_ESCAPE:c==8?KEY_BACKSPACE:(int)c;
}
int usb_pick_and_copy(void) {assert(0);return -1;}
const usb_xhci_info_t *usb_xhci_info(void) {
    static usb_xhci_info_t info={.status="test"};return &info;
}
const usb_ehci_info_t *usb_ehci_info(void) {
    static usb_ehci_info_t info={.status="test"};return &info;
}
int usb_transport_scan(void) {scans++;return present[0]+present[1];}
void usb_transport_forget_absent(void) {}
int usb_transport_present(unsigned i) {return i<4 && present[i];}
int usb_transport_release(unsigned i) {present[i]=0;return 0;}
static void put_be32(uint8_t *p,uint32_t n) {
    p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;
}
int usb_transport_command(unsigned i,const uint8_t *cdb,unsigned len,
                          uint8_t *buffer,unsigned bytes,int write) {
    (void)len;
    if (!usb_transport_present(i)) return -1;
    if (cdb[0]==0x25) {
        assert(bytes==8);put_be32(buffer,sectors[i]-1);put_be32(buffer+4,512);return 0;
    }
    if (cdb[0]==0x35) {
        syncs[i]++;
        return fail_sync_at[i] && syncs[i]>=fail_sync_at[i]?-1:fflush(media[i]);
    }
    assert(cdb[0]==0x28 || cdb[0]==0x2a);
    assert(bytes==512 && cdb[7]==0 && cdb[8]==1);
    assert(write==(cdb[0]==0x2a));
    uint32_t lba=be32(cdb+2);
    if (lba>=sectors[i]) return -1;
    if (write && (++writes[i],fail_write_at[i] && writes[i]>=fail_write_at[i])) return -1;
    if (fseek(media[i],(long)lba*512,SEEK_SET)) return -1;
    return (write?fwrite(buffer,1,512,media[i]):fread(buffer,1,512,media[i]))==512?0:-1;
}
static void bootstrap(unsigned drive) {
    char path[3];volume_name(drive,path);f_mount(0,path,0);
    present[drive]=1;fail_write_at[drive]=fail_sync_at[drive]=0;
    volumes[drive].sectors=sectors[drive];volumes[drive].mounted=0;
    MKFS_PARM p={.fmt=FM_FAT,.n_fat=2};
    uint32_t work[128];
    assert(f_mkfs(path,&p,work,sizeof(work))==FR_OK);
}
static const uint8_t sentinel[]="THIS OTHER USB MUST NOT BE FORMATTED";
static void check_sentinel(unsigned expected_writes) {
    uint8_t b[128];uint32_t length=0;
    assert(usb_file_read("USB1:\\KEEP.TXT",b,sizeof(b),&length)==0);
    assert(length==sizeof(sentinel) && !memcmp(b,sentinel,length));
    assert(writes[1]==expected_writes && volumes[1].mounted);
}
static int command(int argc,const char **argv,const char *input) {
    keys=input;key_index=0;output[0]=0;
    int rc=usb_command(argc,argv);
    assert(key_index==strlen(input));return rc;
}
static void no_writes_command(int argc,const char **argv,const char *input,int success) {
    unsigned before=writes[0],other=writes[1],scan=scans;
    assert((command(argc,argv,input)==0)==success);
    assert(writes[0]==before && scans==scan);
    check_sentinel(other);
}
static void restore_target(void) {
    bootstrap(0);assert(usb_storage_mount_all()==2);
    const uint8_t old[]="OLD FILE";
    assert(usb_file_write("USB0:\\OLD.TXT",old,sizeof(old))==0);
}
int main(int argc,char **argv) {
    assert(argc==4);
    unsigned final_format=(unsigned)atoi(argv[3]);
    const char *interactive[]={"/FORMAT"};
    assert(command(1,interactive,"")==-1 && !writes[0]);
    for (unsigned i=0;i<2;i++) {
        media[i]=fopen(argv[i+1],"r+b");assert(media[i]);
        fseek(media[i],0,SEEK_END);sectors[i]=(unsigned)(ftell(media[i])/512);
        bootstrap(i);
    }
    assert(usb_storage_mount_all()==2);
    assert(usb_file_write("USB1:\\KEEP.TXT",sentinel,sizeof(sentinel))==0);
    restore_target();
    unsigned other=writes[1];
    const char *direct[]={"/FORMAT","USB0:","FAT32"};
    const char *native[]={"/FORMAT","C:","FAT32"};
    const char *file[]={"/FORMAT","USB0:\\OLD.TXT","FAT32"};
    const char *unsupported[]={"/FORMAT","USB0:","NTFS"};
    const char *unmounted[]={"/FORMAT","USB3:","FAT32"};
    const char *extra[]={"/FORMAT","USB0:","FAT32","YES"};
    no_writes_command(3,native,"",0);
    no_writes_command(3,file,"",0);
    no_writes_command(3,unsupported,"",0);
    no_writes_command(3,unmounted,"",0);
    no_writes_command(4,extra,"",0);
    no_writes_command(1,interactive,"\n",1);
    no_writes_command(1,interactive,"\033",1);
    no_writes_command(1,interactive,"USB0:\n\033",1);
    no_writes_command(3,direct,"YES\n",1);
    no_writes_command(3,direct,"FORMAT USB1:\n",1);
    no_writes_command(3,direct,"FORMAT USB0:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",1);
    /* A disappearing target is not replaced with another mounted USB slot. */
    disconnect_at=12;
    no_writes_command(3,direct,"FORMAT USB0:\n",0);
    disconnect_at=0;present[0]=1;
    usb_format_plan_t plan;
    assert(usb_storage_prepare_format(0,USB_FORMAT_FAT32,&plan)==0);
    unsigned before=writes[0];
    assert(usb_storage_mount_all()==2);
    assert(usb_storage_format(&plan)==-106 && writes[0]==before);
    /* Same-size replacement / external VBR change between prepare and commit. */
    assert(usb_storage_prepare_format(0,USB_FORMAT_FAT32,&plan)==0);
    assert(fseek(media[0],(long)filesystems[0].volbase*512+39,SEEK_SET)==0);
    int original=fgetc(media[0]);
    assert(fseek(media[0],-1,SEEK_CUR)==0);
    assert(fputc(original^1,media[0])!=EOF);fflush(media[0]);
    assert(usb_storage_format(&plan)==-106 && writes[0]==before);
    restore_target();
    assert(usb_storage_prepare_format(0,USB_FORMAT_FAT32,&plan)==0);
    before=writes[0];fail_sync_at[0]=syncs[0]+1;
    assert(usb_storage_format(&plan)==-107 && volumes[0].mounted && writes[0]==before);
    fail_sync_at[0]=0;
    /* Reject unsupported sizes and plan IDs before any formatting writes. */
    uint32_t saved=sectors[0];
    volumes[0].sectors=sectors[0]=4194305;
    assert(usb_storage_prepare_format(0,USB_FORMAT_FAT16,&plan)==-105);
    volumes[0].sectors=sectors[0]=32768;
    assert(usb_storage_prepare_format(0,USB_FORMAT_FAT32,&plan)==-105);
    volumes[0].sectors=sectors[0]=saved;
    assert(usb_storage_prepare_format(4,USB_FORMAT_FAT32,&plan)==-105);
    assert(usb_storage_prepare_format(0,0,&plan)==-105);
    assert(writes[0]==before);
    /* Exercise every offered filesystem and real post-format read/write. */
    for (unsigned fs=USB_FORMAT_FAT16;fs<=USB_FORMAT_EXFAT;fs++) {
        restore_target();
        assert(usb_storage_prepare_format(0,fs,&plan)==0);
        assert(usb_storage_format(&plan)==0 && volumes[0].mounted);
        assert(filesystems[0].fs_type==(fs==USB_FORMAT_FAT16?FS_FAT16:fs==USB_FORMAT_FAT32?FS_FAT32:FS_EXFAT));
        uint8_t b[64];uint32_t n;
        assert(usb_file_read("USB0:\\OLD.TXT",b,sizeof(b),&n)<0);
        const uint8_t data[]="NEW FILE AFTER FORMAT";
        assert(usb_file_write("USB0:\\NEW.TXT",data,sizeof(data))==0);
        assert(usb_file_read("USB0:\\NEW.TXT",b,sizeof(b),&n)==0);
        assert(n==sizeof(data) && !memcmp(data,b,n));
        assert(usb_storage_format(&plan)==-106); /* A plan is single-use. */
        check_sentinel(other);
    }
    restore_target();
    assert(usb_storage_prepare_format(0,USB_FORMAT_FAT32,&plan)==0);
    fail_write_at[0]=writes[0]+3;
    assert(usb_storage_format(&plan)==-108 && !volumes[0].mounted);
    fail_write_at[0]=0;check_sentinel(other);
    restore_target();
    assert(usb_storage_prepare_format(0,USB_FORMAT_FAT32,&plan)==0);
    fail_sync_at[0]=syncs[0]+3; /* Fail the explicit final device flush. */
    assert(usb_storage_format(&plan)==-109 && !volumes[0].mounted);
    fail_sync_at[0]=0;check_sentinel(other);
    restore_target();
    /* Bare command: drive, filesystem, typed confirmation, remount. */
    assert(command(1,interactive,"USB0:\nEXFAT\nFORMAT USB0:\n")==0);
    assert(strstr(output,"formatted as exFAT and remounted"));
    const char *lower[]={"/format","usb0:","fat32"};
    assert(command(3,lower,"FORMAT USB0:\n")==0);
    assert(strstr(output,"formatted as FAT32 and remounted"));
    assert(usb_storage_prepare_format(0,final_format,&plan)==0);
    assert(usb_storage_format(&plan)==0);
    check_sentinel(other);
    for (unsigned i=0;i<2;i++) {fflush(media[i]);fclose(media[i]);}
    puts("PASS: real FAT16/FAT32/exFAT format/remount/read/write; safe UI, target/media checks, cancellation, flush/partial-write failures and other-drive preservation");
    return 0;
}
