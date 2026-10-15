#include "../kernel/usb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t injected[32];
static unsigned count;
void keyboard_inject(uint8_t c) { assert(count<sizeof(injected));injected[count++]=c; }
static void press(uint8_t modifier,uint8_t key,uint8_t expected) {
    uint8_t report[8]={modifier,0,key},previous[8]={0};
    unsigned before=count;
    assert(usb_process_boot_report(report,previous));
    assert(count==before+1 && injected[before]==expected);
    assert(usb_process_boot_report(report,report) && count==before+1);
}
int main(void) {
    press(0,4,'a');press(2,4,'A');
    uint8_t caps[8]={0,0,0x39},zero[8]={0};
    assert(usb_process_boot_report(caps,zero));
    press(0,4,'A');press(2,4,'a');press(1,6,3);
    press(2,0x28,'\r');press(2,0x41,0x97);
    for (uint8_t i=0;i<12;i++) press(0,0x3A+i,0x90+i);
    press(0,0x4F,0x83);press(0,0x50,0x82);
    press(0,0x51,0x81);press(0,0x52,0x80);
    uint8_t rollover[8]={0,0,1};
    unsigned before=count;
    assert(!usb_process_boot_report(rollover,zero) && count==before);
    uint8_t cfg[]={9,2,25,0,1,5,0,0x80,50,
                   9,4,2,0,1,3,1,1,0,7,5,0x82,3,8,0,10};
    usb_keyboard_descriptor_t info;
    assert(!usb_parse_boot_keyboard(cfg,sizeof(cfg),&info));
    assert(info.configuration==5 && info.interface==2 && info.endpoint==2);
    assert(info.packet_size==8 && info.interval==10);
    cfg[16]=2;assert(usb_parse_boot_keyboard(cfg,sizeof(cfg),&info)); /* mouse */
    cfg[16]=1;cfg[12]=1;assert(usb_parse_boot_keyboard(cfg,sizeof(cfg),&info));
    cfg[12]=0;cfg[19]=0;assert(usb_parse_boot_keyboard(cfg,sizeof(cfg),&info));
    cfg[19]=5;assert(usb_parse_boot_keyboard(cfg,sizeof(cfg)-1,&info));
    cfg[22]=7;assert(usb_parse_boot_keyboard(cfg,sizeof(cfg),&info));
    puts("PASS: USB boot descriptors, mouse rejection, malformed descriptors, caps/shift/control, Enter/F1-F12/arrows, held keys and rollover");
}
