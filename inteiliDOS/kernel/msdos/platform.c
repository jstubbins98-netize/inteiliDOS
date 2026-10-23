/* Native platform adapter for the software-only 8086 PC.
 * No guest port accesses reach host hardware. Only bounded speaker tones,
 * text output, keyboard events and private-file disk sectors are exported. */
#include "msdos.h"
#include "sound.h"
#include "mouse.h"
#include "startup.h"
#include "cdrom.h"
#include "../pcm.h"
#include "mbr.h"
#include "../fs.h"
#include "../memory.h"
#include "../timer.h"
#include "../keyboard.h"
#include "keyboard_input.h"
#include "../vga.h"
#include "../iso9660.h"
#include "../fdc.h"
#include "../cdrom.h"
#include "../ata.h"
#include "../usb_storage.h"
#ifndef CONFIG_MSDOS_SOUND
#define CONFIG_MSDOS_SOUND 0
#endif
#ifndef CONFIG_MSDOS_SOUND_MODEL
#define CONFIG_MSDOS_SOUND_MODEL 2
#endif

#define DISK_SECTORS (520u * 63u)
#define FLOPPY_BYTES 1474560u
#define IMAGE_NAME "MSDOS622.HDD"
#define SEED_CANCELLED (-100)
#define SEED_NOMEM     (-101)
#define SCREEN ((volatile uint16_t *)0xb8000)
static fs_image_t hard;
static uint32_t floppy_size;
static iso9660_dirent_t floppy_entry;
static uint8_t media_cache[2048];
static uint32_t media_cached_lba;
static int disc = -1, media_index, physical, pending = -1, io_failed;
static int delivered_keyboard_state = -1;
static int auto_media, physical_ready;
static int media_changed;
static int usb_drive = -1;
static char usb_image[128];
#define USB_GAME_DISKS 8
static char usb_game_paths[USB_GAME_DISKS][128];
static unsigned usb_game_count, usb_game_index;
static uint16_t screen[2000], saved[2000];
static int row, col, save_row, save_col, region_top, region_bottom;
static uint8_t attr, ansi, bright;
static unsigned parameters[8], parameter;
static uint32_t last_refresh, started;
static const char *media_names[] = {"D6221.IMG", "D6222.IMG", "D6223.IMG", "D622S.IMG"};
static const char *media_aliases[] = {"DISK1.IMG", "DISK2.IMG", "DISK3.IMG", "SUPPDISK.IMG"};

static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p+2) << 16); }
static void w16(uint8_t *p, uint16_t n) { p[0]=n; p[1]=n>>8; }
static void w32(uint8_t *p, uint32_t n) { w16(p,n); w16(p+2,n>>16); }
static void blank(int first, int end) {
    for (int i=first; i<end; ++i) screen[i]=((uint16_t)attr<<8)|' ';
}
static void scroll_up(int top, int bottom, int count) {
    if (top<0 || bottom>24 || top>bottom) return;
    if (count>bottom-top+1) count=bottom-top+1;
    kmemcpy(screen+top*80, screen+(top+count)*80,
            (bottom-top+1-count)*160);
    blank((bottom-count+1)*80,(bottom+1)*80);
}
static void newline(void) {
    if (row==region_bottom) scroll_up(region_top,region_bottom,1);
    else if (row<24) ++row;
}
static unsigned param(unsigned i, unsigned fallback) {
    return i<=parameter && parameters[i] ? parameters[i] : fallback;
}
static void ansi_finish(uint8_t c) {
    static const uint8_t colors[8]={0,4,2,6,1,5,3,7};
    unsigned n=param(0,1);
    switch (c) {
    case 'H': case 'f': row=(int)param(0,1)-1; col=(int)param(1,1)-1; break;
    case 'A': row-=n; break;
    case 'B': row+=n; break;
    case 'C': col+=n; break;
    case 'D': col-=n; break;
    case 'G': col=n-1; break;
    case 'J':
        if (parameters[0]==2 || parameters[0]==3) blank(0,2000);
        else if (parameters[0]==1) blank(0,row*80+col+1);
        else blank(row*80+col,2000);
        break;
    case 'K':
        if (parameters[0]==2) blank(row*80,(row+1)*80);
        else if (parameters[0]==1) blank(row*80,row*80+col+1);
        else blank(row*80+col,(row+1)*80);
        break;
    case 'M': scroll_up(row,region_bottom,n); break;
    case 'L':
        if ((int)n>region_bottom-row+1) n=region_bottom-row+1;
        for (int i=(region_bottom+1)*80-1; i>=(row+(int)n)*80; --i)
            screen[i]=screen[i-n*80];
        blank(row*80,(row+n)*80);
        break;
    case 'r':
        region_top=(int)param(0,1)-1; region_bottom=(int)param(1,25)-1;
        if (region_top<0 || region_bottom>24 || region_top>=region_bottom) {
            region_top=0; region_bottom=24;
        }
        break;
    case 's': save_row=row; save_col=col; break;
    case 'u': row=save_row; col=save_col; break;
    case 'm':
        for (unsigned i=0;i<=parameter;i++) {
            n=parameters[i];
            if (!n) {attr=7; bright=0;}
            else if (n==1) {bright=8; attr|=8;}
            else if (n==22) {bright=0; attr&=0xf7;}
            else if (n>=30 && n<=37) attr=(attr&0xf0)|colors[n-30]|bright;
            else if (n>=40 && n<=47) attr=(attr&15)|(colors[n-40]<<4);
            else if (n==39) attr=(attr&0xf0)|7|bright;
            else if (n==49) attr&=15;
        }
        break;
    }
    if (row<0) row=0;
    if (row>24) row=24;
    if (col<0) col=0;
    if (col>79) col=79;
}
void msdos_pc_putchar(uint8_t c) {
    if (ansi==1) {
        ansi=c=='[' ? 2 : 0;
        kmemset(parameters,0,sizeof(parameters)); parameter=0;
        return;
    }
    if (ansi==2) {
        if (c>='0' && c<='9') {
            if (parameters[parameter]<1000)
                parameters[parameter]=parameters[parameter]*10+c-'0';
        } else if (c==';') { if (parameter<7) ++parameter; }
        else if (c!='?') { ansi_finish(c); ansi=0; }
        return;
    }
    if (c==27) {ansi=1;return;}
    if (c==13) {col=0;return;}
    if (c==10) {newline();return;}
    if (c==8) {if (col) --col;return;}
    if (c==7) return;
    if (c==9) {do {msdos_pc_putchar(' ');} while (col%8);return;}
    screen[row*80+col]=((uint16_t)attr<<8)|c;
    if (++col==80) {col=0;newline();}
}
/* The BIOS expects nine 32-bit struct-tm fields followed by milliseconds.
 * Seed from CMOS once; subsequent time comes from the host monotonic timer.
 * CMOS reads do not modify RTC configuration or interrupt masks. */
