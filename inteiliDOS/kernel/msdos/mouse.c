/* Resident INT 33h-compatible mouse services. No guest I/O touches native ports.
 * The displayed cursor is an overlay, never a write into guest video memory. */
#include "mouse.h"
#include "../mouse_input.h"
static struct {
    int enabled, installed, hidden, cursor_visible, x, y, minx, maxx, miny, maxy;
    int ratio_x, ratio_y, remainder_x, remainder_y;
    uint16_t buttons, mickey_x, mickey_y, events;
    uint16_t pressed[3], released[3], px[3], py[3], rx[3], ry[3];
    uint16_t callback_mask, callback_segment, callback_offset;
    uint16_t text_screen, text_cursor, screen[16], cursor[16];
    uint16_t hotx, hoty, sensitivity_x, sensitivity_y, threshold;
    uint8_t mode;
} m;
static int clamp(int n,int lo,int hi) {return n<lo?lo:n>hi?hi:n;}
static int max_y(uint8_t mode) {
    return mode==0x0f || mode==0x10?349:
           mode==0x11 || mode==0x12?479:199;
}
static uint16_t word(const uint8_t *ram,unsigned p) {
    return ram[p&0xfffff]|((uint16_t)ram[(p+1)&0xfffff]<<8);
}
static void reset(void) {
    int enabled=m.enabled,installed=m.installed,visible=m.cursor_visible;
    m=(typeof(m)){0};m.enabled=enabled;m.installed=installed;
    m.cursor_visible=visible; /* DOS reset must not override the user's F12 choice */
    m.hidden=1;m.maxx=639;m.maxy=199;m.x=320;m.y=100;
    m.ratio_x=8;m.ratio_y=16;m.mode=3;
    m.sensitivity_x=m.sensitivity_y=50;m.threshold=64;
    m.text_screen=0xffff;m.text_cursor=0x7700;
    /* Small high-contrast arrow; programs may replace both 16x16 masks. */
    for (unsigned i=0;i<16;i++) {
        unsigned bits=i<10?0xffffu<<(15-i):0;
        m.screen[i]=(uint16_t)~bits;
        m.cursor[i]=(uint16_t)(i<9?(0x8000u>>i):0);
    }
}
int msdos_mouse_start(int requested) {
    m=(typeof(m)){0};
    if (!requested) return 0;
    m.enabled=mouse_input_start();
    reset();
    return m.enabled;
}
int msdos_mouse_enabled(void) {return m.enabled && m.installed;}
void msdos_mouse_toggle_cursor(void) {
    if (msdos_mouse_enabled()) m.cursor_visible=!m.cursor_visible;
}
void msdos_mouse_stop(void) {
    if (m.enabled) mouse_input_stop();
    m.enabled=m.installed=0;
}
void msdos_mouse_install(uint8_t *ram) {
    if (!m.enabled || m.installed) return;
    /* DOS rewrites reserved vectors during initialization. Called at its
     * first EXEC, after those writes. Keep later guest replacement/chaining legal. */
    ram[0x33*4]=MSDOS_MOUSE_ENTRY&255;ram[0x33*4+1]=MSDOS_MOUSE_ENTRY>>8;
    ram[0x33*4+2]=0;ram[0x33*4+3]=0xf0;
    ram[0xf0000+MSDOS_MOUSE_ENTRY]=0xcf; /* valid IRET/probe signature */
    ram[0xf0000+MSDOS_MOUSE_RETURN]=0x90;
    m.installed=1;
}
void msdos_mouse_poll(uint8_t mode) {
    if (!m.enabled) return;
    if (mode!=m.mode) {
        m.mode=mode;m.minx=m.miny=0;m.maxx=639;
        m.maxy=max_y(mode);
        m.hidden=1;m.x=clamp(m.x,0,m.maxx);m.y=clamp(m.y,0,m.maxy);
    }
    int dx,dy;unsigned buttons;
    mouse_input_poll(&dx,&dy,&buttons);
    m.mickey_x=(uint16_t)(m.mickey_x+dx);
    m.mickey_y=(uint16_t)(m.mickey_y+dy);
    m.remainder_x+=dx*8*m.sensitivity_x;m.remainder_y+=dy*8*m.sensitivity_y;
    m.x=clamp(m.x+m.remainder_x/(m.ratio_x*50),m.minx,m.maxx);
    m.y=clamp(m.y+m.remainder_y/(m.ratio_y*50),m.miny,m.maxy);
    m.remainder_x%=m.ratio_x*50;m.remainder_y%=m.ratio_y*50;
    if (dx || dy) m.events|=1;
    unsigned changed=m.buttons^buttons;
    for (unsigned b=0;b<3;b++) {
        if (!(changed&(1u<<b))) continue;
        if (buttons&(1u<<b)) {
            m.pressed[b]++;m.px[b]=m.x;m.py[b]=m.y;
            m.events|=2u<<(2*b);
        } else {
            m.released[b]++;m.rx[b]=m.x;m.ry[b]=m.y;
            m.events|=4u<<(2*b);
        }
    }
    m.buttons=buttons&7;
}
void msdos_mouse_bios(uint16_t *r,const uint8_t *ram) {
    /* AX,CX,DX,BX,SP,BP,SI,DI,ES,CS,SS,DS matches the interpreter. */
    unsigned button=r[3],function=r[0];
    switch (function) {
    case 0:case 0x21:
        reset();r[0]=0xffff;r[3]=3;break;
    case 1:if (m.hidden>0) --m.hidden;break;
    case 2:if (m.hidden<32767) ++m.hidden;break;
    case 3:r[3]=m.buttons;r[1]=m.x;r[2]=m.y;break;
    case 4:
        m.x=clamp((int16_t)r[1],m.minx,m.maxx);
        m.y=clamp((int16_t)r[2],m.miny,m.maxy);
        m.remainder_x=m.remainder_y=0;break;
    case 5:case 6:
        r[0]=m.buttons;r[3]=r[1]=r[2]=0;
        if (button<3) {
            if (function==5) {
                r[3]=m.pressed[button];m.pressed[button]=0;
                r[1]=m.px[button];r[2]=m.py[button];
            } else {
                r[3]=m.released[button];m.released[button]=0;
                r[1]=m.rx[button];r[2]=m.ry[button];
            }
        }break;
    case 7:case 8: {
        int lo=(int16_t)r[1],hi=(int16_t)r[2];
        if (lo>hi) {int t=lo;lo=hi;hi=t;}
        /* Native presenter coordinates are never allowed outside the raster. */
        int limit=function==7?639:max_y(m.mode);
        lo=clamp(lo,0,limit);hi=clamp(hi,0,limit);
        if (function==7) {m.minx=lo;m.maxx=hi;m.x=clamp(m.x,lo,hi);}
        else {m.miny=lo;m.maxy=hi;m.y=clamp(m.y,lo,hi);}
        break;
    }
    case 9: {
        unsigned p=((unsigned)r[8]<<4)+r[2];
        m.hotx=r[3]&15;m.hoty=r[1]&15;
        for (unsigned i=0;i<16;i++) {
            m.screen[i]=word(ram,p+2*i);m.cursor[i]=word(ram,p+32+2*i);
        }break;
    }
    case 10:
        if (!r[3]) {m.text_screen=r[1];m.text_cursor=r[2];}
        break;
    case 11:
        r[1]=m.mickey_x;r[2]=m.mickey_y;m.mickey_x=m.mickey_y=0;break;
    case 12:case 0x14: {
        uint16_t mask=m.callback_mask,seg=m.callback_segment,off=m.callback_offset;
        m.callback_mask=r[1]&127;m.callback_segment=r[8];m.callback_offset=r[2];
        m.events=0;
        if (function==0x14) {r[1]=mask;r[8]=seg;r[2]=off;}
        break;
    }
    case 15:
        if (r[1] && r[1]<=32767) m.ratio_x=r[1];
        if (r[2] && r[2]<=32767) m.ratio_y=r[2];
        m.remainder_x=m.remainder_y=0;break;
    case 0x1a:
        m.sensitivity_x=clamp(r[3],1,100);
        m.sensitivity_y=clamp(r[1],1,100);m.threshold=r[2];break;
    case 0x1b:
        r[3]=m.sensitivity_x;r[1]=m.sensitivity_y;r[2]=m.threshold;break;
    case 0x15:r[3]=0;break; /* driver-state serialization is not implemented */
    case 0x24:
        r[3]=0x0600;r[1]=0x0400;break; /* v6.00, PS/2-compatible API, no guest IRQ */
    default:break; /* no claim of unimplemented save/restore or wheel extensions */
    }
}
int msdos_mouse_event(uint16_t *r,uint16_t *segment,uint16_t *offset) {
    unsigned events=m.events&m.callback_mask;
    m.events=0;
    if (!m.enabled || !events || !(m.callback_segment|m.callback_offset)) return 0;
    r[0]=events;r[3]=m.buttons;r[1]=m.x;r[2]=m.y;
    r[6]=m.mickey_x;r[7]=m.mickey_y;
    *segment=m.callback_segment;*offset=m.callback_offset;
    return 1;
}
uint16_t msdos_mouse_text(unsigned cell,uint16_t value) {
    if (!m.enabled || !m.cursor_visible || m.hidden ||
        cell!=(unsigned)(m.y/8*80+m.x/8)) return value;
    return (value&m.text_screen)^m.text_cursor;
}
uint8_t msdos_mouse_pixel(unsigned x,unsigned y,unsigned width,uint8_t value) {
    if (!m.enabled || !m.cursor_visible || m.hidden) return value;
    int px=m.x/(width==320?2:1)-m.hotx,py=m.y-m.hoty;
    int cx=(int)x-px,cy=(int)y-py;
    if (cx<0 || cx>=16 || cy<0 || cy>=16) return value;
    unsigned bit=0x8000u>>cx;
    return (value&(m.screen[cy]&bit?255:0))^(m.cursor[cy]&bit?15:0);
}
