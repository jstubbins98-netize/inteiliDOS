/* USB mass storage volumes. FatFs never sees an ATA/floppy/native device.
 * Files are opened/closed within each operation; no live file handles can
 * survive eject. No autoformat, no write cache outside FatFs, no IRQ I/O.
 */
#include "usb_storage.h"
#include "usb_transport.h"
#include "memory.h"
#include "timer.h"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"

static FATFS filesystems[USB_STORAGE_MAX];
static usb_volume_info_t volumes[USB_STORAGE_MAX];

static void volume_name(unsigned i, char out[3]) {
    out[0]='0'+i;out[1]=':';out[2]=0;
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static int sync_disk(unsigned i) {
    uint8_t cdb[10]={0x35};
    return usb_transport_command(i,cdb,10,0,0,0);
}
static int capacity(unsigned i, uint32_t *sectors) {
    for (unsigned attempt=0;attempt<3;attempt++) {
        uint8_t cdb[10]={0x25}, data[8];
        int result=usb_transport_command(i,cdb,10,data,8,0);
        if (!result) {
            uint32_t last=be32(data);
            if (be32(data+4)!=512 || last==0xFFFFFFFF || !last) return -3;
            *sectors=last+1;return 0;
        }
        if (result!=-2) return -1;
        uint8_t sense_cdb[6]={3,0,0,0,18,0},sense[18];
        if (usb_transport_command(i,sense_cdb,6,sense,18,0)) return -1;
        unsigned key=sense[2]&15;
        if (key!=6 && key!=2) return -1;
        timer_sleep(100);
    }
    return -1;
}
DSTATUS disk_status(BYTE drive) {
    return drive<USB_STORAGE_MAX && volumes[drive].sectors &&
           usb_transport_present(drive) ? 0 : STA_NOINIT;
}
DSTATUS disk_initialize(BYTE drive) { return disk_status(drive); }
static DRESULT disk_transfer(BYTE drive,BYTE *buffer,LBA_t lba,UINT count,int write) {
    if (disk_status(drive)) return RES_NOTRDY;
    if (!buffer || !count || lba>=volumes[drive].sectors ||
        count>volumes[drive].sectors-lba) return RES_PARERR;
    for (UINT n=0;n<count;n++,lba++,buffer+=512) {
        uint8_t cdb[10]={write ? 0x2A : 0x28,0,
                         (uint8_t)(lba>>24),(uint8_t)(lba>>16),
                         (uint8_t)(lba>>8),(uint8_t)lba,0,0,1,0};
        if (usb_transport_command(drive,cdb,10,buffer,512,write)) return RES_ERROR;
    }
    return RES_OK;
}
DRESULT disk_read(BYTE drive,BYTE *buffer,LBA_t lba,UINT count) {
    return disk_transfer(drive,buffer,lba,count,0);
}
DRESULT disk_write(BYTE drive,const BYTE *buffer,LBA_t lba,UINT count) {
    return disk_transfer(drive,(BYTE *)buffer,lba,count,1);
}
DRESULT disk_ioctl(BYTE drive,BYTE cmd,void *buffer) {
    if (disk_status(drive)) return RES_NOTRDY;
    switch(cmd) {
    case CTRL_SYNC: return sync_disk(drive) ? RES_ERROR : RES_OK;
    case GET_SECTOR_COUNT:
        if (!buffer) return RES_PARERR;
        *(LBA_t *)buffer=volumes[drive].sectors;return RES_OK;
    case GET_SECTOR_SIZE:
        if (!buffer) return RES_PARERR;
        *(WORD *)buffer=512;return RES_OK;
    case GET_BLOCK_SIZE:
        if (!buffer) return RES_PARERR;
        *(DWORD *)buffer=1;return RES_OK;
    default: return RES_PARERR;
    }
}

int usb_storage_mount_all(void) {
    /* Finish any previous metadata/cache sync before forgetting its mapping.
     * On failure do not rescan/remap a possibly still-dirty live device. */
    for (unsigned i=0;i<USB_STORAGE_MAX;i++)
        if (volumes[i].mounted && usb_transport_present(i) && sync_disk(i)) {
            volumes[i].status="Drive flush failed; not remounted";return -1;
        }
    for (unsigned i=0;i<USB_STORAGE_MAX;i++) {
        char path[3];volume_name(i,path);f_mount(0,path,0);
        kmemset(&volumes[i],0,sizeof(volumes[i]));
    }
    usb_transport_forget_absent();
    if (usb_transport_scan()<0) return -1;
    int mounted=0;
    for (unsigned i=0;i<USB_STORAGE_MAX;i++) {
        if (!usb_transport_present(i)) continue;
        volumes[i].detected=1;
        int rc=capacity(i,&volumes[i].sectors);
        if (rc) {
            volumes[i].status=rc==-3 ? "Unsupported capacity or sector size (512 required)" :
                                     "Cannot read drive capacity";
            continue;
        }
        char path[3];volume_name(i,path);
        FRESULT result=f_mount(&filesystems[i],path,1);
        if (result==FR_OK) {
            volumes[i].mounted=1;
            volumes[i].status=filesystems[i].fs_type==FS_EXFAT ?
                "Mounted (exFAT read/write)" : "Mounted (FAT read/write)";
            mounted++;
        } else {
            f_mount(0,path,0);
            volumes[i].status=result==FR_NO_FILESYSTEM ?
                "Not FAT12/16/32 or exFAT; no changes made" : "Cannot mount filesystem";
        }
    }
    return mounted;
}
int usb_storage_eject_all(void) {
    int failures=0;
    /* Include drives plugged in since the last /MOUNT, even if no volume has
     * been mounted. The scan leaves all connected, owned slots untouched.
     * There are no open FatFs handles or deferred metadata between calls. */
    int scanned=usb_transport_scan();
    if (scanned<0) {
        for (unsigned i=0;i<USB_STORAGE_MAX;i++)
            if (volumes[i].detected) failures=1;
    }
    for (unsigned i=0;i<USB_STORAGE_MAX;i++) {
        if (!volumes[i].detected && !usb_transport_present(i)) continue;
        if (!usb_transport_present(i)) {
            volumes[i].status="Drive disconnected or failed; safe eject not confirmed";
            volumes[i].mounted=0;
            char path[3];volume_name(i,path);f_mount(0,path,0);
            failures++;continue;
        }
        if (sync_disk(i)) {
            volumes[i].status="Flush failed; DO NOT remove drive";failures++;continue;
        }
        if (usb_transport_release(i)) {
            volumes[i].status="USB disconnect failed; DO NOT remove drive";failures++;continue;
        }
        char path[3];volume_name(i,path);f_mount(0,path,0);
        volumes[i].mounted=0;volumes[i].detected=0;volumes[i].sectors=0;
        volumes[i].status="Ejected; safe to remove";
    }
    return failures ? -1 : 0;
}
const usb_volume_info_t *usb_storage_volumes(void) { return volumes; }
int usb_path(const char *p) {
    if (!p || !(p[0]=='U'||p[0]=='u') || !(p[1]=='S'||p[1]=='s') ||
        !(p[2]=='B'||p[2]=='b')) return 0;
    /* Native filenames like USBNOTES.TXT remain ordinary native names.
     * Invalid numeric IDs still take the USB error path, never a HDD write. */
    p+=3;
    while (*p>='0' && *p<='9') p++;
    return *p==':';
}
/* Require an explicit USBn: path, never fall back to the native HDD. */
static int path_for(const char *src,char out[128],unsigned *drive) {
    if (!usb_path(src) || src[3]<'0' || src[3]>'3' || src[4]!=':') return -100;
    unsigned i=src[3]-'0';
    if (!volumes[i].mounted || !usb_transport_present(i)) return -101;
    size_t len=kstrlen(src+5);
    if (len>123) return -100;
    volume_name(i,out);
    kstrcpy(out+2,src+5);
    *drive=i;return 0;
}
const char *usb_storage_error(int code) {
    switch(code) {
    case -100:return "Use USB0:..USB3: paths; maximum path suffix is 123 bytes.";
    case -101:return "USB drive is not mounted. Run USB /MOUNT.";
    case -102:return "File is too large for this operation (64 KiB maximum).";
    case -103:return "USB drive full or write incomplete; file may be partial.";
    case -104:return "Files of 4 GiB or larger cannot be streamed by this kernel.";
    case -FR_NO_FILE:case -FR_NO_PATH:return "USB file or directory not found.";
    case -FR_WRITE_PROTECTED:return "USB drive is write protected.";
    case -FR_INVALID_NAME:return "Invalid filename; use CP437/ASCII names without reserved characters.";
    case -FR_DENIED:return "Access denied, directory full, or drive full.";
    default:return "USB I/O failed; check media. Safe eject is not confirmed.";
    }
}
int usb_file_list(const char *path,usb_file_info_t *out,unsigned max) {
    char actual[128];unsigned drive;
    int rc=path_for(path,actual,&drive);
    if (rc) return rc;
    DIR dir;FILINFO info;
    FRESULT result=f_opendir(&dir,actual);
    if (result) return -(int)result;
    unsigned n=0;
    while (n<max) {
        result=f_readdir(&dir,&info);
        if (result || !info.fname[0]) break;
        kstrncpy(out[n].name,info.fname,255);out[n].name[255]=0;
        out[n].size_overflow=info.fsize>UINT32_MAX;
        out[n].size=out[n].size_overflow?UINT32_MAX:(uint32_t)info.fsize;
        out[n].directory=!!(info.fattrib&AM_DIR);n++;
    }
    f_closedir(&dir);
    return result ? -(int)result : (int)n;
}
int usb_file_read(const char *path,uint8_t *buf,uint32_t max,uint32_t *length) {
    char actual[128];unsigned drive;
    int rc=path_for(path,actual,&drive);
    if (rc) return rc;
    FIL file;
    FRESULT result=f_open(&file,actual,FA_READ);
    if (result) return -(int)result;
    if (f_size(&file)>max) {f_close(&file);return -102;}
    UINT n=0;
    result=f_read(&file,buf,max,&n);
    FRESULT closed=f_close(&file);
    if (result || closed) return -(int)(result ? result : closed);
    *length=n;return 0;
}
int usb_file_read_at(const char *path,uint32_t offset,uint8_t *buf,
                     uint32_t length,uint32_t *file_size) {
    char actual[128];unsigned drive;
    int rc=path_for(path,actual,&drive);
    if (rc) return rc;
    FIL file;
    FRESULT result=f_open(&file,actual,FA_READ);
    if (result) return -(int)result;
    if(f_size(&file)>UINT32_MAX){f_close(&file);return -104;}
    uint32_t size=(uint32_t)f_size(&file);
    *file_size=size;
    if (offset>size || length>size-offset) {f_close(&file);return -102;}
    result=f_lseek(&file,offset);
    UINT n=0;
    if (!result) result=f_read(&file,buf,length,&n);
    FRESULT closed=f_close(&file);
    if (result || closed) return -(int)(result ? result : closed);
    return n==length ? 0 : -103;
}
int usb_file_write(const char *path,const uint8_t *buf,uint32_t length) {
    char actual[128];unsigned drive;
    int rc=path_for(path,actual,&drive);
    if (rc) return rc;
    if (length>USB_FILE_MAX) return -102;
    FIL file;
    FRESULT result=f_open(&file,actual,FA_WRITE|FA_CREATE_ALWAYS);
    if (result) {
        if (result==FR_DISK_ERR || result==FR_INT_ERR || result==FR_NOT_READY) {
            volumes[drive].mounted=0;
            volumes[drive].status="Write-open failed; eject status must be checked";
            char root[3];volume_name(drive,root);f_mount(0,root,0);
        }
        return -(int)result;
    }
    UINT n=0;
    result=f_write(&file,buf,length,&n);
    FRESULT closed=f_close(&file); /* flush data, FAT, directory, FSInfo, device */
    if (result || closed) {
        /* Do not reuse a FatFs dirty window after a failed write. */
        volumes[drive].mounted=0;
        volumes[drive].status="Write failed; eject status must be checked";
        char root[3];volume_name(drive,root);f_mount(0,root,0);
        return -(int)(result ? result : closed);
    }
    return n==length ? 0 : -103;
}
