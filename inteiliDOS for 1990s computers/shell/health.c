/* System Health: read-only, live diagnostics for VGA text-mode computers. */
#include "health.h"
#include "../kernel/ata.h"
#include "../kernel/cdrom.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"
#include "../kernel/timer.h"
#include "../kernel/vga.h"

typedef struct {
    ata_drive_t info;
    ata_health_t smart;
    uint32_t tail, read_ms;
    int first_ok, last_ok, boot_signature;
} disk_t;
static disk_t disks[ATA_MAX_DRIVES];
static unsigned count, selected, page;
static uint32_t disk_tick, updates, keys;
static char vendor[13], brand[49];
static unsigned family, model, stepping;
static int has_cpuid;

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}
static void cpu_info(void) {
    uint32_t before, after, edited;
    __asm__ volatile("pushfl; popl %0" : "=r"(before));
    edited = before^(1u<<21);
    __asm__ volatile("pushl %1; popfl; pushfl; popl %0; pushl %2; popfl"
                     : "=&r"(after) : "r"(edited), "r"(before) : "cc", "memory");
    has_cpuid = !!((before^after)&(1u<<21));
    vendor[0] = brand[0] = 0; family = model = stepping = 0;
    if (!has_cpuid) return;
    uint32_t a,b,c,d;
    cpuid(0, &a,&b,&c,&d);
    kmemcpy(vendor,&b,4); kmemcpy(vendor+4,&d,4); kmemcpy(vendor+8,&c,4);
    vendor[12] = 0;
    if (a >= 1) {
        cpuid(1,&a,&b,&c,&d);
        unsigned base_family = (a>>8)&15;
        family = base_family + (base_family == 15 ? (a>>20)&255 : 0);
        model = ((a>>4)&15) + ((base_family == 6 || base_family == 15) ? ((a>>16)&15)*16 : 0);
        stepping = a&15;
    }
    cpuid(0x80000000,&a,&b,&c,&d);
    if (a >= 0x80000004) {
        for (unsigned i=0; i<3; i++) {
            cpuid(0x80000002+i,&a,&b,&c,&d);
            kmemcpy(brand+i*16,&a,4); kmemcpy(brand+i*16+4,&b,4);
            kmemcpy(brand+i*16+8,&c,4); kmemcpy(brand+i*16+12,&d,4);
        }
        brand[48] = 0;
    }
}
static void refresh_disks(void) {
    static uint8_t sector[512];
    for (unsigned i=0; i<count; i++) {
        disk_t *disk = &disks[i];
        uint32_t begin = timer_get_ticks();
        disk->first_ok = ata_read_sector(disk->info.drive_index,0,sector) == 0;
        disk->boot_signature = disk->first_ok && sector[510] == 0x55 && sector[511] == 0xAA;
        disk->tail = disk->info.total_sectors ? disk->info.total_sectors-1 : 0;
        if (disk->info.drive_type == ATA_TYPE_PATA && disk->tail > 0x0FFFFFFF)
            disk->tail = 0x0FFFFFFF;
        disk->last_ok = disk->info.total_sectors &&
                       ata_read_sector(disk->info.drive_index,disk->tail,sector) == 0;
        ata_read_health(disk->info.drive_index,&disk->smart);
        disk->read_ms = timer_get_ticks()-begin;
    }
    disk_tick = timer_get_ticks();
}
static int disk_bad(const disk_t *d) {
    return !d->first_ok || !d->last_ok || d->smart.status == ATA_HEALTH_FAIL;
}
static int disk_warning(const disk_t *d) {
    const ata_health_t *s = &d->smart;
    return s->status != ATA_HEALTH_OK || !s->attributes_valid ||
           (s->reallocated_valid && s->reallocated) || (s->pending_valid && s->pending) ||
           (s->uncorrectable_valid && s->uncorrectable) ||
           (s->temperature_valid && s->temperature_c >= 60);
}
static void line(unsigned row) {
    vga_set_cursor(row,2);
    vga_set_color(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}
static void status(const char *text, int bad, int warning) {
    vga_set_color(bad ? VGA_COLOR_LIGHT_RED : warning ? VGA_COLOR_LIGHT_BROWN : VGA_COLOR_LIGHT_GREEN,
                  VGA_COLOR_BLACK);
    vga_puts(text);
}
static const char *smart_status(unsigned state) {
    switch (state) {
    case ATA_HEALTH_DISABLED: return "Disabled (left unchanged)";
    case ATA_HEALTH_OK: return "Drive reports PASS";
    case ATA_HEALTH_FAIL: return "FAILURE predicted - back up now!";
    case ATA_HEALTH_IO_ERROR: return "SMART read failed";
    case ATA_HEALTH_BAD_DATA: return "Invalid / incomplete SMART response";
    case ATA_HEALTH_INTERFACE: return "Unavailable through this AHCI driver";
    default: return "Not supported / not advertised";
    }
}
static void attribute(const char *label, int available, unsigned value) {
    vga_puts(label);
    if (available) {
        if (value==0xFFFFFFFFu) vga_puts(">=");
        vga_printf("%u",value);
    }
    else vga_puts("Unavailable");
}
static const char *disk_message(const disk_t *d) {
    const ata_health_t *s=&d->smart;
    if (!d->first_ok || !d->last_ok) return "Read failure: back up files and investigate the drive/cable.";
    if (s->status==ATA_HEALTH_FAIL) return "SMART predicts failure: back up files and replace the drive.";
    if ((s->pending_valid && s->pending) || (s->uncorrectable_valid && s->uncorrectable))
        return "Unstable/unreadable sectors reported: back up important files.";
    if (s->reallocated_valid && s->reallocated)
        return "Reallocated sectors reported: back up and monitor for increases.";
    if (s->temperature_valid && s->temperature_c>=60)
        return "Temperature >=60 C: inspect drive cooling (advisory).";
    if (s->status!=ATA_HEALTH_OK || !s->attributes_valid)
        return "SMART monitoring unavailable/incomplete; wear cannot be assessed.";
    return "Available checks passed; failures can still occur.";
}
static void draw(uint32_t now) {
    memory_heap_stats_t heap;
    int heap_ok = memory_heap_snapshot(&heap) == 0;
    uint32_t total = memory_total_kb(), free = memory_free_kb();
    int bad = !heap_ok || (total && free>total), warnings = !total;
    unsigned failures = 0, limited = 0;
    for (unsigned i=0; i<count; i++) {
        if (disk_bad(&disks[i])) failures++;
        else if (disk_warning(&disks[i])) limited++;
    }
    bad |= failures != 0;
    warnings |= limited != 0 || (heap_ok && heap.free_bytes<65536) ||
                (total && free<256);
    /* Clear cells directly to avoid flickering and scroll at row 24. */
    volatile uint16_t *vram = (volatile uint16_t *)0xB8000;
    for (unsigned i=0; i<2000; i++) vram[i] = 0x0F20;
    line(0); vga_set_color(VGA_COLOR_WHITE,VGA_COLOR_BLUE);
    vga_puts(" SYSTEM HEALTH | inteiliDOS ");
    static const char *tabs[] = {"1 Health dashboard","2 Disk health","3 System info"};
    line(2);
    for (unsigned i=0; i<3; i++) {
        vga_set_color(i==page ? VGA_COLOR_BLACK : VGA_COLOR_LIGHT_CYAN,
                      i==page ? VGA_COLOR_LIGHT_CYAN : VGA_COLOR_BLACK);
        vga_printf(" %s ",tabs[i]);
    }
    line(4); vga_puts("Overall: ");
    status(bad ? "PROBLEM DETECTED" : warnings ? "WARNINGS / LIMITED MONITORING" :
           "No problems detected in available checks",bad,warnings);
    if (page == 0) {
        line(6); vga_printf("Uptime: %u days %02u:%02u:%02u",now/86400000,
                    (now/3600000)%24,(now/60000)%60,(now/1000)%60);
        line(8); vga_printf("Physical RAM: %u KB total | %u KB allocatable",total,free);
        line(9); vga_puts("Heap metadata: ");
        status(heap_ok ? "Valid" : "CORRUPT / unavailable",!heap_ok,0);
        line(10);
        if (heap_ok) vga_printf("Heap: %u KB used | %u KB free | largest block %u KB",
                  heap.used_bytes/1024,heap.free_bytes/1024,heap.largest_free/1024);
        line(12); vga_printf("ATA HDDs: %u | read/failure alerts: %u | warnings/limited: %u",
                             count,failures,limited);
        line(13); vga_printf("CD-ROM drives: %u (not included in HDD SMART checks)",cdrom_count());
        line(15);
        if (!heap_ok) status("Heap error: avoid launching apps; restart safely.",1,0);
        else if (total && free>total) status("Invalid physical RAM accounting detected.",1,0);
        else if (!total) status("Physical RAM information unavailable; check is incomplete.",0,1);
        else if (heap.free_bytes<65536 || (total && free<256))
            status("Low allocatable memory: close programs before starting more.",0,1);
        else vga_puts("Memory checks: no error detected.");
        line(16);
        if (failures) status("Disk error/failure: open Disk health; back up important files.",1,0);
        else if (limited) status("Disk warning or incomplete monitoring: open Disk health.",0,1);
        else if (!count) vga_puts("No ATA HDD detected. Booting from CD/floppy is not a fault.");
        else vga_puts("Sampled disk reads and available SMART checks passed.");
        line(18); vga_puts("CPU temperature, fans, voltage and CPU load: unavailable.");
        line(19); vga_puts("Checks are limited; this is not a full RAM test or disk surface scan.");
    } else if (page == 1) {
        if (!count) {
            line(7); vga_puts("No ATA hard disk detected.");
            line(9); vga_puts("Only boot-detected IDE/AHCI disks are monitored, not CD/floppy.");
            line(11); vga_puts("No HDD is normal for a CD/floppy-only system.");
        } else {
            disk_t *disk=&disks[selected]; ata_health_t *s=&disk->smart;
            line(6); vga_printf("Disk %u/%u | slot %u | %s",selected+1,count,disk->info.drive_index,
                         disk->info.drive_type==ATA_TYPE_PATA ? "IDE / PATA" : "SATA / AHCI");
            line(7); vga_printf("Model: %s",disk->info.model);
            line(8); vga_printf("Capacity: %u MiB | %u sectors of 512 bytes",
                               disk->info.total_sectors/2048,disk->info.total_sectors);
            line(10); vga_puts("Read samples: ");
            status(disk->first_ok && disk->last_ok ? "PASS" : "READ ERROR",
                   !disk->first_ok || !disk->last_ok,0);
            vga_printf(" | LBA 0: %s | LBA %u: %s",disk->first_ok?"OK":"FAIL",
                       disk->tail,disk->last_ok?"OK":"FAIL");
            line(11); vga_printf("Boot signature: %s (not a hardware-health test)",
                                disk->boot_signature ? "55 AA present" : "Absent / not bootable");
            line(12); vga_puts("SMART: "); status(smart_status(s->status),
                       s->status==ATA_HEALTH_FAIL,s->status!=ATA_HEALTH_OK);
            line(14); attribute("Temperature: ",s->temperature_valid,s->temperature_c);
            if (s->temperature_valid) vga_puts(" C (>=60 C is an advisory warning)");
            line(15); attribute("Reallocated sectors: ",s->attributes_valid&&s->reallocated_valid,s->reallocated);
            line(16); attribute("Pending sectors:     ",s->attributes_valid&&s->pending_valid,s->pending);
            line(17); attribute("Uncorrectable:       ",s->attributes_valid&&s->uncorrectable_valid,s->uncorrectable);
            line(19); status(disk_message(disk),disk_bad(disk),disk_warning(disk));
            line(20); vga_puts("SMART attribute meanings vary by vendor. No writes/self-tests are run.");
            line(21); vga_printf("Sample age: %u s | check duration: %u ms | Up/Down: select disk",
                                 (now-disk_tick)/1000,disk->read_ms);
        }
    } else {
        line(6); vga_puts("OS: inteiliDOS | IA-32 protected mode | VGA 80x25 text");
        line(8); vga_printf("CPU vendor: %s",has_cpuid ? vendor : "CPUID unavailable (386/early 486)");
        line(9); if (has_cpuid) vga_printf("CPU family %u | model %u | stepping %u",family,model,stepping);
        else vga_puts("CPU identification is limited on this processor.");
        line(10); vga_printf("CPU name: %s",brand[0] ? brand : "Model string unavailable");
        line(12); vga_printf("Live PIT ticks: %u | uptime: %u s | refreshes: %u",now,now/1000,updates);
        line(13); vga_printf("Keys handled by this app: %u | VGA/keyboard UI: active",keys);
        line(14); vga_printf("Physical RAM: %u KB | currently allocatable: %u KB",total,free);
        line(15); vga_printf("Heap integrity: %s | blocks: %u",heap_ok?"valid":"ERROR",heap.blocks);
        line(16); vga_printf("Storage inventory: %u HDDs / %u CD-ROM drives",count,cdrom_count());
        line(18); vga_puts("Live: timer, memory/heap snapshots; disk checks every 5 seconds.");
        line(19); vga_puts("No CPU load accounting or temperature/fan sensors are available.");
        line(20); vga_puts("Inventory is boot-time; reboot after attaching new hardware.");
    }
    line(23); vga_set_color(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    vga_puts("Tab/Left/Right or 1-3: tabs | R: read-only recheck | Esc/Q: quit");
    vga_set_cursor(24,0);
}

void health_run(void) {
    count=selected=page=updates=keys=0;
    for (unsigned i=0; i<ATA_MAX_DRIVES; i++) {
        ata_drive_t info;
        if (ata_get_drive(i,&info)==0) disks[count++].info=info;
    }
    cpu_info();
    vga_clear(); line(6); vga_puts("System Health: checking disks (read-only)...");
    refresh_disks();
    uint32_t painted=timer_get_ticks();
    draw(painted); updates++;
    for (;;) {
        int key=keyboard_poll(), repaint=0;
        if (key>=0) {
            keys++; repaint=1;
            if (key==KEY_ESCAPE || key=='q' || key=='Q') break;
            if (key=='\t' || key==KEY_RIGHT) page=(page+1)%3;
            if (key==KEY_LEFT) page=(page+2)%3;
            if (key>='1' && key<='3') page=key-'1';
            if (page==1 && count && key==KEY_UP) selected=(selected+count-1)%count;
            if (page==1 && count && key==KEY_DOWN) selected=(selected+1)%count;
            if (key=='r' || key=='R') refresh_disks();
        }
        uint32_t now=timer_get_ticks();
        if (now-disk_tick>=5000) { refresh_disks(); now=timer_get_ticks(); repaint=1; }
        if (repaint || now-painted>=500) { draw(now); updates++; painted=now; }
        __asm__ volatile("hlt");
    }
    vga_set_color(VGA_COLOR_WHITE,VGA_COLOR_BLACK); vga_clear();
}