static uint32_t rtc_base;
static unsigned rtc_year, rtc_month, rtc_day;
static uint8_t cmos(uint8_t reg) {
#ifdef MSDOS_TEST_CMOS
    return MSDOS_TEST_CMOS(reg);
#else
    uint8_t previous, value;
    __asm__ volatile("inb $0x70,%0":"=a"(previous));
    __asm__ volatile("outb %0,$0x70"::"a"((uint8_t)((previous&0x80)|reg)));
    __asm__ volatile("inb $0x71,%0":"=a"(value));
    __asm__ volatile("outb %0,$0x70"::"a"(previous));
    return value;
#endif
}
static unsigned rtc_value(uint8_t reg, int binary) {
    unsigned x=cmos(reg);
    return binary ? x : (x>>4)*10+(x&15);
}
static void rtc_init(void) {
    int binary=!!(cmos(11)&4);
    rtc_base=rtc_value(0,binary)+60*rtc_value(2,binary);
    unsigned hour=cmos(4), pm=hour&128;
    hour&=127; if (!binary) hour=(hour>>4)*10+(hour&15);
    if (!(cmos(11)&2)) hour=(hour%12)+(pm?12:0);
    rtc_base+=3600*hour;
    unsigned yy=rtc_value(9,binary), century=rtc_value(0x32,binary);
    rtc_year=(century==19 || century==20 ? century*100 : yy>=80?1900:2000)+yy;
    rtc_month=rtc_value(8,binary); rtc_day=rtc_value(7,binary);
    if (!rtc_month || rtc_month>12) rtc_month=1;
    if (!rtc_day || rtc_day>31) rtc_day=1;
    started=timer_get_ticks();
}
void msdos_pc_rtc(uint8_t *p) {
    uint32_t ticks=timer_get_ticks()-started, seconds=rtc_base+ticks/1000;
    unsigned year=rtc_year, month=rtc_month, day=rtc_day+seconds/86400;
    static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for (;;) {
        unsigned d=days[month-1]+(month==2 && !(year%4) && (year%100 || !(year%400)));
        if (day<=d) break;
        day-=d; if (++month==13) {month=1;++year;}
    }
    kmemset(p,0,38);
    w32(p,seconds%60); w32(p+4,(seconds/60)%60); w32(p+8,(seconds/3600)%24);
    w32(p+12,day); w32(p+16,month-1); w32(p+20,year-1900);
    unsigned yday=day-1;
    for (unsigned m=1;m<month;m++)
        yday+=days[m-1]+(m==2 && !(year%4) && (year%100 || !(year%400)));
    unsigned previous_year=year-1;
    w32(p+24,(1+365*previous_year+previous_year/4-previous_year/100+
              previous_year/400+yday)%7);
    w32(p+28,yday);
    w16(p+36,ticks%1000);
}

static int find_image_cd(const char *name) {
    disc=-1;
    const cdrom_drive_t *drives=cdrom_drives();
    for (unsigned d=0;d<CDROM_MAX_DRIVES;d++) {
        if (!drives[d].present) continue;
        if (!cdrom_rescan_media(d)) continue; /* empty tray */
        iso9660_dirent_t e;
        if (iso9660_find_file(d,name,&e)==1 && !e.is_dir) {
            disc=(int)d;
            return 0;
        }
    }
    return -1;
}

static int find_install_cd(void) {
    return !find_image_cd(media_names[0]) ? 0 : find_image_cd(media_aliases[0]);
}

static int load_media(int index, const char *name) {
    if (disc<0) return -1;
    iso9660_dirent_t e;
    int found=iso9660_find_file(disc,name,&e);
    if (!found && index>=0 && index<4)
        found=iso9660_find_file(disc,media_aliases[index],&e);
    if (found!=1 || e.is_dir ||
        (e.size!=1474560 && e.size!=737280 && e.size!=368640 &&
         e.size!=1228800)) return -1;
    uint8_t boot[512];
    if (iso9660_read_file(disc,&e,boot,512)!=512 || boot[510]!=0x55 ||
        boot[511]!=0xaa || u16(boot+11)!=512) return -1;
    floppy_entry=e; floppy_size=e.size; media_cached_lba=0xffffffff;
    media_index=index; physical=0;
    media_changed=1;
    return 0;
}

/* Only a small read cache lives in RAM. Never copy an entire floppy image
 * into the 2 MB native heap, and never write to user-supplied USB media. */
