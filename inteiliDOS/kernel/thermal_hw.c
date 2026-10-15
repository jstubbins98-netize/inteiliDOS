/*
 * Pre-enabled Intel PIIX4 SMBus + W83781D/W83782D/AS99127F monitoring.
 * No fan/PWM, alarm, sensor-enable or temperature-limit writes.
 * Only the bank selector is temporarily changed, then restored.
 * No arbitrary ISA probing, SMBus enumeration or firmware BIOS calls.
 *
 * Register reference: Linux hwmon/w83781d and i2c-piix4 documentation.
 * CPU channel 3 is verified in 86Box's VEi8/Pavilion85xx board definitions.
 */
#include "thermal_hw.h"
#include "pci.h"
#include "memory.h"
#include "config.h"

#ifdef THERMAL_HW_TEST
#include "../tests/thermal_test_io.h"
#else
static uint8_t inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v;
}
static void outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));
}
#endif

static uint16_t base;
static uint8_t stage, active, transaction, bank, changed, failed;
static uint8_t vendor_lo, vendor_hi, chip, config, div12, div3, div_high;
static uint8_t raw_temp[3], temp_config[2], raw_fan[3], temp_low[2], cpu_channel;
static uint32_t started;
static const char *name = "Unavailable";

/* For non-SMBIOS boards a verified wiring override may be supplied at build
 * time. Do not guess a CPU sensor merely from a monitor's manufacturer. */
#ifndef CONFIG_THERMAL_CPU_CHANNEL
#define CONFIG_THERMAL_CPU_CHANNEL 0
#endif
#if CONFIG_THERMAL_CPU_CHANNEL < 0 || CONFIG_THERMAL_CPU_CHANNEL > 3
#error CONFIG_THERMAL_CPU_CHANNEL must be 0, 1, 2 or 3
#endif

#ifndef THERMAL_HW_TEST
static int checksum(const uint8_t *p, unsigned len) {
    uint8_t sum = 0;
    while (len--) sum += *p++;
    return sum == 0;
}
static int contains(const char *p, const char *word) {
    for (; *p; p++) {
        unsigned i = 0;
        while (word[i]) {
            char c = p[i], w = word[i];
            if (!c) break;
            if (c >= 'A' && c <= 'Z') c += 'a'-'A';
            if (c != w) break;
            i++;
        }
        if (!word[i]) return 1;
    }
    return 0;
}
static uint8_t firmware_cpu_channel(void) {
    /* SMBIOS 2.x in the legacy BIOS window. Bounds/checksums before walking.
     * Restrict tables to low 1 MiB: do not dereference unknown physical RAM. */
    for (uint32_t addr = 0xF0000; addr <= 0xFFFE0; addr += 16) {
        const uint8_t *ep = (const uint8_t *)(uintptr_t)addr;
        if (ep[0]!='_' || ep[1]!='S' || ep[2]!='M' || ep[3]!='_' ||
            ep[5]<31 || ep[5]>32 || !checksum(ep,ep[5]) ||
            ep[16]!='_' || ep[17]!='D' || ep[18]!='M' ||
            ep[19]!='I' || ep[20]!='_' || !checksum(ep+16,15)) continue;
        unsigned len = ep[22] | (ep[23]<<8);
        uint32_t start = ep[24] | ((uint32_t)ep[25]<<8) |
                         ((uint32_t)ep[26]<<16) | ((uint32_t)ep[27]<<24);
        if (start < 0x400 || start >= 0x100000 || len > 0x100000-start) continue;
        const uint8_t *p = (const uint8_t *)(uintptr_t)start, *end = p+len;
        while (end-p >= 4 && p[1]>=4 && p[1]<=end-p) {
            const uint8_t *strings=p+p[1], *next=strings;
            while (end-next>=2 && (next[0] || next[1])) next++;
            if (end-next<2) break;
            if (p[0]==1 && p[1]>=6) {
                char manufacturer[65]={0}, product[65]={0};
                const uint8_t *s=strings;
                for (unsigned index=1; s<next && *s; index++) {
                    char *dst = index==p[4] ? manufacturer : index==p[5] ? product : 0;
                    unsigned n=0;
                    while (s<next && *s) {
                        if (dst && n<64) dst[n++]=(char)*s;
                        s++;
                    }
                    s++;
                }
                if ((contains(manufacturer,"hewlett") || contains(manufacturer,"hp")) &&
                    (contains(product,"vei8") || contains(product,"vei 8") ||
                     contains(product,"pavilion 85"))) return 3;
            }
            if (p[0]==127) break;
            p=next+2;
        }
    }
    return 0;
}
#endif

