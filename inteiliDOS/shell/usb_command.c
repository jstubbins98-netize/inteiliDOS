#include "usb_command.h"
#include "../kernel/usb_storage.h"
#include "../kernel/fs.h"
#include "../kernel/memory.h"
#include "../kernel/vga.h"

static uint8_t file_buffer[USB_FILE_MAX+1]; /* detect native read truncation */
static int option(const char *a,const char *b) {
    while (*a && *b) {
        char c=*a++;
        if (c>='a'&&c<='z') c-=32;
        char d=*b++;
        if (d>='a'&&d<='z') d-=32;
        if (c!=d) return 0;
    }
    return !*a && !*b;
}
static void usage(void) {
    vga_puts("USB          Show this usage\n"
             "USB /MOUNT   Detect and mount supported USB drives (read/write)\n"
             "USB /EJECT   Flush and safely disconnect all detected USB drives\n"
             "USB /COPY    Choose ONE file and its save destination (no bulk copy)\n"
             "Mounted drives: USB0:..USB3: (not C: or D:)\n"
             "Examples: DIR USB0:    TYPE USB0:\\NOTES.TXT\n"
             "          COPY USB0:\\NOTES.TXT USB1:\\NOTES.TXT\n"
             "iEdit/BASIC/Sheets: choose [3] USB in the save/open device dialog.\n"
             "Requires xHCI, direct root port, SCSI bulk-only USB storage.\n"
             "FAT12/16/32; 8.3 names. NTFS/exFAT, hubs, UHCI/OHCI/EHCI storage\n"
             "and UAS-only drives are not supported. USB keyboards stay active.\n");
}
static void statuses(void) {
    const usb_volume_info_t *volumes=usb_storage_volumes();
    int shown=0;
    for (unsigned i=0;i<USB_STORAGE_MAX;i++) {
        if (!volumes[i].status) continue;
        vga_printf("USB%u: %s\n",i,volumes[i].status);shown++;
    }
    if (!shown) vga_puts("No supported USB storage drives detected.\n");
}
int usb_command(int argc,const char *argv[]) {
    if (!argc || (argc==1 && option(argv[0],"/?"))) {usage();return 0;}
    if (argc==1 && option(argv[0],"/COPY")) return usb_pick_and_copy();
    if (argc!=1 || (!option(argv[0],"/MOUNT") && !option(argv[0],"/EJECT"))) {
        vga_puts("Unknown USB option or extra arguments.\n");usage();return -1;
    }
    if (option(argv[0],"/MOUNT")) {
        int result=usb_storage_mount_all();
        statuses();
        if (result<0) {
            vga_puts("USB mount unavailable: no usable xHCI controller, or drive/controller error.\n");
            return -1;
        }
        vga_printf("%u USB drive(s) mounted.\n",(unsigned)result);
        return 0;
    }
    int result=usb_storage_eject_all();
    statuses();
    if (result) vga_puts("Some drives could not be safely ejected. Do not remove them.\n");
    else vga_puts("USB storage eject complete. Keyboard devices were not ejected.\n");
    return result;
}
int usb_command_list(const char *path) {
    usb_file_info_t files[64];
    int n=usb_file_list(path,files,64);
    if (n<0) {vga_puts(usb_storage_error(n));vga_putchar('\n');return -1;}
    vga_printf(" Directory of %s\n",path);
    for (int i=0;i<n;i++) {
        if (files[i].directory) vga_printf(" <DIR> %s\n",files[i].name);
        else vga_printf(" %s  %u bytes\n",files[i].name,files[i].size);
    }
    if (!n) vga_puts("(empty directory)\n");
    if (n==64) vga_puts("Listing limit: 64 entries.\n");
    return 0;
}
int usb_command_type(const char *path) {
    uint32_t length=0;
    int result=usb_file_read(path,file_buffer,USB_FILE_MAX,&length);
    if (result) {vga_puts(usb_storage_error(result));vga_putchar('\n');return -1;}
    for (uint32_t i=0;i<length;i++) {
        uint8_t c=file_buffer[i];
        vga_putchar((c>=32 && c<127) || c=='\n' || c=='\r' || c=='\t' ? c : '.');
    }
    vga_putchar('\n');return 0;
}
static int native_path(const char *p) {
    return p && (p[0]=='C'||p[0]=='c'||p[0]=='D'||p[0]=='d') &&
           p[1]==':' && (p[2]=='\\'||p[2]=='/') && p[3] &&
           !kstrstr(p+3,"\\") && !kstrstr(p+3,"/");
}
int usb_command_copy(const char *src,const char *dst) {
    if ((!usb_path(src) && !native_path(src)) ||
        (!usb_path(dst) && !native_path(dst))) {
        vga_puts("Use explicit USBn:\\FILE.EXT or native C:\\NAME / D:\\NAME paths.\n");
        return -1;
    }
    if (option(src,dst)) {
        vga_puts("Source and destination must be different.\n");return -1;
    }
    uint32_t length=0;
    int result;
    if (usb_path(src)) result=usb_file_read(src,file_buffer,USB_FILE_MAX,&length);
    else result=fs_volume_read((src[0]=='D'||src[0]=='d') ? 1 : 0,
                              src+3,file_buffer,sizeof(file_buffer),&length);
    if (!result && length>USB_FILE_MAX) {
        vga_puts("File exceeds the 64 KiB copy limit; destination unchanged.\n");return -1;
    }
    if (result) {
        vga_puts(usb_path(src) ? usb_storage_error(result) : "Native file read failed.");
        vga_putchar('\n');return -1;
    }
    if (usb_path(dst)) result=usb_file_write(dst,file_buffer,length);
    else result=fs_volume_write((dst[0]=='D'||dst[0]=='d') ? 1 : 0,
                               dst+3,file_buffer,length);
    if (result) {
        vga_puts(usb_path(dst) ? usb_storage_error(result) : "Native file save failed.");
        vga_putchar('\n');return -1;
    }
    vga_printf("Copied %u bytes to %s.\n",length,dst);return 0;
}