static int load_usb_media(int index) {
    uint8_t boot[512];
    uint32_t size=0;
    char path[32];
    int rc=-4;
    for (unsigned alias=0;alias<2;alias++) {
        kstrcpy(path,"USB0:\\");path[3]='0'+usb_drive;
        kstrcpy(path+6,alias ? media_aliases[index] : media_names[index]);
        rc=usb_file_read_at(path,0,boot,sizeof(boot),&size);
        if (rc!=-4 && rc!=-5) break; /* only missing files permit an alias */
    }
    if (rc) return rc;
    if (size!=FLOPPY_BYTES || boot[510]!=0x55 || boot[511]!=0xaa ||
        u16(boot+11)!=512 || boot[13]!=1 || u16(boot+14)!=1 ||
        boot[16]!=2 || u16(boot+17)!=224 || u16(boot+19)!=2880 ||
        u16(boot+22)!=9 || u16(boot+24)!=18 || u16(boot+26)!=2) {
        vga_printf("MS-DOS: %s is not a valid 1.44 MB FAT12 DOS image.\n",path);
        return -104;
    }
    kstrcpy(usb_image,path);
    floppy_size=size;media_index=index;physical=0;
    media_cached_lba=0xffffffff;media_changed=1;
    vga_printf("MS-DOS: loaded %s as read-only A:.\n",path);
    return 0;
}

static int select_usb_media(int selected) {
    const usb_volume_info_t *volumes=usb_storage_volumes();
    int mounted=0;
    for (int d=0;d<USB_STORAGE_MAX;d++) {
        if ((selected>=0 && d!=selected) || !volumes[d].mounted) continue;
        mounted=1;usb_drive=d;
        int rc=load_usb_media(0);
        if (!rc) return 0;
        if (rc!=-4 && rc!=-5) {
            if (rc!=-104) vga_printf("MS-DOS: USB%d: %s\n",d,usb_storage_error(rc));
            return -1;
        }
    }
    usb_drive=-1;
    vga_puts(mounted ?
        "MS-DOS: USB Disk 1 missing; put D6221.IMG or DISK1.IMG in the drive root.\n" :
        "MS-DOS: USB drive not mounted; run USB /MOUNT first.\n");
    return -1;
}

/* Game media need not contain DOS system files. Match the supported floppy
 * geometries, and stream sectors through the same bounded USB cache. */
static int load_usb_game(const char *path) {
    uint8_t boot[512];
    uint32_t size=0;
    if (kstrlen(path)>=sizeof(usb_image)) {
        vga_puts("MS-DOS: USB image path is too long.\n");return -1;
    }
    int rc=usb_file_read_at(path,0,boot,sizeof(boot),&size);
    if (rc) {
        vga_printf("MS-DOS: cannot read USB game image: %s\n",usb_storage_error(rc));
        return -1;
    }
    unsigned total=u16(boot+19),track=u16(boot+24);
    if (boot[510]!=0x55 || boot[511]!=0xaa || u16(boot+11)!=512 ||
        u16(boot+26)!=2 || size!=total*512u ||
        !((track==9 && (total==720 || total==1440)) ||
          (track==15 && total==2400) || (track==18 && total==2880))) {
        vga_puts("MS-DOS: invalid USB floppy image; use 360K, 720K, 1.2M or 1.44M FAT media.\n");
        return -1;
    }
    kstrcpy(usb_image,path);
    usb_drive=path[3]-'0';floppy_size=size;media_index=-1;physical=0;
    media_cached_lba=0xffffffff;media_changed=1;
    vga_printf("MS-DOS: loaded %s as read-only A:.\n",path);
    return 0;
}