int thermal_hw_init(void) {
    base=active=transaction=changed=0;
    cpu_channel=CONFIG_THERMAL_CPU_CHANNEL;
#ifndef THERMAL_HW_TEST
    if (!cpu_channel) cpu_channel=firmware_cpu_channel();
#endif
    /* Only PIIX4 (8086:7113), already configured by firmware. Do not enable
     * PCI decoding, assign a BAR, change IRQ routing or steal a busy bus. */
    for (unsigned dev=0; dev<32; dev++) {
        if (pci_read32(0,dev,3,0) != 0x71138086u) continue;
        if (!(pci_read16(0,dev,3,4)&1) ||
            !(pci_read16(0,dev,3,0xD2)&1)) continue;
        uint16_t io = pci_read16(0,dev,3,0x90);
        if (!(io&1) || !(io&0xFFF0) || (io&0xFFF0)==0xFFF0) continue;
        base=io&0xFFF0;
        return 1;
    }
    return 0;
}
uint8_t thermal_hw_cpu_channel(void) { return cpu_channel; }

/* One SMBus byte-data operation, no wait loops. Ownership remains ours
 * until completion; the next host IRQ advances it. */
static int request(uint8_t reg, int write, uint8_t val, uint32_t now) {
    if (inb(base)&1) return 0;
    outb(base,0x1E); /* clear completed/error flags, never interrupt enable */
    outb(base+3,reg);
    outb(base+4,0x5A | (write ? 0 : 1)); /* 7-bit monitor address 0x2D */
    if (write) outb(base+5,val);
    outb(base+2,0x48); /* BYTE_DATA | START; interrupts disabled */
    started=now; transaction=1;
    return 1;
}

int thermal_hw_start(uint32_t now) {
    if (!base || active || (inb(base)&1)) return 0;
    stage=0; active=1; changed=failed=0; name="Unavailable";
    if (!request(0x4E,0,0,now)) { active=0; return 0; }
    return 1;
}

/* Stage advances at most once per tick. A successful cycle takes ~30 ms,
 * starting every 500 ms. Transport errors always invalidate the cycle. */
