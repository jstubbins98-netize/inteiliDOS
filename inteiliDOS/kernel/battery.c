/* Smart Battery Data 1.1: read-word commands at 7-bit address 0x0b.
 * Only firmware-enabled Intel PIIX4/ICH SMBus hosts are supported.
 * No PCI configuration writes, ACPI AML execution, EC probing, charging
 * commands, battery mode writes, SMBus scanning or controller resets.
 */
#include "battery.h"
#include "pci.h"
#include "thermal_hw.h"
#include "timer.h"
#ifdef BATTERY_TEST
#include "../tests/battery_test_io.h"
#else
static uint8_t inb(uint16_t p) {
    uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;
}
static void outb(uint16_t p,uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));
}
static uint32_t irq_lock(void) {
    uint32_t f;__asm__ volatile("pushfl; popl %0; cli":"=r"(f)::"memory");return f;
}
static void irq_unlock(uint32_t f) {
    __asm__ volatile("pushl %0; popfl"::"r"(f):"memory","cc");
}
#endif

static battery_snapshot_t live;
static uint16_t base, words[6];
static unsigned stage, active, disabled;
static uint32_t started, deadline;
static const uint8_t commands[]={0x03,0x0d,0x10,0x18,0x16,0x03};

static int ich_supported(uint16_t id) {
    switch (id) {
    case 0x2413:case 0x2423:case 0x2443:case 0x2483:case 0x24c3:
    case 0x24d3:case 0x266a:case 0x27da:case 0x283e:case 0x2930:
    case 0x3a30:case 0x3b30:case 0x1c22:case 0x1e22:return 1;
    default:return 0;
    }
}

void battery_init(void) {
    live=(battery_snapshot_t){0};
    base=stage=active=disabled=0;deadline=0;
    pci_device_t d;
    if (pci_find_class(0x0c,0x05,&d)==0 && d.vendor_id==0x8086 &&
        ich_supported(d.device_id) && (pci_read16(d.bus,d.device,d.function,4)&1) &&
        (pci_read32(d.bus,d.device,d.function,0x40)&1)) {
        uint32_t bar=pci_read32(d.bus,d.device,d.function,0x20);
        if ((bar&1) && !(bar&0xffff0000u) && (bar&0xffe0)!=0xffe0)
            base=bar&0xffe0;
    }
    if (!base) for (unsigned dev=0;dev<32;dev++) {
        if (pci_read32(0,dev,3,0)!=0x71138086u ||
            !(pci_read16(0,dev,3,4)&1) || !(pci_read16(0,dev,3,0xd2)&1)) continue;
        uint16_t bar=pci_read16(0,dev,3,0x90);
        if ((bar&1) && (bar&0xfff0)!=0xfff0) base=bar&0xfff0;
        if (base) break;
    }
    live.status=base ? BATTERY_WAITING : BATTERY_UNAVAILABLE;
}

static void finish(unsigned status,uint32_t now) {
    active=0;live.status=status;
    if (status!=BATTERY_OK) live.percent_valid=live.health_valid=0;
    deadline=now+5000;
    thermal_hw_bus_release();
}

static void publish(uint32_t now) {
    /* Capacity units are selected by BatteryMode bit 15 and must remain
     * unchanged across the sample. Never combine mAh and 10mWh readings. */
    if (words[0]==0xffff || words[0]!=words[5] || words[4]==0xffff ||
        (words[4]&15) || !(words[4]&0x80)) {
        finish(BATTERY_READ_ERROR,now);return;
    }
    live.percent_valid=words[1]<=100;
    live.percent=words[1];
    live.health_valid=words[2]>0 && words[2]!=0xffff &&
                      words[3]>0 && words[3]!=0xffff;
    live.full_capacity=words[2];live.design_capacity=words[3];
    live.health_percent=live.health_valid ? (uint32_t)words[2]*100/words[3] : 0;
    if (live.health_percent>100) live.health_percent=100;
    live.capacity_energy=(words[0]>>15)&1;
    live.discharging=(words[4]&0x40)!=0;live.fully_charged=(words[4]&0x20)!=0;
    live.sampled_ms=now;finish(BATTERY_OK,now);
}

void battery_poll(uint32_t now) {
    if (!base || disabled) return;
    if (!active) {
        if (live.status!=BATTERY_WAITING && (int32_t)(now-deadline)<0) return;
        uint32_t flags=irq_lock();
        /* Do not take a busy bus or steal interrupt/PEC-enabled transactions. */
        if ((inb(base)&1) || (inb(base+2)&0x81) ||
            !thermal_hw_bus_claim(base)) {irq_unlock(flags);return;}
        active=1;stage=0;started=now;
        outb(base,0x1e);outb(base+3,commands[stage]);
        outb(base+4,0x17); /* battery address | read */
        outb(base+2,0x4c); /* word-data | START, no interrupt */
        irq_unlock(flags);
        return;
    }
    uint8_t status=inb(base);
    if (status&1) {
        if (now-started<20) return;
        /* Stop only our own timed-out transfer, then retire the driver.
         * Do not clear/reset hardware that remains busy. */
        outb(base+2,2);disabled=1;
        finish(BATTERY_READ_ERROR,now);return;
    }
    if ((status&0x1c) || !(status&2)) {
        outb(base,0x1e);finish(BATTERY_READ_ERROR,now);return;
    }
    words[stage]=inb(base+5)|((uint16_t)inb(base+6)<<8);
    outb(base,0x1e);
    if (++stage==6) {publish(now);return;}
    outb(base+3,commands[stage]);outb(base+4,0x17);outb(base+2,0x4c);
    started=now;
}
void battery_snapshot(battery_snapshot_t *out) { if (out) *out=live; }

void battery_shutdown(void) {
    /* Drain only our in-flight sample before leaving the dashboard. Host
     * interrupts remain enabled; timeout handling bounds each transaction. */
    while (active) {
        battery_poll(timer_get_ticks());
#ifndef BATTERY_TEST
        __asm__ volatile("hlt");
#endif
    }
}
