/* Deterministic IRQ-handler checks, including the complete 4-second PCM
 * stream. Does not execute privileged HLT/STI or fake a physical sensor. */
#include <assert.h>
#include <stdio.h>
#define THERMAL_TIMER_TEST
#include "../kernel/timer.c"

static unsigned thermal_calls, secondary_calls, output_writes;
static uint32_t last_thermal_tick;
static uint8_t inb(uint16_t port) {(void)port;return 0;}
static void outb(uint16_t port,uint8_t value) {
    (void)value;assert(port==0x43 || port==0x42);output_writes++;
}
void isr_register_handler(int n,isr_handler_t h) {(void)n;(void)h;}
void thermal_tick(uint32_t now) {
    assert(now==last_thermal_tick+1);
    last_thermal_tick=now;thermal_calls++;
}
static void secondary(void) {secondary_calls++;}
int main(void) {
    registers_t regs={0};
    timer_register_secondary(secondary);
    for (unsigned i=0;i<1000;i++) timer_handler(&regs);
    assert(ticks==1000 && thermal_calls==1000 && secondary_calls==1000);
    unsigned before=thermal_calls;
    static const unsigned char buffer[88200]={0};
    pcm_data=buffer;pcm_total=sizeof(buffer);
    pcm_pos=pcm_done=pcm_clock_accum=0;
    /* The PCM caller suspends the USB callback but NOT thermal monitoring. */
    timer_secondary_cb=0;
    for (unsigned i=0;i<=sizeof(buffer);i++) pcm_irq_handler(&regs);
    assert(pcm_done && pcm_pos==sizeof(buffer));
    unsigned expected=((uint64_t)(sizeof(buffer)+1)*PCM_DIV*1000)/PIT_BASE_HZ;
    assert(thermal_calls-before==expected && expected>=3900);
    assert(ticks==1000+expected);
    assert(secondary_calls==1000 && output_writes==3*sizeof(buffer));
    timer_register_secondary(secondary);
    timer_handler(&regs);
    assert(secondary_calls==1001 && thermal_calls==1001+expected);
    puts("PASS: host timer callback, unchanged USB callback, thermal millisecond cadence during complete PCM IRQ stream and normal-timer restoration");
}
