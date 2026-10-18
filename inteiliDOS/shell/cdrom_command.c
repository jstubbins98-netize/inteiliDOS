/* CDROM: real ATAPI drive information, ISO9660 listings and ELF launch. */
#include "cdrom_command.h"
#include "launchpad.h"
#include "../kernel/cdrom.h"
#include "../kernel/iso9660.h"
#include "../kernel/vga.h"

static int option_is(const char *a, const char *b) {
    while (*a && *b) {
        char c=*a++;
        if (c>='a' && c<='z') c-=32;
        if (c!=*b++) return 0;
    }
    return !*a && !*b;
}

static void usage(void) {
    vga_puts("CDROM        Show command usage\n"
             "CDROM /DRIVE-INFO  Drive and inserted-disc information\n"
             "CDROM /EJECT  Open the first inserted disc's tray (else first drive)\n"
             "CDROM /LIST   List ISO9660 root contents on inserted discs\n"
             "CDROM /RUN    Browse and launch CD-ROM ELF programs in LaunchPad\n");
}

static int readable(const cdrom_drive_t *drive) {
    return drive->last_lba && drive->block_size==CDROM_SECTOR_SIZE;
}

static void drive_heading(unsigned i) {
    static const char *positions[]={
        "Primary master","Primary slave","Secondary master","Secondary slave"
    };
    if (i<4)
        vga_printf("\nCD-ROM drive %u - %s (IDE ATAPI)\n",i,positions[i]);
    else
        vga_printf("\nCD-ROM drive %u - SATA / AHCI optical (ATAPI)\n",i);
}

static void unavailable(void) {
    vga_puts("No readable data disc: tray empty, unsupported media, or read error.\n");
}

static void disc_info(unsigned i, const cdrom_drive_t *drive) {
    if (!readable(drive)) { unavailable(); return; }
    vga_printf("Last readable sector: %u; sector size: %u bytes\n",
               drive->last_lba,drive->block_size);
    if (drive->last_lba<0x7fffffffu)
        vga_printf("Disc capacity: %u KiB\n",(drive->last_lba+1)*2);
    uint8_t sector[CDROM_SECTOR_SIZE];
    if (cdrom_read_sector(i,16,sector)) {
        vga_puts("Disc information could not be read.\n");
        return;
    }
    if (sector[0]!=1 || sector[1]!='C' || sector[2]!='D' ||
        sector[3]!='0' || sector[4]!='0' || sector[5]!='1' || sector[6]!=1) {
        vga_puts("Filesystem: not recognized as ISO9660.\n");
        return;
    }
    char label[33];
    for (unsigned n=0;n<32;n++) {
        unsigned char c=sector[40+n];
        label[n]=(c>=32 && c<127) ? c : '?';
    }
    label[32]=0;
    for (int n=31;n>=0 && label[n]==' ';n--) label[n]=0;
    vga_printf("Filesystem: ISO9660; volume: %s\n",label[0] ? label : "(unnamed)");
}

int cdrom_command(int argc, const char *argv[]) {
    enum {INFO,EJECT,LIST,RUN} action=INFO;
    if (argc==0) { usage(); return 0; }
    if (argc>1) { usage(); return -1; }
    if (argc==1) {
        if (option_is(argv[0],"/DRIVE-INFO")) action=INFO;
        else if (option_is(argv[0],"/EJECT")) action=EJECT;
        else if (option_is(argv[0],"/LIST")) action=LIST;
        else if (option_is(argv[0],"/RUN")) action=RUN;
        else { vga_puts("Unknown CDROM option.\n"); usage(); return -1; }
    }
    /* Refresh media inserted after boot; never invent a cached disc listing. */
    cdrom_rescan_all();
    const cdrom_drive_t *drives=cdrom_drives();
    int first=-1,inserted=-1;
    for (unsigned i=0;i<CDROM_MAX_DRIVES;i++) {
        if (drives[i].present!=CDROM_PRESENT) continue;
        if (first<0) first=i;
        if (inserted<0 && readable(&drives[i])) inserted=i;
    }
    if (first<0) { vga_puts("No CD-ROM drive detected.\n"); return -1; }
    if (action==EJECT) {
        unsigned i=inserted>=0 ? inserted : first;
        drive_heading(i);
        if (cdrom_eject(i)) {
            vga_puts("Eject failed: drive rejected the command or cannot open its tray.\n");
            return -1;
        }
        cdrom_rescan_media(i);
        vga_puts("Eject command accepted.\n");
        return 0;
    }
    if (action==RUN) {
        if (inserted<0) { unavailable(); return -1; }
        launchpad_run_cdrom(inserted);
        return 0;
    }
    int listed=0;
    for (unsigned i=0;i<CDROM_MAX_DRIVES;i++) {
        if (drives[i].present!=CDROM_PRESENT) continue;
        drive_heading(i);
        if (action==INFO) { disc_info(i,&drives[i]); continue; }
        if (!readable(&drives[i])) { unavailable(); continue; }
        iso9660_dirent_t entries[ISO9660_MAX_FILES];
        int count=iso9660_read_dir(i,entries);
        if (count<0) {
            vga_puts("Cannot list disc: unreadable or not an ISO9660 data disc.\n");
            continue;
        }
        listed=1;
        vga_puts("Root directory (use CDROM /RUN to browse program folders):\n");
        for (int n=0;n<count;n++) {
            vga_printf("%s %s",entries[n].is_dir ? "<DIR>" : "     ",entries[n].name);
            if (!entries[n].is_dir) vga_printf(" (%u bytes)",entries[n].size);
            vga_putchar('\n');
        }
        if (!count) vga_puts("(empty directory)\n");
        if (count==ISO9660_MAX_FILES)
            vga_puts("Listing limit reached (64 entries); more entries may exist.\n");
    }
    return action==INFO || listed ? 0 : -1;
}
