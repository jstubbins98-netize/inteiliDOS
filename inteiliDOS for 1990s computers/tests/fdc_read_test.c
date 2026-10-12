/* Host-side 8237/FDC register fixture; no actual port I/O or disk writes. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void fdc_test_outb(uint16_t port,uint8_t value);
static uint8_t fdc_test_inb(uint16_t port);
#define FDC_TEST_IO
#include "../kernel/fdc.c"

static int results[7],result_index,fifo_bytes;
static uint16_t count;
static unsigned count_half;
static uint8_t pic,primary_cmd,secondary_cmd,cascade_mode,cascade_mask,channel_mask;
static int stuck_send,stuck_result;
static uint32_t ticks;

void timer_sleep(uint32_t ms) {(void)ms;}
uint32_t timer_get_ticks(void) {return ticks++;}
static void fdc_test_outb(uint16_t p,uint8_t v) {
    switch(p) {
    case PIC1_MASK:pic=v;break;
    case DMA_COMMAND:primary_cmd=v;break;
    case DMA2_COMMAND:secondary_cmd=v;break;
    case DMA2_MODE:cascade_mode=v;break;
    case DMA2_SINGLE_MASK:cascade_mask=v;break;
    case DMA_SINGLE_MASK:channel_mask=v;break;
    case DMA_RESET_FF:count_half=0;break;
    case FDC_FIFO:
        if (++fifo_bytes==9) {
            /* Without both controllers and cascade channel 4 enabled, an AT
             * cannot deliver channel 2's DMA request. FDC reports overrun. */
            if (primary_cmd || secondary_cmd || cascade_mode!=0xc0 ||
                cascade_mask || channel_mask!=2) {
                results[0]=0x40;results[1]=0x10;count=0x1ff;
            }
        }
        break;
    default:break;
    }
}
static uint8_t fdc_test_inb(uint16_t p) {
    switch(p) {
    case PIC1_MASK:return pic;
    case FDC_MSR:
        if (fifo_bytes<9) return stuck_send ? 0 : 0x80;
        return stuck_result ? 0x80 : 0xc0;
    case FDC_FIFO:
        assert(result_index<7);
        return (uint8_t)results[result_index++];
    case DMA_COUNT_CH2:return count_half++ ? count>>8 : count&0xff;
    default:assert(!"unexpected register read");return 0;
    }
}
static void fixture(int st0,int st1,int st2,uint16_t left) {
    int initial[]={st0,st1,st2,0,0,2,2};
    memcpy(results,initial,sizeof(results));
    result_index=fifo_bytes=0;count=left;count_half=0;
    pic=0xa5;primary_cmd=secondary_cmd=4;
    cascade_mode=0;cascade_mask=4;channel_mask=6;
    stuck_send=stuck_result=0;
    ticks=0;
}
static void check(int st0,int st1,int st2,uint16_t left,int expected) {
    fixture(st0,st1,st2,left);
    assert(fdc_do_read_attempt(0,0,0,1,(uint8_t *)0x70000)==expected);
    assert(pic==0xa5 && channel_mask==6 && result_index==7);
    fdc_read_status_t status;fdc_last_read_status(&status);
    assert(status.st0==st0 && status.st1==st1 && status.st2==st2);
    assert(status.dma_count==left);
}
int main(void) {
    check(0,0,0,0xffff,0);
    check(0x40,4,0,0x1ff,FDC_ERR_ABNORMAL); /* no data */
    check(0x40,0x10,0,0x1ff,FDC_ERR_ABNORMAL); /* DMA overrun */
    check(0x40,0x20,0,0xffff,FDC_ERR_CRC);
    check(0x40,0,0x20,0xffff,FDC_ERR_CRC);
    check(0x80,0,0,0x1ff,FDC_ERR_ABNORMAL);
    check(0x40,0,0x10,0xffff,FDC_ERR_ABNORMAL); /* wrong cylinder */
    check(0,0,0,0x1ff,FDC_ERR_DMA);
    check(0,0,0,0x80,FDC_ERR_DMA); /* short transfer */
    fixture(0,0,0,0xffff);stuck_result=1;
    assert(fdc_do_read_attempt(0,0,0,1,(uint8_t *)0x70000)==FDC_ERR_TIMEOUT);
    assert(pic==0xa5 && channel_mask==6);
    fixture(0,0,0,0xffff);stuck_send=1;
    assert(fdc_do_read_attempt(0,0,0,1,(uint8_t *)0x70000)==FDC_ERR_TIMEOUT);
    assert(pic==0xa5 && channel_mask==6);
    fixture(0,0,0,0xffff);
    assert(fdc_do_read_attempt(0,0,0,1,(uint8_t *)0x1000000)==FDC_ERR_DMA);
    assert(!fifo_bytes && pic==0xa5);
    assert(fdc_do_read_attempt(0,0,0,1,(uint8_t *)0x7ff00)==FDC_ERR_DMA);
    puts("PASS: AT DMA cascade setup, full-transfer check, abnormal/CRC/timeout rejection, address bounds, diagnostics and PIC restoration");
}
