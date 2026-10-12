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
static uint8_t cmos_index=0x8b,cmos_types;
static int sense_mode;

void timer_sleep(uint32_t ms) {(void)ms;}
uint32_t timer_get_ticks(void) {return ticks++;}
static void fdc_test_outb(uint16_t p,uint8_t v) {
    switch(p) {
    case 0x70:cmos_index=v;break;
    case PIC1_MASK:pic=v;break;
    case DMA_COMMAND:primary_cmd=v;break;
    case DMA2_COMMAND:secondary_cmd=v;break;
    case DMA2_MODE:cascade_mode=v;break;
    case DMA2_SINGLE_MASK:cascade_mask=v;break;
    case DMA_SINGLE_MASK:channel_mask=v;break;
    case DMA_RESET_FF:count_half=0;break;
    case FDC_FIFO:
        if (sense_mode) {assert(v==CMD_SENSE_INT);fifo_bytes=1;result_index=0;break;}
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
    case 0x70:return cmos_index;
    case 0x71:assert((cmos_index&0x7f)==0x10);return cmos_types;
    case PIC1_MASK:return pic;
    case FDC_MSR:
        if (sense_mode)
            return fifo_bytes && result_index<(results[0]==0x80 ? 1 : 2) ? 0xd0 : 0x80;
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
    sense_mode=0;
    geometry[0]=geometry[1]=(floppy_geometry_t){2880,18,0,1,0};
}
static void test_geometry(unsigned sectors,unsigned spt,unsigned type,unsigned step) {
    uint8_t boot[512]={0};
    boot[11]=0;boot[12]=2;boot[19]=sectors;boot[20]=sectors>>8;
    boot[24]=spt;boot[26]=2;cmos_types=type<<4;cmos_index=0x8b;
    assert(!fdc_accept_geometry(0,boot,2));
    assert(geometry[0].sectors==sectors && geometry[0].spt==spt);
    assert(geometry[0].step==step && geometry[0].rate==2 && geometry[0].known);
    assert(cmos_index==0x8b); /* preserve NMI and RTC register selection */
    boot[24]=10;assert(fdc_accept_geometry(0,boot,2)==FDC_ERR_FORMAT);
    boot[24]=spt;boot[11]=1;assert(fdc_accept_geometry(0,boot,2)==FDC_ERR_FORMAT);
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
    test_geometry(720,9,1,1);
    test_geometry(720,9,2,2); /* 360K disk in an 80-track 5.25" HD drive */
    test_geometry(720,9,4,1);
    test_geometry(1440,9,4,1);
    test_geometry(2400,15,2,1);
    test_geometry(2880,18,4,1);
    fixture(0,0,0,0xffff);sense_mode=1;results[0]=0x20;results[1]=28;
    fdc_cur_track[0]=0xff;assert(!fdc_wait_seek(0,28) && fdc_cur_track[0]==28);
    fixture(0,0,0,0xffff);sense_mode=1;results[0]=0x70;results[1]=0;
    fdc_cur_track[0]=0xff;assert(fdc_wait_seek(0,0)==-1 && fdc_cur_track[0]==0xff);
    fixture(0,0,0,0xffff);sense_mode=1;results[0]=0x20;results[1]=1;
    assert(fdc_wait_seek(0,0)==-1);
    fixture(0,0,0,0xffff);sense_mode=1;results[0]=0x80;
    assert(fdc_wait_seek(0,0)==-1); /* no pending completion */
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
    puts("PASS: 360/720/1200/1440K geometry, double stepping, seek completion, DMA cascade/full transfer, errors, bounds and PIC restoration");
}
