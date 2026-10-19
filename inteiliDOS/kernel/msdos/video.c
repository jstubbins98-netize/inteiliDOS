/* EGA/VGA register, latch, memory and BIOS emulation. MPL-2.0.
 * All addresses refer to guest RAM, never native VGA registers or memory. */
#include "video.h"
#include "../memory.h"

static struct {
    uint8_t (*plane)[65536], latch[4], seq[8], gc[16], crtc[32], ac[32];
    uint8_t dac[768], si, gi, ci, ai, flip, enabled, mask, dr, dw, dc, dac_state;
    uint8_t misc, status, mode, active;
    uint8_t *ram;
    unsigned page_size, char_height;
    uint32_t palette_version;
} v;

static unsigned word(unsigned p) { return v.ram[p] | ((unsigned)v.ram[p+1]<<8); }
static void putword(unsigned p,unsigned n) { v.ram[p]=n;v.ram[p+1]=n>>8; }
static uint8_t rotate(uint8_t b,unsigned n) {
    n&=7;return n ? (uint8_t)((b>>n)|(b<<(8-n))) : b;
}
static uint8_t logic(uint8_t a,uint8_t b) {
    switch ((v.gc[3]>>3)&3) {
    case 1:return a&b;case 2:return a|b;case 3:return a^b;default:return a;
    }
}
static void default_palette(void) {
    for (unsigned i=0;i<256;i++) {
        unsigned r,g,b;
        if (i<64) {
            r=(i&4?42:0)+(i&32?21:0);
            g=(i&2?42:0)+(i&16?21:0);
            b=(i&1?42:0)+(i&8?21:0);
        } else r=g=b=0;
        v.dac[3*i]=r;v.dac[3*i+1]=g;v.dac[3*i+2]=b;
    }
    for (unsigned i=0;i<16;i++) {
        unsigned a=i==6?20:i<8?i:i+48;
        v.ac[i]=a;
    }
    /* The first 16 DAC entries in mode 13h are the IBM 16-color palette. */
    if (v.mode==0x13) {
        for (unsigned i=0;i<16;i++) {
            unsigned c=i==6?20:i<8?i:i+48;
            uint8_t rgb[3]={v.dac[c*3],v.dac[c*3+1],v.dac[c*3+2]};
            v.dac[i*3]=rgb[0];v.dac[i*3+1]=rgb[1];v.dac[i*3+2]=rgb[2];
        }
        static const uint8_t gray[16]={0,5,8,11,14,17,20,24,28,32,36,40,45,50,56,63};
        static const uint8_t levels[9][5]={
            {0,16,31,47,63},{31,39,47,55,63},{45,49,54,58,63},
            {0,7,14,21,28},{14,17,21,24,28},{20,22,24,26,28},
            {0,4,8,12,16},{8,10,12,14,16},{11,12,13,15,16}
        };
        for (unsigned i=0;i<16;i++)
            v.dac[(16+i)*3]=v.dac[(16+i)*3+1]=v.dac[(16+i)*3+2]=gray[i];
        /* IBM-compatible 24-hue rings, not a generic RGB cube. */
        for (unsigned ring=0;ring<9;ring++)
            for (unsigned n=0;n<24;n++) {
                unsigned phase=n/4,t=n%4;
                uint8_t low=levels[ring][0],high=levels[ring][4];
                uint8_t up=levels[ring][t],down=levels[ring][4-t];
                uint8_t r=low,g=low,b=high;
                if (phase==0) r=up;
                else if (phase==1) {r=high;b=down;}
                else if (phase==2) {r=high;g=up;b=low;}
                else if (phase==3) {r=down;g=high;b=low;}
                else if (phase==4) {g=high;b=up;}
                else g=down;
                unsigned at=(32+ring*24+n)*3;
                v.dac[at]=r;v.dac[at+1]=g;v.dac[at+2]=b;
            }
    }
    ++v.palette_version;
}
void msdos_video_release(void) {kfree(v.plane);v.plane=0;v.active=0;}
int msdos_video_reset(uint8_t *ram) {
    msdos_video_release();
    kmemset(&v,0,sizeof(v));v.ram=ram;v.mode=3;v.enabled=1;
    v.plane=kcalloc(4,65536);
    if (!v.plane) return -1;
    v.mask=255;v.char_height=16;v.seq[2]=15;v.seq[4]=6;
    v.gc[7]=15;v.gc[8]=255;v.ac[0x12]=15;v.misc=0x63;
    default_palette();
    return 0;
}
void msdos_video_font(const uint8_t *font) {
    /* ROM font pointers used by INT 10h/11h/30h. Font plane slots are 32 bytes. */
    for (unsigned c=0;c<256;c++) {
        for (unsigned y=0;y<16;y++) v.ram[0xc8000+c*16+y]=font[c*32+y];
        for (unsigned y=0;y<8;y++) v.ram[0xca000+c*8+y]=font[c*32+y*2];
    }
}
int msdos_video_active(void) {return v.active;}
unsigned msdos_video_width(void) {
    unsigned w=(v.crtc[1]+1)*((v.gc[5]&0x40)?4:8);
    return w>800?800:w;
}
unsigned msdos_video_height(void) {
    unsigned h=1+v.crtc[0x12]+((v.crtc[7]&2)<<7)+((v.crtc[7]&0x40)<<3);
    h/=(v.crtc[9]&31)+1;
    if (v.crtc[9]&0x80) h/=2;
    return h>600?600:h?h:1;
}
static unsigned aperture(void) {
    unsigned map=(v.gc[6]>>2)&3;
    return map<2?0xa0000:map==2?0xb0000:0xb8000;
}
int msdos_video_mapped(uint32_t a) {
    unsigned map=(v.gc[6]>>2)&3,base=aperture();
    unsigned length=map==0?0x20000:map==1?0x10000:0x8000;
    return v.active && a>=base && a-base<length;
}
uint8_t msdos_video_read(uint32_t a) {
    unsigned offset=a-aperture(),plane=v.gc[4]&3;
    if (v.seq[4]&8) {plane=offset&3;offset>>=2;}
    else if (!(v.seq[4]&4) && (v.gc[5]&0x10)) {
        plane=(plane&2)|(offset&1);offset>>=1;
    }
    offset&=65535;
    for (unsigned p=0;p<4;p++) v.latch[p]=v.plane[p][offset];
    if (!(v.gc[5]&8)) return v.latch[plane];
    uint8_t answer=255;
    for (unsigned p=0;p<4;p++)
        if (v.gc[7]&(1<<p)) answer&=(v.gc[2]&(1<<p))?v.latch[p]:(uint8_t)~v.latch[p];
    return answer;
}
void msdos_video_write(uint32_t a,uint8_t value) {
    unsigned offset=a-aperture(),planes=v.seq[2]&15,mode=v.gc[5]&3;
    if (v.seq[4]&8) {planes&=1<<(offset&3);offset>>=2;}
    else if (!(v.seq[4]&4) && (v.gc[6]&2)) {
        planes&=(offset&1)?10:5;offset>>=1;
    }
    offset&=65535;
    uint8_t rotated=rotate(value,v.gc[3]),mask=v.gc[8];
    if (mode==3) mask&=rotated;
    for (unsigned p=0;p<4;p++) if (planes&(1<<p)) {
        uint8_t data;
        if (mode==1) {v.plane[p][offset]=v.latch[p];continue;}
        if (mode==2) data=(value&(1<<p))?255:0;
        else if (mode==3 || (v.gc[1]&(1<<p))) data=(v.gc[0]&(1<<p))?255:0;
        else data=rotated;
        data=logic(data,v.latch[p]);
        v.plane[p][offset]=(data&mask)|(v.latch[p]&(uint8_t)~mask);
    }
}
int msdos_video_out(uint16_t p,uint8_t b) {
    switch (p) {
    case 0x3c2:v.misc=b;break;
    case 0x3c4:v.si=b&7;break;case 0x3c5:v.seq[v.si]=b;break;
    case 0x3ce:v.gi=b&15;break;case 0x3cf:v.gc[v.gi]=b;break;
    case 0x3b4:case 0x3d4:v.ci=b&31;break;
    case 0x3b5:case 0x3d5:v.crtc[v.ci]=b;break;
    case 0x3c0:
        if (!v.flip) {v.ai=b&31;v.enabled=!!(b&32);}
        else {v.ac[v.ai]=b;++v.palette_version;}
        v.flip^=1;break;
    case 0x3c6:v.mask=b;++v.palette_version;break;
    case 0x3c7:v.dr=b;v.dc=0;v.dac_state=3;break;
    case 0x3c8:v.dw=b;v.dc=0;v.dac_state=0;break;
    case 0x3c9:
        v.dac[v.dw*3+v.dc]=b&63;
        if (++v.dc==3) {v.dc=0;++v.dw;}
        ++v.palette_version;break;
    default:return 0;
    }
    return 1;
}
int msdos_video_in(uint16_t p,uint8_t *b) {
    switch (p) {
    case 0x3cc:*b=v.misc;break;
    case 0x3c4:*b=v.si;break;case 0x3c5:*b=v.seq[v.si];break;
    case 0x3ce:*b=v.gi;break;case 0x3cf:*b=v.gc[v.gi];break;
    case 0x3b4:case 0x3d4:*b=v.ci;break;
    case 0x3b5:case 0x3d5:
        if (!v.active) return 0;
        *b=v.crtc[v.ci];break;
    case 0x3c0:*b=v.ai|(v.enabled?32:0);break;case 0x3c1:*b=v.ac[v.ai];break;
    case 0x3c6:*b=v.mask;break;case 0x3c7:*b=v.dac_state;break;
    case 0x3c8:*b=v.dw;break;
    case 0x3c9:
        *b=v.dac[v.dr*3+v.dc];if (++v.dc==3) {v.dc=0;++v.dr;}break;
    case 0x3ba:case 0x3da:v.flip=0;v.status^=9;*b=v.status;break;
    default:return 0;
    }
    return 1;
}
static uint8_t raw_pixel(unsigned x,unsigned y,unsigned page) {
    unsigned stride=v.crtc[0x13]*2;
    unsigned start=page;
    unsigned offset;
    if (v.gc[5]&0x40) {
        offset=(start+y*stride+x/4)&65535;
        return v.plane[x&3][offset];
    }
    offset=(start+y*stride+x/8)&65535;
    unsigned bit=0x80>>(x&7),color=0;
    for (unsigned p=0;p<4;p++) if ((v.ac[0x12]&(1<<p)) && (v.plane[p][offset]&bit))
        color|=1<<p;
    return color;
}
uint8_t msdos_video_pixel(unsigned x,unsigned y) {
    if (!v.enabled || !(v.seq[0]&3) || (v.seq[1]&0x20)) return 0;
    unsigned scan=(v.crtc[9]&31)+1;
    if (v.crtc[9]&0x80) scan*=2;
    unsigned split=v.crtc[0x18]+((v.crtc[7]&0x10)<<4)+((v.crtc[9]&0x40)<<3);
    unsigned start=((unsigned)v.crtc[0xc]<<8)+v.crtc[0xd],pan=v.ac[0x13]&7;
    if (v.gc[5]&0x40) pan=(pan&6)>>1;
    if (y*scan>split) {
        y-=(split+1)/scan;
        start=0;
        if (v.ac[0x10]&0x20) pan=0;
    }
    uint8_t color=raw_pixel(x+pan,y,start);
    if (!(v.gc[5]&0x40)) {
        color=v.ac[color&15]&63;
        if (v.ac[0x10]&0x80) color=(color&15)|((v.ac[0x14]&3)<<4);
        color|=(v.ac[0x14]&12)<<4;
    }
    return color&v.mask;
}
const uint8_t *msdos_video_palette(void) {return v.dac;}
uint32_t msdos_video_palette_version(void) {return v.palette_version;}

