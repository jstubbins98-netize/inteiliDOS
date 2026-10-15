/* Nonblocking lyric scheduler and monophonic PC-speaker MIDI rendition.
 * Never alters the host timer, keyboard IRQs, volume preference or VGA mode. */
#include "glados.h"
#include "../kernel/timer.h"
#include "../kernel/keyboard.h"
#include "../kernel/vga.h"
#include <stdint.h>
#include "data.h"
#define SCREEN ((volatile uint16_t *)0xB8000)
#define AMBER 0x6000u
static uint16_t saved_screen[2000];
static uint8_t colors[2][3];
static inline uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v;
}
static inline void outb(uint16_t port,uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port));
}
static void palette_read(unsigned index,uint8_t rgb[3]) {
    outb(0x3C7,(uint8_t)index);
    for (unsigned i=0;i<3;i++) rgb[i]=inb(0x3C9);
}
static void palette_write(unsigned index,const uint8_t rgb[3]) {
    outb(0x3C8,(uint8_t)index);
    for (unsigned i=0;i<3;i++) outb(0x3C9,rgb[i]);
}
static void clear_amber(void) {
    for (unsigned i=0;i<2000;i++) SCREEN[i]=AMBER|' ';
}
void glados_play(void) {
    int saved_row,saved_col;
    vga_get_cursor(&saved_row,&saved_col);
    for (unsigned i=0;i<2000;i++) saved_screen[i]=SCREEN[i];
    /* Native mode 3 maps text colour 6 to DAC entry 20 (brown). Make
     * that entry actual bright amber, not a blinking high-bit background. */
    palette_read(0,colors[0]); palette_read(20,colors[1]);
    static const uint8_t black[3]={0,0,0}, amber[3]={63,42,0};
    palette_write(0,black);palette_write(20,amber);
    uint8_t index=inb(0x3D4);
    outb(0x3D4,0x0A);uint8_t shape=inb(0x3D5);outb(0x3D5,shape|0x20);
    clear_amber();
    uint32_t start=timer_get_ticks(),note_end=glados_notes[0].ms;
    unsigned note=0,printed=0,row=0,col=0;
    int page=-1,finished=0;
    uint32_t frequency=0xFFFFFFFFu;
    for (;;) {
        int key=keyboard_poll();
        if (key==27 || (finished && (key=='\n' || key=='\r'))) break;
        uint32_t elapsed=timer_get_ticks()-start;
        while (note<sizeof(glados_notes)/sizeof(glados_notes[0]) && elapsed>=note_end) {
            note++;
            if (note<sizeof(glados_notes)/sizeof(glados_notes[0]))
                note_end+=glados_notes[note].ms;
        }
        uint32_t hz=note<sizeof(glados_notes)/sizeof(glados_notes[0]) ?
            glados_notes[note].hz : 0;
        if (!speaker_get_volume()) hz=0;
        if (hz!=frequency) {
            if (hz) speaker_on(hz);else speaker_off();
            frequency=hz;
        }
        int next=-1;
        for (unsigned i=0;i<sizeof(glados_stanzas)/sizeof(glados_stanzas[0]);i++)
            if (elapsed>=glados_stanzas[i].start) next=(int)i;
        if (next>=0) {
            const glados_stanza *s=&glados_stanzas[next];
            if (page!=next) {
                clear_amber();page=next;printed=0;row=s->row;col=s->col;
            }
            uint32_t age=elapsed-s->start;
            unsigned wanted=age>=s->typing_ms ? s->len :
                (age*s->len)/s->typing_ms+1;
            if (wanted>s->len) wanted=s->len;
            /* Metadata bounds the schedule; the terminator bounds the text. */
            while (printed<wanted && s->text[printed]!='\0') {
                char c=s->text[printed++];
                if (c=='\n') { row++;col=s->col; }
                else SCREEN[row*80+col++]=AMBER|(uint8_t)c;
            }
        }
        if (!finished && elapsed>=GLADOS_DURATION) {
            finished=1;speaker_off();
            const char *hint="Enter / Esc: return to inteiliDOS";
            for (unsigned i=0;hint[i];i++) SCREEN[24*80+23+i]=AMBER|(uint8_t)hint[i];
        }
        __asm__ volatile("hlt");
    }
    speaker_off();
    palette_write(0,colors[0]);palette_write(20,colors[1]);
    for (unsigned i=0;i<2000;i++) SCREEN[i]=saved_screen[i];
    outb(0x3D4,0x0A);outb(0x3D5,shape);outb(0x3D4,index);
    vga_set_color(VGA_COLOR_LIGHT_GREY,VGA_COLOR_BLACK);
    vga_set_cursor(saved_row,saved_col);
}