static int ensure_physical_floppy(void) {
    /* Unlike LaunchPad, entering MS-DOS need not have initialized the FDC.
     * BIOS leftovers are not a usable controller contract (notably in 86Box).
     * Initialize lazily so a saved DOS HDD still boots with no floppy drive. */
    if (!physical_ready) {
        if (fdc_init()) return -1;
        physical_ready=1;
    }
    return 0;
}
static int load_install_floppy(void) {
    uint8_t boot[512];
    if (ensure_physical_floppy()) {
        vga_puts("MS-DOS: cannot initialize the floppy controller for A:.\n");
        return -1;
    }
    int rc=fdc_read_sector(0,0,boot);
    if (rc) {
        const char *reason=rc==FDC_ERR_TIMEOUT ? "controller/drive timeout" :
                           rc==FDC_ERR_CRC ? "CRC error" :
                           rc==FDC_ERR_DMA ? "incomplete DMA transfer" :
                           rc==FDC_ERR_FORMAT ? "unsupported or corrupt floppy geometry" :
                           rc==FDC_ERR_ABNORMAL ? "no readable sector or disk" :
                           "seek or controller error";
        vga_printf("MS-DOS: cannot read A: boot sector: %s (code %d).\n",reason,rc);
        fdc_read_status_t status;
        fdc_last_read_status(&status);
        vga_printf("FDC status: ST0=%x ST1=%x ST2=%x DMA count=%x.\n",
                   (unsigned)status.st0,(unsigned)status.st1,
                   (unsigned)status.st2,(unsigned)status.dma_count);
        return -1;
    }
    if (boot[510]!=0x55 || boot[511]!=0xaa) {
        vga_puts("MS-DOS: A: has no valid boot signature; mount DOS Disk 1 directly.\n");
        vga_printf("Read signature bytes: %x %x; sector size=%u, sectors=%u.\n",
                   (unsigned)boot[510],(unsigned)boot[511],
                   (unsigned)u16(boot+11),(unsigned)u16(boot+19));
        return -1;
    }
    if (u16(boot+11)!=512 || boot[13]!=1 || u16(boot+14)!=1 || boot[16]!=2 ||
        u16(boot+17)!=224 || u16(boot+19)!=2880 || u16(boot+22)!=9 ||
        u16(boot+24)!=18 || u16(boot+26)!=2) {
        vga_puts("MS-DOS: A: is not a supported 1.44 MB DOS floppy format.\n");
        return -1;
    }
    physical=1;floppy_size=FLOPPY_BYTES;media_index=0;media_changed=1;
    return 0;
}
int msdos_media_changed(void) {
    int changed=media_changed;media_changed=0;return changed;
}
static int read_floppy(uint32_t offset, uint8_t *data, uint32_t length) {
    /* Existing DOS C: does not depend on the installation CD. Mount optional
     * A: media only if the guest actually requests a floppy read. */
    if (auto_media) {
        auto_media=0;
        if (!find_install_cd()) load_media(0,media_names[0]);
    }
    if (physical) {
        if (offset>=FLOPPY_BYTES || length>FLOPPY_BYTES-offset) return -1;
        if (ensure_physical_floppy()) return -1;
        uint8_t sector[512];
        while (length) {
            uint32_t in=offset%512, count=512-in;
            if (count>length) count=length;
            if (fdc_read_sector(0,offset/512,sector)) return -1;
            kmemcpy(data,sector+in,count);
            data+=count;offset+=count;length-=count;
        }
        return 0;
    }
    if (!floppy_size || offset>=floppy_size || length>floppy_size-offset) return -1;
    if (usb_image[0]) {
        while (length) {
            uint32_t block=offset/2048, in=offset%2048;
            if (media_cached_lba!=block) {
                uint32_t size=0, bytes=floppy_size-block*2048;
                if (bytes>sizeof(media_cache)) bytes=sizeof(media_cache);
                if (usb_file_read_at(usb_image,block*2048,media_cache,bytes,&size) ||
                    size!=floppy_size) {media_cached_lba=0xffffffff;return -1;}
                media_cached_lba=block;
            }
            uint32_t count=2048-in;
            if (count>length) count=length;
            kmemcpy(data,media_cache+in,count);
            length-=count;data+=count;offset+=count;
        }
        return 0;
    }
    while (length) {
        uint32_t lba=floppy_entry.lba+offset/2048, in=offset%2048;
        if (media_cached_lba!=lba) {
            if (cdrom_read_sector(disc,lba,media_cache)) return -1;
            media_cached_lba=lba;
        }
        uint32_t count=2048-in;
        if (count>length) count=length;
        kmemcpy(data,media_cache+in,count);
        length-=count;data+=count;offset+=count;
    }
    return 0;
}
/* Accurate geometry matters for DOS 2-era FAT12 media as well as disk swaps. */
int msdos_pc_floppy_params(unsigned *sectors, unsigned *spt) {
    uint8_t boot[512];
    if (read_floppy(0,boot,sizeof(boot))) return 0;
    unsigned total=u16(boot+19),track=u16(boot+24);
    if (u16(boot+11)!=512 || u16(boot+26)!=2 ||
        !((track==9 && (total==720 || total==1440)) ||
          (track==15 && total==2400) || (track==18 && total==2880))) return 0;
    *sectors=total;*spt=track;
    return 1;
}
int msdos_pc_disk(int drive, uint32_t offset, uint8_t *data,
                  uint32_t length, int write) {
    if (!length || length%512 || offset%512) return 0;
    if (drive==2) {
        if (write) return 0;
        for (uint32_t i=0;i<length;i+=512)
            if (msdos_cdrom_read((offset+i)/512,data+i)) return 0;
        return 1;
    }
    if (drive==1) {
        if (write) return 0; /* supplied images and physical floppy read-only */
        return !read_floppy(offset,data,length);
    }
    if (drive!=0 || offset/512>=hard.sectors ||
        length/512>hard.sectors-offset/512) return 0;
    for (uint32_t i=0;i<length;i+=512) {
        if (fs_image_sector(&hard,(offset+i)/512,data+i,write)) {
            io_failed=1; return 0;
        }
    }
    return 1;
}
/* Deliver one key with its original modifier snapshot. An idle resync also
 * repairs a missed modifier release if the existing pending slot was busy,
 * and initializes a new guest while a modifier is already physically held. */
