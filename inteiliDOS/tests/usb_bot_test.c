/* Wire-level BOT validation shared by both host-controller drivers. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/usb_bot.h"
static unsigned step,fail_step,bad_signature,bad_tag,residue,status;
static int direction;
static uint8_t payload[512];
static int bulk(void *dev,uint8_t *b,unsigned n,int write) {
    assert(dev==payload);step++;
    if (step==fail_step) return -1;
    if (step==1) {
        assert(n==31 && write && !memcmp(b,"USBC",4));
        assert(b[4]==42 && b[8]==0 && b[9]==2);
        assert(b[12]==(direction ? 0 : 0x80) && b[14]==10 && b[15]==0x28);
    } else if (step==2) {
        assert(n==512 && write==direction);
        if (write) assert(!memcmp(b,payload,512));else memcpy(b,payload,512);
    } else {
        assert(step==3 && n==13 && !write);
        memset(b,0,13);memcpy(b,bad_signature ? "FAIL" : "USBS",4);
        b[4]=bad_tag ? 41 : 42;
        b[8]=residue;b[9]=residue>>8;b[12]=status;
    }
    return 0;
}
void *kmemset(void *p,int c,size_t n) {return memset(p,c,n);}
void *kmemcpy(void *p,const void *q,size_t n) {return memcpy(p,q,n);}
int main(void) {
    uint8_t scratch[512],saved[512],out[512],cdb[10]={0x28};
    for(unsigned i=0;i<512;i++) payload[i]=(i*37+11)&255;
    for(direction=0;direction<=1;direction++) {
        for(unsigned fault=0;fault<11;fault++) {
            step=0;fail_step=fault<=3 ? fault : 0;
            bad_signature=fault==4;bad_tag=fault==5;residue=fault==6;
            status=fault==7 ? 2 : fault==8 ? 1 : 0;
            if(fault==9 || fault==10) {status=1;residue=fault==9 ? 512 : 513;}
            if(direction)memcpy(out,payload,512);else memset(out,0xA5,512);
            int result=usb_bot_command(bulk,payload,42,scratch,saved,cdb,10,out,512,direction);
            assert(result==(fault==8 || fault==9 ? -2 : fault ? -1 : 0));
            if(!fault) assert(!memcmp(out,payload,512));
            if(fault && !direction) for(unsigned i=0;i<512;i++) assert(out[i]==0xA5);
        }
    }
    step=0;
    assert(usb_bot_command(bulk,payload,42,scratch,saved,cdb,17,out,512,0)==-1 && !step);
    assert(usb_bot_command(bulk,payload,42,scratch,saved,cdb,10,out,513,0)==-1 && !step);
    puts("PASS: shared BOT wire bytes, read/write, CBW/data/CSW failures, signature/tag/"
         "residue/status validation and failed-read destination preservation");
}
