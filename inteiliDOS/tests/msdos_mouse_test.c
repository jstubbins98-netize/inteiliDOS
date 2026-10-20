/* Pure INT33 state tests: no physical ports, display, or licensed DOS needed. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/msdos/mouse.h"
static int next_x,next_y,starts,stops,available=1;
static unsigned next_buttons;
int mouse_input_start(void) {++starts;return available;}
void mouse_input_stop(void) {++stops;}
void mouse_input_poll(int *x,int *y,unsigned *b) {
    *x=next_x;*y=next_y;*b=next_buttons;next_x=next_y=0;
}
static uint8_t ram[0x110000];
static uint16_t r[12];
static void call(unsigned ax,unsigned bx,unsigned cx,unsigned dx) {
    r[0]=ax;r[3]=bx;r[1]=cx;r[2]=dx;msdos_mouse_bios(r,ram);
}
int main(void) {
    assert(!msdos_mouse_start(0) && starts==0);
    msdos_mouse_install(ram);assert(ram[0xcc]==0);
    available=0;assert(!msdos_mouse_start(1));msdos_mouse_stop();assert(stops==0);
    available=1;assert(msdos_mouse_start(1));
    msdos_mouse_install(ram);
    assert(msdos_mouse_enabled() && ram[0xcc]==0 && ram[0xcd]==0x30 &&
           ram[0xcf]==0xf0 && ram[0xf3000]==0xcf);
    call(0,0,0,0);assert(r[0]==0xffff && r[3]==3);
    call(3,0,0,0);assert(r[1]==320 && r[2]==100);
    assert(msdos_mouse_text(1000,0x0741)==0x0741);
    call(1,0,0,0);
    assert(msdos_mouse_text(1000,0x0741)==0x0741); /* DOS show cannot bypass F12 */
    msdos_mouse_toggle_cursor();
    assert(msdos_mouse_text(1000,0x0741)==0x7041);
    call(2,0,0,0);call(2,0,0,0);call(1,0,0,0);
    assert(msdos_mouse_text(1000,0x0741)==0x0741); /* nested hide */
    call(1,0,0,0);
    call(7,0,20,100);call(8,0,10,80);
    call(4,0,639,199);call(3,0,0,0);assert(r[1]==100 && r[2]==80);
    call(7,0,639,0);call(8,0,199,0);call(4,0,200,100);
    call(15,0,8,8);
    msdos_mouse_toggle_cursor(); /* input and button counts still work hidden */
    next_x=-10;next_y=4;next_buttons=5;msdos_mouse_poll(3);
    call(3,0,0,0);assert(r[1]==190 && r[2]==104 && r[3]==5);
    call(5,0,0,0);assert(r[0]==5 && r[3]==1 && r[1]==190 && r[2]==104);
    call(5,0,0,0);assert(r[3]==0);
    call(5,2,0,0);assert(r[3]==1);
    call(11,0,0,0);assert((int16_t)r[1]==-10 && r[2]==4);
    call(11,0,0,0);assert(!r[1] && !r[2]);
    r[8]=0x1234;call(12,0,127,0x5678);
    next_buttons=0;msdos_mouse_poll(3);
    uint16_t seg,off;
    assert(msdos_mouse_event(r,&seg,&off) && r[0]==0x44 &&
           seg==0x1234 && off==0x5678);
    assert(!msdos_mouse_event(r,&seg,&off));
    call(6,2,0,0);assert(r[3]==1 && r[1]==190);
    call(6,99,0,0);assert(r[3]==0);
    call(15,0,0,0); /* zero ratios ignored, never divide by zero */
    next_x=32767;next_y=-32767;msdos_mouse_poll(3);
    call(3,0,0,0);assert(r[1]==639 && r[2]==0);
    call(0,0,0,0);
    msdos_mouse_poll(0x12);call(4,0,600,400);
    call(3,0,0,0);assert(r[1]==600 && r[2]==400);
    call(1,0,0,0);
    assert(msdos_mouse_pixel(600,400,640,7)==7); /* reset/mode change stay gated */
    msdos_mouse_toggle_cursor();
    assert(msdos_mouse_pixel(600,400,640,7)!=7);
    assert(msdos_mouse_pixel(599,400,640,7)==7);
    r[8]=0x1000;
    for (unsigned i=0;i<32;i++) ram[0x10000+i]=0xff;
    memset(ram+0x10020,0,32);call(9,0,0,0);
    assert(msdos_mouse_pixel(600,400,640,7)==7); /* transparent custom mask */
    call(0x1a,100,25,20);call(0x1b,0,0,0);
    assert(r[3]==100 && r[1]==25 && r[2]==20);
    msdos_mouse_stop();assert(!msdos_mouse_enabled() && stops==1);
    assert(msdos_mouse_text(1040,0x0741)==0x0741);
    assert(msdos_mouse_start(1));msdos_mouse_install(ram);call(3,0,0,0);
    assert(r[1]==320 && r[2]==100 && r[3]==0); /* no stale session state */
    call(1,0,0,0);
    assert(msdos_mouse_text(1000,0x0741)==0x0741); /* new session starts hidden */
    msdos_mouse_stop();
    puts("PASS: DOS mouse state, F12 display gate, hidden input, cursor masks, re-entry");
}