int msdos_pc_key(void) {
    int k=pending;pending=-1;
    if (k<0) {
        keyboard_event_t state;
        keyboard_current_event(&state);
        unsigned current=msdos_keyboard_state(&state);
        if ((int)current==delivered_keyboard_state) return -1;
        k=(int)(current | MSDOS_KEY_STATE_ONLY);
    }
    delivered_keyboard_state=k & MSDOS_KEY_STATE_MASK;
    return k;
}
int msdos_pc_poll(const uint8_t *ram, const uint8_t *ports) {
    msdos_mouse_poll(ram[0x449]);
    keyboard_event_t event;
    int key=keyboard_poll_event(&event) ? event.character : -1;
    if (key==KEY_F8 || io_failed) return 1;
    if (key==KEY_F1+5) {
        auto_media=0;
        if (usb_image[0] && media_index<0) {
            if (usb_game_count>1) {
                unsigned next=(usb_game_index+1)%usb_game_count;
                if (!load_usb_game(usb_game_paths[next])) usb_game_index=next;
                else vga_puts("MS-DOS: USB disk swap failed; previous disk retained.\n");
            } else {
                vga_puts("MS-DOS: one USB image selected; list more paths after /USB to swap with F6.\n");
            }
        } else if (usb_game_count>1) {
            /* Resume the chosen USB set after temporarily selecting F7. */
            unsigned next=(usb_game_index+1)%usb_game_count;
            if (!load_usb_game(usb_game_paths[next])) usb_game_index=next;
            else vga_puts("MS-DOS: USB disk swap failed; previous disk retained.\n");
        } else {
        int next=(floppy_size || physical) ? (media_index+1)%4 : 0;
        if (usb_drive>=0 ? load_usb_media(next) :
            ((disc<0 && find_install_cd()) || load_media(next,media_names[next]))) {
            vga_puts("MS-DOS: could not load next installation disk.\n");
            /* Optional A: media failure must not stop the saved DOS C:. */
        }
        }
    } else if (key==KEY_F1+6) {
        auto_media=physical_ready=0;physical=1;media_changed=1;pending=-1;
    }
    else if (key==KEY_F12 && msdos_mouse_enabled() &&
             !(event.modifiers&(KEY_MOD_SHIFT|KEY_MOD_CTRL|KEY_MOD_ALT))) {
        msdos_mouse_toggle_cursor();
    }
    else if (key>0 && pending<0) {
        pending=msdos_keyboard_encode(&event);
    }
    uint32_t now=timer_get_ticks();
    msdos_devices_audio_poll();
    if (now-last_refresh>=16) {
        msdos_devices_update(ram,ports);
        if (!msdos_devices_graphics()) {
            for (int i=0;i<2000;i++) SCREEN[i]=msdos_mouse_text(i,screen[i]);
            vga_set_cursor(row,col);
        }
        last_refresh=now;
    }
    return 0;
}
static int put_sector(uint32_t n, uint8_t *buf) {
    return fs_image_sector(&hard,n,buf,1);
}
static int disk_failure(int rc) {
    if (rc==FS_ERR_FULL)
        vga_puts("MS-DOS: native file directory is full (15 entries).\n"
                 "A free directory entry is needed for MSDOS622.HDD.\n");
    else if (rc==SEED_CANCELLED)
        vga_puts("MS-DOS: DOS HDD creation cancelled with F8.\n");
    else if (rc==SEED_NOMEM)
        vga_puts("MS-DOS: not enough memory to create the DOS HDD.\n");
    else if (rc==FS_ERR_IO)
        vga_puts("MS-DOS: disk I/O failure; cannot read or write the DOS HDD/media.\n");
    else
        vga_puts("MS-DOS needs safe native storage.\n"
                 "Older installs need backup and approved migration first.\n");
    vga_puts("Existing native files kept; no physical disk was formatted.\n");
    return 1;
}
/* Refuse before reading installation media or initializing any image sectors.
 * reserve revalidates the same layout/directory; the query is read-only. */
static int reserve_disk(uint8_t drive) {
    fs_image_space_t space;
    int rc=fs_image_space(drive,&space);
    if (rc) return disk_failure(rc);
    vga_printf("DOS HDD requires %u KiB; native C: has %u KiB safely available.\n",
               DISK_SECTORS/2,space.available_sectors/2);
    if (!space.free_slots) return disk_failure(FS_ERR_FULL);
    if (space.available_sectors<DISK_SECTORS) {
        vga_puts("MS-DOS: insufficient disk space; DOS HDD creation not started.\n"
                 "Use a 32 MiB or larger native disk with the required free space.\n"
                 "Keep existing files; do not erase this disk to make room.\n");
        return 1;
    }
    rc=fs_image_reserve(drive,DISK_SECTORS,&hard);
    if (rc==FS_ERR_NOSPC) {
        vga_puts("MS-DOS: insufficient disk space (available space changed).\n"
                 "DOS HDD creation not started; existing native files kept.\n"
                 "Use a 32 MiB or larger native disk with the required free space.\n");
        return 1;
    }
    return rc ? disk_failure(rc) : 0;
}
/* Extract FAT12 files directly from the user's boot disk into the private
 * FAT16 volume. IO.SYS is first, contiguous, and starts at cluster two. */
