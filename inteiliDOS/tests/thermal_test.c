/* Real service and SMBus driver against a deterministic register model.
 * gcc -O1 -fsanitize=address,undefined -I/tmp/thermal-config tests/thermal_test.c
 *     -o /tmp/thermal-test */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define THERMAL_TEST
#define THERMAL_HW_TEST
#include "../kernel/thermal_hw.c"
#include "../kernel/thermal.c"

static int present=1, enabled=1, monitor_present=1, hang, inject_stage=-1;
static uint8_t host[16], regs[3][256], selected_bank=0x80, ident=0x10;
static uint8_t manufacturer_low=0xA3, manufacturer_high=0x5C;
static unsigned writes, transactions, kills, forbidden_writes;

uint32_t pci_read32(uint8_t bus,uint8_t dev,uint8_t fn,uint8_t reg) {
    assert(bus==0 && fn==3 && reg==0);
    return present && dev==7 ? 0x71138086u : 0xFFFFFFFFu;
}
uint16_t pci_read16(uint8_t bus,uint8_t dev,uint8_t fn,uint8_t reg) {
    assert(bus==0 && dev==7 && fn==3);
    if (reg==4 || reg==0xD2) return enabled ? 1 : 0;
    assert(reg==0x90);return 0x501;
}
static uint8_t read_monitor(uint8_t reg) {
    if (reg==0x4E) return selected_bank;
    if (reg==0x4F) return selected_bank&0x80 ? manufacturer_high : manufacturer_low;
    if (reg==0x58 && !(selected_bank&7)) return ident;
    return regs[selected_bank&7][reg];
}
static uint8_t inb(uint16_t p) {
    assert(p>=0x500 && p<0x510);
    return host[p-0x500];
}
static void outb(uint16_t p,uint8_t v) {
    assert(p>=0x500 && p<0x510);
    unsigned reg=p-0x500;
    if (!reg) {host[0]&=~v;return;}
    host[reg]=v;
    if (reg!=2) return;
    if (v==2) {kills++;host[0]=0;return;}
    if (!(v&0x40)) return;
    assert(v==0x48); /* no IRQ-enable or non-byte-data command */
    assert((host[4]&0xFE)==0x5A);
    transactions++;
    if (hang) {host[0]=1;return;}
    if (!monitor_present || stage==inject_stage) {host[0]=4;return;}
    if (host[4]&1) host[5]=read_monitor(host[3]);
    else {
        writes++;
        if (host[3]!=0x4E) forbidden_writes++;
        assert(host[3]==0x4E); /* NEVER changes fan or sensor configuration */
        selected_bank=host[5];
    }
    host[0]=2;
}
static void reset(void) {
    present=enabled=monitor_present=1;hang=0;inject_stage=-1;
    writes=transactions=kills=forbidden_writes=0;
    selected_bank=0x80;ident=0x10;manufacturer_low=0xA3;manufacturer_high=0x5C;
    memset(host,0,sizeof(host));memset(regs,0,sizeof(regs));
    regs[0][0x40]=1;regs[0][0x47]=0x90;regs[0][0x4B]=0x40;
    regs[0][0x27]=35;regs[1][0x50]=40;regs[2][0x50]=50;
    regs[0][0x28]=100;regs[0][0x29]=150;regs[0][0x2A]=255;
    thermal_init();
}
static thermal_snapshot_t snapshot(void) {
    thermal_snapshot_t s;thermal_snapshot(&s);return s;
}
static void ticks(uint32_t start,uint32_t end) {
    for (uint32_t t=start;t<end;t++) thermal_tick(t);
}
int main(void) {
    reset();
    assert(thermal_hw_bus_claim(0x500));
    ticks(0,100);
    assert(snapshot().status==THERMAL_WAITING && !transactions);
    thermal_hw_bus_release();ticks(100,540);
    assert(snapshot().status==THERMAL_OK);
    reset();thermal_tick(0);
    assert(!thermal_hw_bus_claim(0x500)); /* don't interrupt an active sample */
    reset();ticks(0,100);
    thermal_snapshot_t s=snapshot();
    assert(s.status==THERMAL_OK && s.samples==1 && s.checks==1);
    assert(s.temperatures[2]==50 && s.cpu_channel==0);
    assert(s.fan_rpm[0]==6750 && s.fan_rpm[1]==2250);
    assert(s.fan_valid[2] && s.fan_rpm[2]==0);
    assert(selected_bank==0x80 && !forbidden_writes);
    ticks(100,500);assert(snapshot().checks==1);
    ticks(500,540);assert(snapshot().checks==2 && snapshot().samples==2);
    regs[2][0x50]=75;ticks(540,1040);assert(snapshot().cooling_needed);
    regs[2][0x50]=70;ticks(1040,1540);assert(snapshot().cooling_needed);
    regs[2][0x50]=65;regs[2][0x51]=0x80;ticks(1540,2040);
    assert(snapshot().temperatures[2]==66 && snapshot().cooling_needed);
    regs[2][0x51]=0;regs[1][0x52]=1;ticks(2040,2540);
    assert(!snapshot().temperature_valid[1] && snapshot().cooling_needed);
    monitor_present=0;ticks(2540,3040);
    assert(snapshot().status==THERMAL_READ_ERROR && snapshot().cooling_needed);
    monitor_present=1;regs[1][0x52]=0;ticks(3040,3540);
    assert(!snapshot().cooling_needed && snapshot().status==THERMAL_OK);

    /* Both AS99127F vendor codes / low byte bank selection supported. */
    reset();ident=0x31;selected_bank=0;
    manufacturer_high=0x12;manufacturer_low=0xC3;ticks(0,40);
    assert(snapshot().status==THERMAL_OK && selected_bank==0);
    assert(strstr(snapshot().sensor,"AS99127F"));
    reset();ident=0x30;regs[0][0x5D]=0xE0;ticks(0,40);
    assert(snapshot().fan_rpm[0]==421); /* divisor 2^(1+4) */
    /* Invalid vendor must be rejected BEFORE ANY bank/config writes. */
    reset();manufacturer_high=0xFF;ticks(0,40);
    assert(snapshot().status==THERMAL_READ_ERROR && !writes);
    /* Unknown chip and disabled monitor restore firmware's original bank. */
    reset();ident=0x99;ticks(0,40);
    assert(snapshot().status==THERMAL_READ_ERROR && selected_bank==0x80);
    reset();regs[0][0x40]=0;ticks(0,40);
    assert(snapshot().status==THERMAL_READ_ERROR && selected_bank==0x80);
    reset();inject_stage=15;ticks(0,40);
    assert(snapshot().status==THERMAL_READ_ERROR && selected_bank==0x80);
    /* Our own timeout can be killed, but a firmware-owned busy bus cannot. */
    reset();hang=1;ticks(0,100);assert(kills==1 && snapshot().status==THERMAL_READ_ERROR);
    reset();host[0]=1;ticks(0,100);
    assert(!kills && !transactions && snapshot().status==THERMAL_READ_ERROR);
    reset();present=0;thermal_init();ticks(0,1001);
    assert(snapshot().status==THERMAL_UNAVAILABLE && snapshot().checks==3 && !transactions);
    reset();enabled=0;thermal_init();ticks(0,100);
    assert(snapshot().status==THERMAL_UNAVAILABLE && !transactions);
    /* Counter wrap: one sample per interval, not a catch-up burst. */
    reset();deadline=0xFFFFFFF0u;
    thermal_tick(0xFFFFFFEFu);assert(!snapshot().checks);
    ticks(0xFFFFFFF0u,0xFFFFFFFFu);ticks(0,40);
    assert(snapshot().checks==1 && snapshot().samples==1);
    thermal_tick(0x1E3u);assert(snapshot().checks==1);
    thermal_tick(0x1E4u);assert(snapshot().checks==2);
    puts("PASS: 500ms background cadence, hysteresis, fractional readings, stale/failure handling, chip IDs, fan RPM, bank restoration, bounded transfers, disabled/absent hardware, wrap; no fan writes");
}