int thermal_hw_step(uint32_t now) {
    if (!active) return 0;
    if (transaction) {
        uint8_t status=inb(base);
        if ((status&1) && now-started<10) return 0;
        if (status&1) {
            /* Terminate ONLY our timed-out transfer. No controller reset. */
            outb(base+2,2);
            if (inb(base)&1) { base=0; active=transaction=0; return -1; }
            outb(base+2,0);
            failed=1;
        } else if ((status&0x1C) || !(status&2)) failed=1;
        uint8_t val=inb(base+5);
        outb(base,0x1E); transaction=0;
        if (!failed) {
            switch (stage) {
            case 0: bank=val; break;
            case 1: vendor_lo=val; break;
            case 3: vendor_hi=val; break;
            case 5: chip=val; break;
            case 6: config=val; break;
            case 7: div12=val; break;
            case 8: div3=val; break;
            case 9: div_high=val; break;
            case 10: raw_temp[0]=val; break;
            case 11: raw_fan[0]=val; break;
            case 12: raw_fan[1]=val; break;
            case 13: raw_fan[2]=val; break;
            case 15: raw_temp[1]=val; break;
            case 16: temp_low[0]=val; break;
            case 17: temp_config[0]=val; break;
            case 19: raw_temp[2]=val; break;
            case 20: temp_low[1]=val; break;
            case 21: temp_config[1]=val; break;
            }
            if (stage==0 && (bank&0x78)) failed=1;
            if (stage==1) {
                /* Validate the currently selected vendor byte before writing
                 * any sensor register. 0xFF / ordinary LM78 are rejected. */
                if (bank&0x80) {
                    if (val!=0x5C && val!=0x12) failed=1;
                } else if (val!=0xA3 && val!=0xC3) failed=1;
            }
            if (stage==3) {
                uint8_t lo=(bank&0x80) ? vendor_hi : vendor_lo;
                uint8_t hi=(bank&0x80) ? vendor_lo : vendor_hi;
                if (!((lo==0xA3 && hi==0x5C) || (lo==0xC3 && hi==0x12))) failed=1;
            }
            if (stage==5) {
                if (chip==0x10 || chip==0x11) name="Winbond W83781D";
                else if (chip==0x30) name="Winbond W83782D (monitoring only)";
                else if (chip==0x31) name="ASUS AS99127F";
                else failed=1;
                if ((chip!=0x31) && ((bank&0x80 ? vendor_lo : vendor_hi)!=0x5C)) failed=1;
            }
            if (stage==6 && (!(config&1) || (config&0x80))) failed=1;
        }
        if (stage==22) { active=0; return failed ? -1 : 1; }
        if (failed) {
            if (!changed) { active=0; return -1; }
            stage=22; /* best effort restore even after a failed bank write */
        } else stage++;
    }
    uint8_t reg=0, val=0; int write=0;
    switch (stage) {
    case 1: reg=0x4F; break;
    case 2: reg=0x4E; val=bank^0x80; write=1; break;
    case 3: reg=0x4F; break;
    case 4: reg=0x4E; val=0x80; write=1; break;
    case 5: reg=0x58; break;
    case 6: reg=0x40; break;
    case 7: reg=0x47; break;
    case 8: reg=0x4B; break;
    case 9: reg=0x5D; break;
    case 10: reg=0x27; break;
    case 11: reg=0x28; break;
    case 12: reg=0x29; break;
    case 13: reg=0x2A; break;
    case 14: reg=0x4E; val=0x81; write=1; break;
    case 15: reg=0x50; break;
    case 16: reg=0x51; break;
    case 17: reg=0x52; break;
    case 18: reg=0x4E; val=0x82; write=1; break;
    case 19: reg=0x50; break;
    case 20: reg=0x51; break;
    case 21: reg=0x52; break;
    case 22: reg=0x4E; val=bank; write=1; break;
    default: active=0; return -1;
    }
    if (write) changed=1;
    if (!request(reg,write,val,now)) {
        /* Firmware may own the bus between operations. Never clear/start a
         * busy transfer; bound our deferral and retain the warning. */
        if (now-started>=50) { base=0; active=0; return -1; }
    }
    return 0;
}

void thermal_hw_sample(thermal_snapshot_t *out) {
    out->sensor=name;
    for (unsigned i=0; i<3; i++) {
        int c=(int8_t)raw_temp[i];
        /* Conservatively round a remote sensor's half degree UP. In
         * particular 65.5 C must never clear a <=65 C warning. */
        if (i && (temp_low[i-1]&0x80)) c++;
        out->temperatures[i]=c;
        out->temperature_valid[i]=c>=-55 && c<=125 &&
                                  (i==0 || !(temp_config[i-1]&1));
        unsigned shift=i==0 ? 4 : 6;
        unsigned exponent=((i==2 ? div3 : div12)>>shift)&3;
        if (chip==0x30) exponent |= ((div_high>>(5+i))&1)<<2;
        out->fan_valid[i]=raw_fan[i]!=0;
        out->fan_rpm[i]=(raw_fan[i]==0 || raw_fan[i]==255) ? 0 :
                          1350000u / ((uint32_t)raw_fan[i]<<exponent);
    }
}