static int seed_file(const uint8_t *e) {
    return e[0] && e[0]!=0xe5 && !(e[11]&0x18) && e[11]!=15 && u32(e+28) &&
           kmemcmp(e,"AUTOEXECBAT",11) && kmemcmp(e,"CONFIG  SYS",11) &&
           kmemcmp(e,"SETUP   ",8) && kmemcmp(e,"DRVSPACEBIN",11);
}
static void seed_progress(int row,const char *phase,uint32_t done,uint32_t total) {
    vga_set_cursor(row,0);
    vga_printf("DOS HDD %s: %u%% (%u/%u KiB)  F8 cancels          ",
               phase,total ? done*100/total : 100,done/1024,total/1024);
}
static int seed_disk(void) {
    uint8_t sector[512];
    uint8_t *fat=kcalloc(1,32*512), *root=kcalloc(1,32*512);
    uint8_t *source_root=kmalloc(224*32), *source_fat=kmalloc(9*512);
    if (!fat || !root || !source_root || !source_fat) {
        kfree(fat);kfree(root);kfree(source_root);kfree(source_fat);return SEED_NOMEM;
    }
    int rc=FS_ERR_IO;
    const char *phase="read system disk";
    uint32_t progress=0;
    int progress_row=0, progress_col=0, progress_active=0;
    uint32_t copy_total=0, copy_done=0, shown_percent=0;
    if (read_floppy(19*512,source_root,224*32) ||
        read_floppy(512,source_fat,9*512) ||
        kmemcmp(source_root,"IO      SYS",11) ||
        kmemcmp(source_root+32,"MSDOS   SYS",11)) goto done;
    for (unsigned i=0;i<224 && source_root[i*32];i++) {
        const uint8_t *e=source_root+i*32;
        if (!seed_file(e)) continue;
        uint32_t size=u32(e+28);
        if (size>FLOPPY_BYTES-copy_total) goto done;
        copy_total+=size;
    }
    kmemset(sector,0,512);
    vga_puts("Creating private DOS disk (native files are kept; F8 cancels)...\n");
    vga_get_cursor(&progress_row,&progress_col);
    progress_active=1;
    seed_progress(progress_row,"clear",0,DISK_SECTORS*512);
    phase="clear private disk";
    for (uint32_t n=0;n<DISK_SECTORS;n+=128) {
        progress=n;
        uint32_t count=DISK_SECTORS-n;
        if (count>128) count=128;
        if (keyboard_poll()==KEY_F8) {rc=SEED_CANCELLED;goto done;}
        if (fs_image_zero(&hard,n,count)) goto done;
        uint32_t percent=(n+count)*100/DISK_SECTORS;
        if (percent!=shown_percent) {
            seed_progress(progress_row,"clear",(n+count)*512,DISK_SECTORS*512);
            shown_percent=percent;
        }
        if (keyboard_poll()==KEY_F8) {rc=SEED_CANCELLED;goto done;}
    }
    phase="boot sectors";
    seed_progress(progress_row,"boot",0,1024);
    kmemcpy(sector,msdos_mbr,msdos_mbr_size);
    sector[446]=0x80; sector[448]=2; sector[450]=4;
    sector[451]=0; sector[452]=0xbf; sector[453]=7;
    w32(sector+454,1); w32(sector+458,DISK_SECTORS-1);
    sector[510]=0x55; sector[511]=0xaa;
    if (put_sector(0,sector)) goto done;
    if (read_floppy(0,sector,512)) goto done;
    sector[13]=4; w16(sector+14,1); sector[16]=2;
    w16(sector+17,512); w16(sector+19,DISK_SECTORS-1);
    sector[21]=0xf8; w16(sector+22,32); w16(sector+24,63);
    w16(sector+26,1); w32(sector+28,1); w32(sector+32,0);
    sector[36]=0x80;
    kmemcpy(sector+43,"DOS622     ",11); kmemcpy(sector+54,"FAT16   ",8);
    if (put_sector(1,sector)) goto done;
    w16(fat,0xfff8); w16(fat+2,0xffff);
    unsigned cluster=2, entry=0;
    shown_percent=0;
    seed_progress(progress_row,"copy",0,copy_total);
    for (unsigned i=0;i<224;i++) {
        phase="copy system files";progress=i;
        const uint8_t *e=source_root+i*32;
        if (!e[0]) break;
        /* Setup is for real disks; ship a prompt on virtual C instead. */
        if (!seed_file(e)) continue;
        uint32_t size=u32(e+28), left=size, src=u16(e+26);
        if (!size) continue;
        uint32_t count=(size+2047)/2048;
        if (count>8152-cluster+2 || entry>=510) goto done;
        kmemcpy(root+entry*32,e,32);
        w16(root+entry*32+26,cluster); ++entry;
        for (uint32_t c=0;c<count;c++) {
            w16(fat+cluster*2,c+1==count?0xffff:cluster+1);
            for (unsigned s=0;s<4;s++) {
                if (keyboard_poll()==KEY_F8) {rc=SEED_CANCELLED;goto done;}
                kmemset(sector,0,512);
                unsigned chunk=left>512?512:left;
                if (chunk) {
                    if (src<2 || src>=2849) goto done;
                    if (read_floppy((33+src-2)*512,sector,chunk)) goto done;
                    unsigned previous=src, at=src+src/2;
                    src=u16(source_fat+at);
                    src=previous&1 ? src>>4 : src&0xfff;
                    left-=chunk;
                }
                if (put_sector(98+(cluster-2)*4+s,sector)) goto done;
                copy_done+=chunk;
                uint32_t percent=copy_total ? copy_done*100/copy_total : 100;
                if (percent!=shown_percent) {
                    seed_progress(progress_row,"copy",copy_done,copy_total);
                    shown_percent=percent;
                }
            }
            ++cluster;
        }
        if (left) goto done;
    }
    /* Add a startup file without any 386 memory managers or disk drivers. */
    phase="startup and FAT";
    seed_progress(progress_row,"FAT",0,96*512);
    const char *startup="@ECHO OFF\r\nPROMPT $P$G\r\nPATH C:\\\r\n"
#if defined(CONFIG_MSDOS_SOUND) && CONFIG_MSDOS_SOUND
        MSDOS_BLASTER_LINE "\r\n"
#endif
        "ECHO MS-DOS 6.22 - F6 swap disks, F7 floppy, F8 exit\r\n";
    uint8_t *e=root+entry*32;
    kmemcpy(e,"AUTOEXECBAT",11); e[11]=32;
    w16(e+26,cluster); w32(e+28,kstrlen(startup));
    w16(fat+cluster*2,0xffff); kmemset(sector,0,512);
    kmemcpy(sector,startup,kstrlen(startup));
    if (put_sector(98+(cluster-2)*4,sector)) goto done;
    for (unsigned s=0;s<32;s++) {
        if (keyboard_poll()==KEY_F8) {rc=SEED_CANCELLED;goto done;}
        if (put_sector(2+s,fat+s*512) || put_sector(34+s,fat+s*512) ||
            put_sector(66+s,root+s*512)) goto done;
    }
    phase="publish virtual disk";
    if (keyboard_poll()==KEY_F8) {rc=SEED_CANCELLED;goto done;}
    seed_progress(progress_row,"FAT",96*512,96*512);
    rc=fs_image_publish(IMAGE_NAME,&hard);
done:
    if (progress_active) {
        vga_set_cursor(progress_row,0);
        vga_puts(rc ? "DOS HDD creation stopped.                                      \n" :
                      "DOS HDD ready; starting MS-DOS 6.22...                         \n");
    }
    if (rc) vga_printf("DOS disk initialization failed: %s (%u, code %d).\n",phase,progress,rc);
    kfree(fat);kfree(root);kfree(source_root);kfree(source_fat);return rc;
}

