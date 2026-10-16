/* System Health: read-only, live diagnostics for VGA text-mode computers. */
#include "health.h"
#include "../kernel/ata.h"
#include "../kernel/cdrom.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"
#include "../kernel/timer.h"
#include "../kernel/thermal.h"
#include "../kernel/battery.h"
#include "../kernel/usb.h"
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
static int has_long_mode;

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
    has_long_mode=0;
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
    uint32_t extended_max=a;
    if (extended_max>=0x80000001u) {
        cpuid(0x80000001u,&a,&b,&c,&d);
        has_long_mode=!!(d&(1u<<29));
    }
    if (extended_max >= 0x80000004) {
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
    battery_snapshot_t battery;
    battery_snapshot(&battery);
    int battery_live=battery.status==BATTERY_OK && now-battery.sampled_ms<10000;
    thermal_snapshot_t cooling;
    thermal_snapshot(&cooling);
    int cooling_live = cooling.status == THERMAL_OK &&
                       now-cooling.last_sample_ms < 2000;
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
    bad |= cooling.cooling_needed != 0;
    warnings |= !cooling_live;
    warnings |= battery_live && ((battery.percent_valid && battery.percent<=10 &&
                                 battery.discharging) ||
                                (battery.health_valid && battery.health_percent<60));
    /* Clear cells directly to avoid flickering and scroll at row 24. */
    volatile uint16_t *vram = (volatile uint16_t *)0xB8000;
    for (unsigned i=0; i<2000; i++) vram[i] = 0x0F20;
    line(0); vga_set_color(VGA_COLOR_WHITE,VGA_COLOR_BLUE);
    vga_puts(" SYSTEM HEALTH | inteiliDOS ");
    static const char *tabs[] = {"1 Dashboard","2 Disk health","3 System info","4 Cooling","5 Battery"};
    line(2);
    for (unsigned i=0; i<5; i++) {
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
        line(18); vga_puts("Thermal monitoring: ");
        status(cooling.cooling_needed ? "COOLING NEEDED - open Cooling tab" :
               cooling_live ? "Live readings - open Cooling tab" :
               "Unavailable / incomplete - open Cooling tab",
               cooling.cooling_needed,!cooling_live);
        line(19); vga_puts("Voltage and CPU load accounting: unavailable.");
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
    } else if (page == 2) {
        line(6); vga_printf("OS: inteiliDOS | 32-bit IA-32 | CPU x86-64: %s",
                           has_long_mode ? "capable" : "not available");
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
        line(19); vga_puts("Temperature/fan diagnostics: Cooling tab | CPU load: unavailable.");
        line(20); vga_puts("Inventory is boot-time; reboot after attaching new hardware.");
        line(21); vga_printf("Keyboard: %s",usb_keyboard_status());
    } else if (page==3) {
        line(6); vga_printf("Background thermal monitor: every %u ms | checks: %u",
                           THERMAL_INTERVAL_MS,cooling.checks);
        line(7); vga_printf("Sensor: %s",cooling.sensor);
        line(8); vga_puts("CPU temperature: ");
        if (cooling_live && cooling.cpu_channel &&
            cooling.temperature_valid[cooling.cpu_channel-1])
            vga_printf("%d C (verified channel %u)",
                       cooling.temperatures[cooling.cpu_channel-1],cooling.cpu_channel);
        else vga_puts("Unavailable (no fresh reading or unverified sensor wiring)");
        for (unsigned i=0; i<3; i++) {
            line(10+i); vga_printf("Temperature channel %u: ",i+1);
            if (cooling_live && cooling.temperature_valid[i])
                vga_printf("%d C%s",cooling.temperatures[i],
                           cooling.cpu_channel==i+1 ? " [CPU]" : " [board / remote sensor]");
            else vga_puts("Unavailable (absent, disabled, failed or stale)");
        }
        for (unsigned i=0; i<3; i++) {
            line(14+i); vga_printf("Fan header %u: ",i+1);
            if (!cooling_live || !cooling.fan_valid[i]) vga_puts("Status unavailable");
            else if (cooling.fan_rpm[i])
                vga_printf("running cooling fan... %u RPM (tachometer)",cooling.fan_rpm[i]);
            else vga_puts("not running / no tach pulses (may be an unused header)");
        }
        line(18);
        status(cooling.cooling_needed ? "Temperature warning: check cooling; stop heavy work." :
               cooling_live ? "Monitoring active; no current temperature warning." :
               cooling.status==THERMAL_READ_ERROR ? "Sensor read failed; protection is NOT confirmed." :
               "Thermal hardware unavailable; protection is NOT confirmed.",
               cooling.cooling_needed,!cooling_live);
        line(19); vga_printf("Advisory warning >=%d C; clears when all channels <=%d C.",
                            THERMAL_WARN_C,THERMAL_CLEAR_C);
        line(20); vga_puts("Fan control: unavailable. Existing BIOS/hardware cooling is unchanged.");
        line(21); vga_printf("Samples: %u | last sample age: %u ms%s",cooling.samples,
                            cooling.samples ? now-cooling.last_sample_ms : 0,
                            cooling.samples ? "" : " (no reading yet)");
        line(22); vga_puts("Not a guaranteed overheating safeguard. Never disable BIOS protection.");
    } else {
        line(6);vga_puts("Battery percentage: ");
        if (battery_live && battery.percent_valid) vga_printf("%u%%",battery.percent);
        else vga_puts("Unavailable");
        line(8);vga_puts("Battery health (full / design capacity): ");
        if (battery_live && battery.health_valid) vga_printf("%u%%",battery.health_percent);
        else vga_puts("Unavailable");
        line(10);vga_puts("Power status: ");
        if (!battery_live) vga_puts("Unavailable");
        else vga_puts(battery.fully_charged ? "Fully charged" :
                      battery.discharging ? "Discharging" :
                      "Not discharging (charging / idle)");
        line(12);
        if (battery_live && battery.health_valid)
            vga_printf("Full capacity: %u | design: %u %s",battery.full_capacity,
                       battery.design_capacity,battery.capacity_energy ? "x 10 mWh" : "mAh");
        else vga_puts("Capacity information: unavailable");
        line(14);
        if (!battery_live) status(battery.status==BATTERY_WAITING ?
            "Waiting for a supported Smart Battery reading." :
            "No readable battery data; presence and health are unknown.",0,1);
        else if (battery.percent_valid && battery.percent<=10 && battery.discharging)
            status("Low battery: connect external power and save your work.",0,1);
        else if (battery.health_valid && battery.health_percent<60)
            status("Reduced capacity: consider battery service/replacement.",0,1);
        else vga_puts("Health percentage estimates retained capacity, not electrical safety.");
        line(16);vga_puts("Source: Smart Battery at SMBus 0x0B; read-only, every 5 seconds.");
        line(17);vga_puts("Supported: firmware-enabled Intel PIIX4 / selected ICH controllers.");
        line(18);vga_puts("ACPI-only / embedded-controller batteries are not supported.");
        line(19);vga_puts("No readings can mean a desktop, unsupported hardware or read failure.");
        line(20);vga_puts("No charging, firmware, battery-mode or fan settings are changed.");
        line(21);
        if (battery_live) vga_printf("Last successful sample: %u ms ago",now-battery.sampled_ms);
        else vga_puts("Battery readings unavailable; no percentage is assumed.");
    }
    line(23); vga_set_color(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    vga_puts("Tab/Left/Right or 1-5: tabs | R: read-only recheck | Esc/Q: quit");
    vga_set_cursor(24,0);
}

void health_run(void) {
    count=selected=page=updates=keys=0;
    for (unsigned i=0; i<ATA_MAX_DRIVES; i++) {
        ata_drive_t info;
        if (ata_get_drive(i,&info)==0) disks[count++].info=info;
    }
    cpu_info();
    battery_init();
    vga_clear(); line(6); vga_puts("System Health: checking disks (read-only)...");
    refresh_disks();
    uint32_t painted=timer_get_ticks();
    draw(painted); updates++;
    for (;;) {
        int key=keyboard_poll(), repaint=0;
        if (key>=0) {
            keys++; repaint=1;
            if (key==KEY_ESCAPE || key=='q' || key=='Q') break;
            if (key=='\t' || key==KEY_RIGHT) page=(page+1)%5;
            if (key==KEY_LEFT) page=(page+4)%5;
            if (key>='1' && key<='5') page=key-'1';
            if (page==1 && count && key==KEY_UP) selected=(selected+count-1)%count;
            if (page==1 && count && key==KEY_DOWN) selected=(selected+1)%count;
            if (key=='r' || key=='R') refresh_disks();
        }
        uint32_t now=timer_get_ticks();
        battery_poll(now);
        if (now-disk_tick>=5000) { refresh_disks(); now=timer_get_ticks(); repaint=1; }
        if (repaint || now-painted>=500) { draw(now); updates++; painted=now; }
        __asm__ volatile("hlt");
    }
    battery_shutdown();
    vga_set_color(VGA_COLOR_WHITE,VGA_COLOR_BLACK); vga_clear();
}