static int set_mode(uint8_t request) {
    unsigned mode=request&127,w,h;
    switch (mode) {
    case 0xd:w=320;h=200;v.page_size=8192;break;
    case 0xe:w=640;h=200;v.page_size=16384;break;
    case 0xf:case 0x10:w=640;h=350;v.page_size=32768;break;
    case 0x11:case 0x12:w=640;h=480;v.page_size=40960;break;
    case 0x13:w=320;h=200;v.page_size=64000;break;
    default:
        if (mode<=7) {v.active=0;v.mode=mode;}
        return 0; /* Let the unchanged ROM implement legacy text/CGA modes. */
    }
    v.active=1;v.mode=mode;v.enabled=1;v.flip=0;v.mask=255;
    v.si=v.gi=v.ci=v.ai=v.dc=0;v.char_height=h==350?14:h==480?16:8;
    kmemset(v.seq,0,sizeof(v.seq));kmemset(v.gc,0,sizeof(v.gc));
    kmemset(v.crtc,0,sizeof(v.crtc));kmemset(v.ac,0,sizeof(v.ac));
    kmemset(v.latch,0,sizeof(v.latch));
    if (!(request&128)) kmemset(v.plane,0,4u*65536);
    v.seq[0]=3;v.seq[1]=1;v.seq[2]=15;v.seq[4]=mode==0x13?14:6;
    v.gc[5]=mode==0x13?0x40:0;v.gc[6]=5;v.gc[7]=15;v.gc[8]=255;
    v.ac[0x10]=mode==0x13?0x41:1;v.ac[0x12]=15;
    if (mode==0xf || mode==0x11) v.ac[0x12]=1;
    v.crtc[1]=w/(mode==0x13?4:8)-1;v.crtc[0x13]=w/(mode==0x13?8:16);
    v.crtc[0x12]=(h-1)&255;v.crtc[7]=((h-1)&256?2:0)|((h-1)&512?0x40:0)|0x10;
    v.crtc[9]=0x40;v.crtc[0x18]=255;v.misc=0x63;
    default_palette();
    if (mode==0xf || mode==0x11) v.ac[1]=63;
    v.ram[0x449]=mode;putword(0x44a,w/8);putword(0x44c,v.page_size);
    putword(0x44e,0);kmemset(v.ram+0x450,0,16);v.ram[0x462]=0;
    putword(0x463,0x3d4);v.ram[0x484]=h/v.char_height-1;
    putword(0x485,v.char_height);v.ram[0x487]=0x60|(request&128);
    return 1;
}
static void put_pixel(unsigned x,unsigned y,unsigned page,uint8_t color,int xor) {
    if (x>=msdos_video_width() || y>=msdos_video_height()) return;
    unsigned offset=page+y*v.crtc[0x13]*2;
    if (v.gc[5]&0x40) {v.plane[x&3][(offset+x/4)&65535]=color;return;}
    offset=(offset+x/8)&65535;unsigned bit=128>>(x&7);
    for (unsigned p=0;p<4;p++) {
        if (xor) {if (color&(1<<p)) v.plane[p][offset]^=bit;}
        else v.plane[p][offset]=(v.plane[p][offset]&~bit)|((color&(1<<p))?bit:0);
    }
}
static void glyph(unsigned x,unsigned y,unsigned page,uint8_t ch,uint8_t color) {
    for (unsigned row=0;row<v.char_height;row++) {
        uint8_t bits=v.char_height==8?v.ram[0xca000+ch*8+row]:
            v.ram[0xc8000+ch*16+row];
        for (unsigned b=0;b<8;b++) put_pixel(x*8+b,y*v.char_height+row,page,
                                            bits&(128>>b)?color:0,0);
    }
}
static void scroll(unsigned page,unsigned left,unsigned top,unsigned right,
                   unsigned bottom,unsigned count,int down,uint8_t color) {
    unsigned w=msdos_video_width(),h=msdos_video_height();
    left*=8;right=(right+1)*8;top*=v.char_height;bottom=(bottom+1)*v.char_height;
    if (right>w) right=w;
    if (bottom>h) bottom=h;
    if (left>=right || top>=bottom) return;
    unsigned lines=count*v.char_height;
    if (!count || lines>bottom-top) lines=bottom-top;
    for (unsigned n=0;n<bottom-top;n++) {
        unsigned y=down?bottom-1-n:top+n;
        int source=down?(int)y-(int)lines:(int)y+(int)lines;
        for (unsigned x=left;x<right;x++)
            put_pixel(x,y,page,source>=(int)top && source<(int)bottom?
                      raw_pixel(x,(unsigned)source,page):color,0);
    }
}
int msdos_video_bios(uint16_t *r) {
    /* Register order shared with tiny.c: AX,CX,DX,BX,SP,BP,SI,DI,ES,CS,SS,DS. */
    unsigned ax=r[0],ah=ax>>8,al=ax&255,bx=r[3],bh=bx>>8,bl=bx&255;
    unsigned cx=r[1],dx=r[2],page=bh<8?bh:0;
    uint32_t pointer=((uint32_t)r[8]<<4)+dx;
    if (ah==0) return set_mode(al);
    if (ah==0x1a) {r[0]=(ax&0xff00)|0x1a;if (!al) r[3]=8;return 1;}
    if (ah==0x12 && bl==0x10) {r[3]=3;r[1]=0;return 1;}
    if (ah==0x4f) {r[0]=0x014f;return 1;} /* No VESA/SVGA extensions. */
    if (ah==0x11 && al==0x30) {
        r[8]=0xc000;r[5]=(bh==3 || bh==4)?0xa000:0x8000;
        r[1]=(bh==3 || bh==4)?8:16;r[2]=v.active?v.ram[0x484]:24;return 1;
    }
    if (!v.active) return 0;
    unsigned page_offset=page*v.page_size;
    if (page_offset+v.page_size>65536) page_offset=0;
    unsigned cursor=0x450+2*page,position=word(cursor);
    unsigned col=position&255,row=position>>8,cols=word(0x44a),rows=v.ram[0x484]+1;
    switch (ah) {
    case 1:putword(0x460,cx);break;
    case 2:putword(cursor,dx);break;
    case 3:r[1]=word(0x460);r[2]=position;break;
    case 5:
        if (al<8 && (al+1)*v.page_size<=65536) {
            unsigned start=al*v.page_size;v.ram[0x462]=al;putword(0x44e,start);
            v.crtc[0xc]=start>>8;v.crtc[0xd]=start;
        }break;
    case 6:case 7:
        scroll(v.ram[0x462]*v.page_size,cx&255,cx>>8,dx&255,dx>>8,al,ah==7,bh&15);
        break;
    case 8:r[0]=0;break;
    case 9:case 0xa:
        for (unsigned i=0;i<cx && row<rows;i++) {
            glyph(col,row,page_offset,al,bl);if (++col>=cols) {col=0;++row;}
        }break;
    case 0xc:put_pixel(cx,dx,page_offset,al,al&128 && v.mode!=0x13);break;
    case 0xd:r[0]=(ax&0xff00)|((cx<msdos_video_width() && dx<msdos_video_height())?
                              raw_pixel(cx,dx,page_offset):0);break;
    case 0xe:
        page=v.ram[0x462];page_offset=page*v.page_size;cursor=0x450+2*page;
        position=word(cursor);col=position&255;row=position>>8;
        if (al==13) col=0;else if (al==10) ++row;
        else if (al==8) {if (col) --col;}
        else if (al!=7) {glyph(col,row,page_offset,al,bl);if (++col>=cols) {col=0;++row;}}
        if (row>=rows) {scroll(page_offset,0,0,cols-1,rows-1,1,0,0);row=rows-1;}
        putword(cursor,(row<<8)|col);break;
    case 0xf:r[0]=(cols<<8)|v.mode;r[3]=(v.ram[0x462]<<8)|bl;break;
    case 0x10:
        switch (al) {
        case 0:if (bl<16) v.ac[bl]=bh;break;
        case 1:v.ac[0x11]=bh;break;
        case 2:if (pointer<=0x10fff0-17) {
            kmemcpy(v.ac,v.ram+pointer,16);v.ac[0x11]=v.ram[pointer+16];
        }break;
        case 3:v.ac[0x10]=(v.ac[0x10]&~8)|((bl&1)<<3);break;
        case 7:if (bl<16) r[3]=(v.ac[bl]<<8)|bl;break;
        case 8:r[3]=(v.ac[0x11]<<8)|bl;break;
        case 9:if (pointer<=0x10fff0-17) {
            kmemcpy(v.ram+pointer,v.ac,16);v.ram[pointer+16]=v.ac[0x11];
        }break;
        case 0x10:
            if (bx<256) {v.dac[bx*3]=(dx>>8)&63;v.dac[bx*3+1]=(cx>>8)&63;v.dac[bx*3+2]=cx&63;}
            break;
        case 0x12:case 0x17:
            if (bx<256 && cx<=256-bx && pointer<=0x10fff0-cx*3) {
                for (unsigned i=0;i<cx*3;i++)
                    if (al==0x12) v.dac[bx*3+i]=v.ram[pointer+i]&63;
                    else v.ram[pointer+i]=v.dac[bx*3+i];
            }
            break;
        case 0x15:if (bx<256) {
            r[2]=(dx&255)|(v.dac[bx*3]<<8);
            r[1]=(v.dac[bx*3+1]<<8)|v.dac[bx*3+2];
        }break;
        case 0x13:
            if (!bl) v.ac[0x10]=(v.ac[0x10]&127)|((bh&1)<<7);
            else v.ac[0x14]=bh&15;
            break;
        case 0x1a:r[3]=((v.ac[0x14]&15)<<8)|((v.ac[0x10]>>7)&1);break;
        default:break;
        }++v.palette_version;break;
    case 0x11:
        if ((al==0 || al==0x10) && bh>0 && bh<=16 && dx<256 && cx<=256-dx) {
            pointer=((uint32_t)r[8]<<4)+r[5];
            if (pointer<=0x10fff0-cx*bh)
                for (unsigned c=0;c<cx;c++)
                    for (unsigned y=0;y<bh;y++)
                        v.ram[0xc8000+(dx+c)*16+y]=v.ram[pointer+c*bh+y];
        }break;
    case 0x13: {
        pointer=((uint32_t)r[8]<<4)+r[5];
        unsigned length=cx*((al&2)?2:1);
        col=dx&255;row=dx>>8;
        if (pointer>0x10fff0-length) break;
        for (unsigned i=0;i<cx && row<rows;i++) {
            uint8_t ch=v.ram[pointer++],color=(al&2)?v.ram[pointer++]:bl;
            if (ch==13) col=0;else if (ch==10) ++row;
            else if (ch==8) {if (col) --col;}
            else if (ch!=7) {glyph(col,row,page_offset,ch,color);if (++col>=cols) {col=0;++row;}}
        }
        if (al&1) putword(cursor,(row<<8)|col);
        break;
    }
    default:break;
    }
    return 1;
}