/* Keep storage/media selection separate from physical VGA/CMOS and CPU entry
 * so selection, recovery and refusal paths can be tested without host I/O. */
static int prepare_disk(int argc, const char *argv[]) {
    int use_floppy=argc==1 &&
        (!kstrcmp(argv[0],"/FLOPPY") || !kstrcmp(argv[0],"/floppy"));
    int use_image=argc==2 &&
        (!kstrcmp(argv[0],"/IMAGE") || !kstrcmp(argv[0],"/image"));
    int use_usb=(argc>=1 && argc<=USB_GAME_DISKS+1) &&
        (!kstrcmp(argv[0],"/USB") || !kstrcmp(argv[0],"/usb"));
    int selected_usb=-1;
    int usb_game=0;
    for (int a=1;use_usb && a<argc;a++) {
        const char *p=argv[a];
        unsigned len=kstrlen(p);
        if (len<5 || (p[0]!='U' && p[0]!='u') ||
            (p[1]!='S' && p[1]!='s') || (p[2]!='B' && p[2]!='b') ||
            p[3]<'0' || p[3]>='0'+USB_STORAGE_MAX || p[4]!=':') use_usb=0;
        else if (len==5 && argc==2) selected_usb=p[3]-'0';
        else if (len>6 && len<sizeof(usb_image) && (p[5]=='\\' || p[5]=='/')) {
            if (a==1) selected_usb=p[3]-'0';
            usb_game=1;
        } else use_usb=0;
    }
    if (argc && !use_floppy && !use_image && !use_usb) {
        vga_puts("Usage: MS-DOS [/MOUSE] [/SOUND2 | /SOUND16] [/CDROM]\n"
                 "       [/FLOPPY | /IMAGE <CD image> | /USB [USB0:..USB3:]]\n"
                 "       MS-DOS /USB USB0:\\DISK1.IMG [USB0:\\DISK2.IMG ...]\n"
                 "       Up to 8 game images; F6 cycles them in the supplied order.\n");return 1;
    }
    floppy_size=0; disc=-1; pending=-1; delivered_keyboard_state=-1;
    io_failed=0; media_index=0;physical=0;
    auto_media=physical_ready=0;
    usb_drive=-1;usb_image[0]=0;media_changed=0;
    usb_game_count=usb_game_index=0;
    int existing=-1, candidate=-1;
    uint8_t native_drive;
    int storage_rc=fs_volume_drive(0, &native_drive);
    if (storage_rc) return disk_failure(storage_rc);
    storage_rc=fs_image_open(native_drive,IMAGE_NAME,&hard);
    if (!storage_rc) existing=native_drive;
    else if (storage_rc==FS_ERR_NOTFOUND) {
        /* CD and HDD boot can select different native C: volumes. Reopen a
         * unique saved image rather than creating another on the new C:. */
        fs_image_t found;
        int found_drive=-1;
        for (uint8_t d=0;d<ATA_MAX_DRIVES;d++) {
            if (d==native_drive) continue;
            fs_image_t image;
            int rc=fs_image_open(d,IMAGE_NAME,&image);
            if (rc==FS_ERR_IO) return disk_failure(rc);
            if (rc) continue; /* absent/foreign/unsafe volumes are unchanged */
            if (found_drive>=0) {
                vga_puts("MS-DOS: multiple saved DOS HDDs found on other native drives.\n"
                         "Select the intended native C: before launching; no files changed.\n");
                return 1;
            }
            found=image;found_drive=d;
        }
        if (found_drive>=0) {hard=found;existing=found_drive;}
        else candidate=native_drive;
    }
    else return disk_failure(storage_rc);
    if (existing>=0 && hard.sectors!=DISK_SECTORS) {
        vga_puts("MS-DOS: invalid virtual HDD size; refusing to overwrite it.\n");return 1;
    }
    /* Explicit USB selection must succeed even with a saved C:. Never
     * silently fall back to CD/floppy or reserve a new HDD for bad USB media. */
    if (usb_game && existing<0) {
        vga_puts("MS-DOS: install DOS first using Disk 1; a game image cannot create DOS C:.\n");
        return 1;
    }
    if (use_usb && (usb_game ? load_usb_game(argv[1]) :
                              select_usb_media(selected_usb))) return 1;
    if (use_usb && usb_game) {
        usb_game_count=argc-1;
        for (unsigned i=0;i<usb_game_count;i++)
            kstrcpy(usb_game_paths[i],argv[i+1]);
    }
    if (existing<0 && reserve_disk(candidate)) return 1;
    if (existing<0) {
        if (use_usb) {
            vga_puts("Reading user-supplied DOS Disk 1 from USB...\n");
        } else if (use_floppy || find_install_cd()) {
            vga_puts("Reading user-supplied DOS Disk 1 from floppy A:...\n");
            if (load_install_floppy()) {
                vga_puts("Supply MS-DOS 6.22 Disk 1 on a separate\n"
                         "CD (D6221.IMG or DISK1.IMG), or insert a 1.44 MB DOS Disk 1\n"
                         "floppy in drive A: and run MS-DOS /FLOPPY.\n"
                         "Check that A: is configured as a 3.5-inch 1.44 MB drive.\n");return 1;
            }
        } else if (load_media(0,media_names[0])) {
            vga_puts("MS-DOS: supplied CD Disk 1 image is invalid or unreadable.\n");return 1;
        }
        int rc=seed_disk();
        if (rc) return disk_failure(rc);
    } else {
        vga_printf("Booting saved DOS HDD on native drive %u; installation CD not required.\n",
                   (unsigned)hard.drive);
        auto_media=!argc;
    }
    if (use_floppy) {physical=1;floppy_size=FLOPPY_BYTES;media_changed=1;}
    if (use_image && (find_image_cd(argv[1]) || load_media(-1,argv[1]))) {
        vga_puts("Cannot read that floppy image from the CD.\n");return 1;
    }
    return 0;
}

