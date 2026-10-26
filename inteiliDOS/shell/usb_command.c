#include "usb_command.h"
#include "../kernel/usb_storage.h"
#include "../kernel/usb_xhci.h"
#include "../kernel/usb_ehci.h"
#include "../kernel/fs.h"
#include "../kernel/memory.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"

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
             "USB /SHOW-PORTS  List currently mounted USB drives and capacities\n"
             "USB /FORMAT [USBn: [FAT16|FAT32|EXFAT]]  Confirmed whole-drive quick format\n"
             "USB /COPY    Choose ONE file and its save destination (no bulk copy)\n"
             "Mounted drives: USB0:..USB3: (not C: or D:)\n"
             "Examples: DIR USB0:    TYPE USB0:\\NOTES.TXT\n"
             "          COPY USB0:\\NOTES.TXT USB1:\\NOTES.TXT\n"
             "iEdit/BASIC/Sheets: choose [3] USB in the save/open device dialog.\n"
             "Requires xHCI or EHCI (USB 2 high-speed), direct root port, BOT.\n"
             "FAT12/16/32 and exFAT; CP437 names. NTFS, hubs, UHCI/OHCI storage\n"
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
static int format_line(char *out,unsigned capacity) {
    unsigned n=0;int overflow=0;
    out[0]=0;
    for (;;) {
        int c=keyboard_getchar();
        if (c==KEY_ESCAPE) {vga_putchar('\n');return -1;}
        if (c=='\r' || c=='\n') {
            out[n]=0;vga_putchar('\n');return overflow?-1:(int)n;
        }
        if (c==KEY_BACKSPACE && n) {out[--n]=0;vga_putchar('\b');}
        else if (c>=32 && c<127) {
            if (n+1<capacity) {out[n++]=(char)c;out[n]=0;vga_putchar(c);}
            else overflow=1;
        }
    }
}
static unsigned format_filesystem(const char *name) {
    if (option(name,"FAT16") || option(name,"1")) return USB_FORMAT_FAT16;
    if (option(name,"FAT32") || option(name,"2")) return USB_FORMAT_FAT32;
    if (option(name,"EXFAT") || option(name,"3")) return USB_FORMAT_EXFAT;
    return 0;
}
static int format_command(int argc,const char *argv[]) {
    if (argc>3) {vga_puts("Use USB /FORMAT [USBn: [FAT16|FAT32|EXFAT]].\n");return -1;}
    const usb_volume_info_t *v=usb_storage_volumes();
    unsigned count=0;
    vga_puts("Mounted USB drives available to format:\n");
    for (unsigned i=0;i<USB_STORAGE_MAX;i++) if (v[i].mounted) {
        vga_printf(" USB%u: %u MiB - %s\n",i,v[i].sectors/2048,v[i].status);count++;
    }
    if (!count) {vga_puts("No USB drive mounted. Run USB /MOUNT first.\n");return -1;}
    char answer[64];
    const char *target=argc>=2?argv[1]:answer;
    if (argc<2) {
        vga_puts("Select USB0:..USB3: (Enter/Esc cancels): ");
        if (format_line(answer,sizeof(answer))<=0) goto cancelled;
    }
    if (kstrlen(target)!=5 || !usb_path(target) || target[3]<'0' ||
        target[3]>='0'+USB_STORAGE_MAX || target[4]!=':') {
        vga_puts("Select an explicit mounted USB0:..USB3: drive, not a native disk or file.\n");
        return -1;
    }
    unsigned drive=(unsigned)(target[3]-'0');
    if (!v[drive].mounted) {vga_puts("That USB drive is not mounted.\n");return -1;}
    unsigned filesystem;
    if (argc>=3) filesystem=format_filesystem(argv[2]);
    else {
        vga_puts("Filesystem: 1 FAT16 (8 MiB..2 GiB), 2 FAT32 (>=64 MiB), 3 exFAT (>=8 MiB).\n"
                 "Enter name or number (Enter/Esc cancels): ");
        if (format_line(answer,sizeof(answer))<=0) goto cancelled;
        filesystem=format_filesystem(answer);
    }
    if (!filesystem) {vga_puts("Unsupported filesystem. Choose FAT16, FAT32 or EXFAT. No changes made.\n");return -1;}
    usb_format_plan_t plan;
    int rc=usb_storage_prepare_format(drive,filesystem,&plan);
    if (rc) {vga_puts(usb_storage_error(rc));vga_putchar('\n');return -1;}
    const char *name=filesystem==USB_FORMAT_FAT16?"FAT16":
                     filesystem==USB_FORMAT_FAT32?"FAT32":"exFAT";
    vga_printf("WARNING: Erase ALL files and ALL partitions on USB%u: (%u MiB), creating %s.\n",
               drive,plan.sectors/2048,name);
    vga_puts("This quick format is not a secure erase. Back up anything you need first.\n");
    char confirmation[]="FORMAT USB0:";
    confirmation[10]=(char)('0'+drive);
    vga_printf("Type exactly %s to erase this drive (anything else cancels): ",confirmation);
    if (format_line(answer,sizeof(answer))<=0 || kstrcmp(answer,confirmation)) goto cancelled;
    vga_printf("Formatting USB%u: as %s. Do not remove it; cancellation is no longer available.\n",drive,name);
    rc=usb_storage_format(&plan);
    if (rc) {
        vga_puts(usb_storage_error(rc));vga_putchar('\n');
        vga_printf("USB%u: %s\n",drive,v[drive].status?v[drive].status:"Unavailable");
        return -1;
    }
    vga_printf("USB%u: formatted as %s and remounted read/write. Other drives were not formatted.\n",
               drive,name);
    return 0;
cancelled:
    vga_puts("USB format cancelled. No formatting writes made.\n");return 0;
}
int usb_command(int argc,const char *argv[]) {
    if (!argc || (argc==1 && option(argv[0],"/?"))) {usage();return 0;}
    if (argc==1 && option(argv[0],"/COPY")) return usb_pick_and_copy();
    if (option(argv[0],"/FORMAT")) return format_command(argc,argv);
    if (argc==1 && option(argv[0],"/SHOW-PORTS")) {
        const usb_volume_info_t *v=usb_storage_volumes();
        unsigned count=0;
        vga_puts("Currently mounted USB drives (not a physical socket map):\n");
        for(unsigned i=0;i<USB_STORAGE_MAX;i++)if(v[i].mounted){
            vga_printf(" USB%u:  %u KiB  %s\n",i,v[i].sectors/2,v[i].status);
            count++;
        }
        if(!count)vga_puts("No USB drives mounted. Run USB /MOUNT first.\n");
        else vga_printf("%u mounted USB drive(s).\n",count);
        return 0;
    }
    if (argc!=1 || (!option(argv[0],"/MOUNT") && !option(argv[0],"/EJECT"))) {
        vga_puts("Unknown USB option or extra arguments.\n");usage();return -1;
    }
    if (option(argv[0],"/MOUNT")) {
        int result=usb_storage_mount_all();
        statuses();
        const usb_xhci_info_t *controller=usb_xhci_info();
        vga_printf("USB controller: %s\n",controller->status);
        if (controller->found) {
            vga_printf("PCI %02X:%02X.%u ID %04X:%04X; connected ports: %u\n",
                       controller->bus,controller->device,controller->function,
                       controller->vendor_id,controller->device_id,
                       controller->connected_ports);
            vga_printf("Failed ports: %u; Intel port routing: %s\n",
                       controller->failed_ports,controller->intel_routing ? "enabled" : "not applied");
            if (controller->legacy_present) {
                vga_printf("xHCI ownership: %08X; legacy control: %08X\n",
                           controller->legacy_support,controller->legacy_control);
                if (controller->bios_handoff_fallback)
                    vga_puts("BIOS handoff: Intel ownership fallback applied.\n");
            }
            if (controller->event_type)
                vga_printf("Last failed event: %u; completion code: %u (0 = timeout)\n",
                           controller->event_type,controller->completion_code);
        }
        const usb_ehci_info_t *ehci=usb_ehci_info();
        vga_printf("EHCI controller: %s\n",ehci->status);
        if (ehci->found)
            vga_printf("EHCI PCI %02X:%02X.%u ID %04X:%04X; connected ports: %u\n"
                       "EHCI failed ports: %u\n",ehci->bus,ehci->device,ehci->function,
                       ehci->vendor_id,ehci->device_id,ehci->connected_ports,ehci->failed_ports);
        if (result<0) {
            vga_puts("USB mount unavailable; controller details shown above.\n");
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
    static usb_file_info_t files[64];
    int n=usb_file_list(path,files,64);
    if (n<0) {vga_puts(usb_storage_error(n));vga_putchar('\n');return -1;}
    vga_printf(" Directory of %s\n",path);
    for (int i=0;i<n;i++) {
        if (files[i].directory) vga_printf(" <DIR> %s\n",files[i].name);
        else if(files[i].size_overflow)vga_printf(" %s  >4 GiB (streaming unsupported)\n",files[i].name);
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
