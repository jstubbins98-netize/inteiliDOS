/* Deterministic read-only SMBus register model; no host hardware access. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BATTERY_TEST
#include "../kernel/battery.c"

static uint8_t host[32];
static uint16_t values[6];
static unsigned requests,releases;
static int chipset=1,io_enabled=1,host_enabled=1,claim_ok=1,error,stuck;
static int claimed;
static uint32_t clock_ms;
uint32_t timer_get_ticks(void) {return ++clock_ms;}
static uint32_t irq_lock(void) {return 0;}
static void irq_unlock(uint32_t f) {(void)f;}
int thermal_hw_bus_claim(uint16_t b) {
    assert(b==0x500);if (!claim_ok) return 0;
    assert(!claimed);claimed=1;return 1;
}
void thermal_hw_bus_release(void) {assert(claimed);claimed=0;releases++;}
int pci_find_class(uint8_t c,uint8_t s,pci_device_t *d) {
    assert(c==0x0c && s==5);
    if (chipset!=2) return -1;
    *d=(pci_device_t){.bus=0,.device=31,.function=3,
                     .vendor_id=0x8086,.device_id=0x1c22};return 0;
}
uint32_t pci_read32(uint8_t b,uint8_t d,uint8_t f,uint8_t r) {
    assert(!b && f==3);
    if (chipset==2 && d==31) {
        if (r==0x40) return host_enabled;
        if (r==0x20) return 0x501;
    }
    assert(r==0);return chipset==1 && d==7 ? 0x71138086u : 0xffffffffu;
}
uint16_t pci_read16(uint8_t b,uint8_t d,uint8_t f,uint8_t r) {
    assert(!b && f==3 && (d==7 || d==31));
    if (r==4) return io_enabled;
    if (r==0xd2) return host_enabled;
    assert(r==0x90);return 0x501;
}
static uint8_t inb(uint16_t p) {assert(p>=0x500 && p<0x520);return host[p-0x500];}
static void outb(uint16_t p,uint8_t v) {
    assert(claimed && p>=0x500 && p<=0x504);
    unsigned r=p-0x500;
    if (!r) {assert(v==0x1e);host[0]&=~v;return;}
    host[r]=v;
    if (r==2 && v==2) {host[0]=0;return;}
    if (r!=2) return;
    assert(v==0x4c && host[4]==0x17 && requests<6);
    assert(host[3]==commands[requests]);
    uint16_t w=values[requests++];
    host[5]=w;host[6]=w>>8;
    host[0]=stuck ? 1 : error ? 4 : 2;
}
static void reset(void) {
    memset(host,0,sizeof(host));
    values[0]=values[5]=0;values[1]=75;values[2]=4000;
    values[3]=5000;values[4]=0xc0;
    requests=releases=0;claimed=error=stuck=0;
    claim_ok=io_enabled=host_enabled=1;chipset=1;
}
static battery_snapshot_t sample(void) {
    battery_init();
    for (unsigned i=0;i<8;i++) battery_poll(i);
    battery_snapshot_t s;battery_snapshot(&s);return s;
}
int main(void) {
    reset();battery_snapshot_t s=sample();
    assert(s.status==BATTERY_OK && s.percent_valid && s.percent==75);
    assert(s.health_valid && s.health_percent==80 && s.discharging);
    assert(requests==6 && releases==1 && !claimed);
    requests=0;error=1;battery_poll(5006);battery_poll(5007);
    battery_snapshot(&s);
    assert(s.status==BATTERY_READ_ERROR && !s.percent_valid && !s.health_valid);
    reset();s=sample();
    for (unsigned i=10;i<5000;i++) battery_poll(i);
    assert(requests==6);
    reset();chipset=2;s=sample();assert(s.status==BATTERY_OK);
    reset();battery_init();
    for (unsigned i=0;i<8;i++) battery_poll(0x90000000u+i);
    battery_snapshot(&s);assert(s.status==BATTERY_OK);
    reset();values[0]=values[5]=0x8000;values[2]=5500;
    s=sample();assert(s.health_percent==100 && s.capacity_energy);
    reset();values[1]=0xffff;values[3]=0;
    s=sample();assert(s.status==BATTERY_OK && !s.percent_valid && !s.health_valid);
    reset();values[5]=0x8000;s=sample();assert(s.status==BATTERY_READ_ERROR);
    reset();values[4]=0xffff;s=sample();assert(s.status==BATTERY_READ_ERROR);
    reset();error=1;s=sample();assert(s.status==BATTERY_READ_ERROR && releases==1);
    reset();stuck=1;battery_init();battery_poll(0);battery_poll(21);
    battery_snapshot(&s);assert(s.status==BATTERY_READ_ERROR && !claimed && disabled);
    reset();claim_ok=0;s=sample();assert(s.status==BATTERY_WAITING && !requests);
    reset();host[0]=1;s=sample();assert(s.status==BATTERY_WAITING && !requests);
    reset();host[2]=1;s=sample();assert(s.status==BATTERY_WAITING && !requests);
    reset();chipset=0;s=sample();assert(s.status==BATTERY_UNAVAILABLE && !requests);
    reset();io_enabled=0;s=sample();assert(s.status==BATTERY_UNAVAILABLE && !requests);
    reset();host_enabled=0;s=sample();assert(s.status==BATTERY_UNAVAILABLE && !requests);
    puts("PASS: real battery percentages/capacity ratios, invalid/absent/error/timeout/busy cases; read-only transactions");
}