static int sound_option(const char *p,const char *option) {
    for (unsigned j=0;;j++) {
        char c=p[j];if (c>='a' && c<='z') c-=32;
        if (c!=option[j]) return 0;
        if (!c) return 1;
    }
}
int msdos_run(int argc, const char *argv[]) {
    /* Strip the independent /MOUSE switch before the existing media parser.
     * Retain path spelling/order; it may appear before or after USB paths. */
    const char *media_args[USB_GAME_DISKS+1];
    int media_argc=0,mouse=0,explicit_sound=0,optical=0;
    msdos_cdrom_close();
    unsigned model=CONFIG_MSDOS_SOUND?(CONFIG_MSDOS_SOUND_MODEL==16?
        MSDOS_SOUND_SB16:MSDOS_SOUND_SB2):MSDOS_SOUND_OFF;
    for (int i=0;i<argc;i++) {
        const char *p=argv[i],*option="/MOUSE";
        int same=1;
        for (unsigned j=0;;j++) {
            char c=p[j];if (c>='a' && c<='z') c-=32;
            if (c!=option[j]) {same=0;break;}
            if (!c) break;
        }
        if (sound_option(p,"/SOUND2") || sound_option(p,"/SOUND16")) {
            unsigned next=sound_option(p,"/SOUND16")?MSDOS_SOUND_SB16:MSDOS_SOUND_SB2;
            if (explicit_sound && model!=next) {
                vga_puts("MS-DOS: choose /SOUND2 or /SOUND16, not both.\n");return 1;
            }
            model=next;explicit_sound=1;
        } else if (sound_option(p,"/CDROM")) optical=1;
        else if (same) mouse=1;
        else if (media_argc<(int)(sizeof(media_args)/sizeof(media_args[0])))
            media_args[media_argc++]=p;
        else {vga_puts("MS-DOS: too many media arguments.\n");return 1;}
    }
    msdos_sound_select(model);
    if (prepare_disk(media_argc,media_args)) return 1;
    if (optical && msdos_cdrom_open()) {
        vga_puts("MS-DOS: cannot mount game CD: ");
        vga_puts(msdos_cdrom_error());vga_puts(".\n");return 1;
    }
    if (optical) vga_puts("DOS game CD: ISO files available read-only on D:.\n");
    if (model && msdos_startup_sound_for(&hard,model==MSDOS_SOUND_SB16)) {
        msdos_cdrom_close();
        vga_puts("MS-DOS: could not configure AUTOEXEC.BAT for sound; startup cancelled.\n");
        return 1;
    }
    if (model) vga_puts(model==MSDOS_SOUND_SB16?
        "DOS sound: Sound Blaster 16 digital PCM, 220h IRQ5 DMA1/5.\n":
        "DOS sound: Sound Blaster 2 digital PCM, 220h IRQ5 DMA1.\n");
    int mouse_ready=msdos_mouse_start(mouse);
    if (mouse) vga_puts(mouse_ready?
        "DOS mouse: driver enabled; pointer hidden, F12 toggles visibility.\n":
        "DOS mouse unavailable: no supported USB boot mouse or responding PS/2 mouse/touchpad.\n");
    vga_puts("DOS CPU: Intel 80286 real-mode compatibility.\n");
    int sr,sc;
    vga_get_cursor(&sr,&sc);
    for (int i=0;i<2000;i++) saved[i]=SCREEN[i];
    row=col=save_row=save_col=ansi=bright=0; attr=7;
    region_top=0;region_bottom=24;blank(0,2000);
    rtc_init();last_refresh=timer_get_ticks()-16;media_changed=0;
    int rc=msdos_cpu_run(hard.sectors);
    msdos_cdrom_close();
    msdos_mouse_stop();
    int audio_error=msdos_sound_error();
    const char *audio_backend=pcm_backend();
    msdos_devices_restore();
    floppy_size=0;
    for (int i=0;i<2000;i++) SCREEN[i]=saved[i];
    vga_set_cursor(sr,sc);
    if (audio_error) vga_puts("DOS audio: an unsupported DSP command or invalid DMA buffer was used.\n");
    if (audio_backend) {vga_puts("DOS audio output stopped: ");vga_puts(audio_backend);vga_puts(".\n");}
    else if (model) {vga_puts("DOS audio was silent: ");vga_puts(pcm_error());vga_puts(".\n");}
    vga_puts(io_failed?"MS-DOS stopped: disk I/O failed.\n":
             rc==-3?"MS-DOS stopped: protected mode is not supported (real-mode 80286 guest).\n":
             rc==-4?"MS-DOS stopped: invalid guest interrupt table.\n":
             rc==-2?"MS-DOS stopped: unsupported CPU instruction or unhandled fault (386/x87).\n":
             rc?"MS-DOS: not enough memory for the emulator.\n":
                "Returned from MS-DOS. Completed disk writes are saved.\n");
    return rc || io_failed;
}
