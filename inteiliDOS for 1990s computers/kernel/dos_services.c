/* Read-only DOS 5-style console/file API subset and text BIOS services.
 * Unsupported APIs fail explicitly; this is not a full MS-DOS kernel. */
#include "dos_internal.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include <stddef.h>

static dos_filesystem_t filesystem;
static struct { uint32_t size, pos; uint8_t used; } files[8];
static uint16_t dta_seg, dta_off;
static uint8_t pending_scan;
static int lookahead;
static uint32_t clock_base;
static uint8_t crtc_index, cursor_shape[2], cursor_address[2], retrace_phase;
static void text_cursor(unsigned row, unsigned col) {
    vga_set_cursor(row, col);
    unsigned position = row*80+col;
    cursor_address[0] = position>>8; cursor_address[1] = position;
    uint8_t *bda = dos_pointer(0x40, 0x50, 2, 1);
    if (bda) { bda[0] = col; bda[1] = row; }
}
/* Some DOS CRT libraries program the text cursor instead of calling INT 10h.
 * These are virtual registers, not permission to access physical VGA ports. */
int dos_video_port(uint16_t port, int write, uint8_t *value) {
    if (port == 0x3DA && !write) {
        retrace_phase ^= 9;
        *value = retrace_phase;
        return 1;
    }
    if (port == 0x3D4) {
        if (!write) { *value = crtc_index; return 1; }
        if (*value != 0x0A && *value != 0x0B &&
            *value != 0x0E && *value != 0x0F) return 0;
        crtc_index = *value; return 1;
    }
    if (port != 0x3D5) return 0;
    if (crtc_index == 0x0A || crtc_index == 0x0B) {
        if (write) cursor_shape[crtc_index-0x0A] = *value;
        else *value = cursor_shape[crtc_index-0x0A];
        return 1;
    }
    if (crtc_index != 0x0E && crtc_index != 0x0F) return 0;
    if (!write) {
        int row, col;
        vga_get_cursor(&row, &col);
        unsigned position = row*80+col;
        *value = crtc_index == 0x0E ? position>>8 : position;
        return 1;
    }
    cursor_address[crtc_index-0x0E] = *value;
    unsigned position = ((unsigned)cursor_address[0]<<8)|cursor_address[1];
    if (position < 2000) text_cursor(position/80, position%80);
    return 1;
}
static void set_ax(registers_t *r, uint16_t v) { r->eax = (r->eax & 0xFFFF0000u) | v; }
static void set_al(registers_t *r, uint8_t v) { r->eax = (r->eax & 0xFFFFFF00u) | v; }
static void set_dx(registers_t *r, uint16_t v) { r->edx = (r->edx & 0xFFFF0000u) | v; }
static void carry(registers_t *r, int e) {
    if (e) { r->eflags |= 1; set_ax(r, e); }
    else r->eflags &= ~1u;
}
static uint16_t rd16(const uint8_t *p) { return p[0] | ((uint16_t)p[1]<<8); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

void dos_services_init(const dos_filesystem_t *fs) {
    dos_hardware_init();
    crtc_index = retrace_phase = 0;
    cursor_shape[0] = 6; cursor_shape[1] = 7;
    int row, col;
    vga_get_cursor(&row, &col);
    unsigned position = row*80+col;
    cursor_address[0] = position>>8; cursor_address[1] = position;
    filesystem = fs ? *fs : (dos_filesystem_t){0, 0, 'D'};
    for (unsigned i = 0; i < 8; i++) files[i].used = 0;
    dta_seg = DOS_PSP; dta_off = 0x80; pending_scan = 0;
    clock_base = timer_get_ticks();
    lookahead = -1;
    /* Virtual IVT entries chain to monitor stubs, not the physical BIOS.
     * A program may replace these vectors; calls through saved old vectors
     * bypass the hook once and reach the original monitor service. */
    uint8_t *ivt = dos_pointer(0, 0, 1024, 1);
    uint8_t *stubs = dos_pointer(0x1000, 0x200, 1024, 1);
    for (unsigned n = 0; n < 256; n++) {
        wr16(ivt+n*4, 0x200+n*4);
        wr16(ivt+n*4+2, 0x1000);
    }
    for (unsigned n = 0; n < 256; n++) {
        stubs[n*4] = 0xCD; stubs[n*4+1] = n;
        stubs[n*4+2] = 0xCF; stubs[n*4+3] = 0x90;
    }
    uint8_t *video_caps = dos_pointer(0x1000, 0x800, 16, 1);
    for (unsigned n = 0; n < 16; n++) video_caps[n] = 0;
    video_caps[0] = 8; /* only mode 3, not the host's graphics capabilities */
    video_caps[7] = 4; /* 400 text scan lines */
    video_caps[8] = video_caps[9] = 1;
    video_caps[11] = 8; /* display combination query */
}
static uint8_t scan(int c) {
    switch (c) {
    case KEY_UP: return 0x48;
    case KEY_DOWN: return 0x50;
    case KEY_LEFT: return 0x4B;
    case KEY_RIGHT: return 0x4D;
    default:
        if (c >= KEY_F1 && c <= KEY_F8) return 0x3B+c-KEY_F1;
        return 0;
    }
}
static int character(int wait) {
    if (lookahead >= 0) { int c = lookahead; lookahead = -1; return c; }
    if (pending_scan) { int c = pending_scan; pending_scan = 0; return c; }
    int c = dos_key(wait);
    if (c < 0) return c;
    if (scan(c)) { pending_scan = scan(c); return 0; }
    return c;
}
static int guest_string(uint16_t seg, uint16_t off, char *out, unsigned max) {
    for (unsigned i = 0; i < max; i++) {
        if ((uint32_t)off+i > 0xFFFF) return 0;
        uint8_t *p = dos_pointer(seg, off+i, 1, 0);
        if (!p) return 0;
        out[i] = *p;
        if (!*p) return 1;
    }
    return 0;
}
static int handle_index(registers_t *r) {
    unsigned h = (uint16_t)r->ebx;
    return h >= 5 && h < 13 && files[h-5].used ? (int)h-5 : -1;
}
static void output(uint8_t c) {
    if (c == 13) { int row, col; vga_get_cursor(&row, &col); text_cursor(row, 0); }
    else vga_putchar(c);
    int row, col;
    vga_get_cursor(&row, &col);
    text_cursor(row, col);
}
static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v;
}
static uint8_t rtc_reg(uint8_t reg) {
    outb(0x70, reg); return inb(0x71);
}
/* Bounded stable RTC snapshots; no busy-wait if CMOS is unavailable. */
static int rtc(uint8_t values[6]) {
    static const uint8_t regs[6] = {0,2,4,7,8,9};
    for (unsigned attempt = 0; attempt < 10000; attempt++) {
        if (rtc_reg(0x0A) & 0x80) continue;
        for (unsigned i = 0; i < 6; i++) values[i] = rtc_reg(regs[i]);
        uint8_t status = rtc_reg(0x0B);
        if ((rtc_reg(0x0A)&0x80) || values[0] != rtc_reg(0)) continue;
        unsigned pm = values[2] & 0x80;
        values[2] &= 0x7F;
        if (!(status&4))
            for (unsigned i = 0; i < 6; i++) values[i] = (values[i]>>4)*10+(values[i]&15);
        if (!(status&2)) values[2] = values[2]%12+(pm ? 12 : 0);
        return values[0]<60 && values[1]<60 && values[2]<24 &&
               values[3]>0 && values[3]<=31 && values[4]>0 && values[4]<=12;
    }
    return 0;
}
static uint8_t bcd(unsigned n) { return ((n/10)<<4) | (n%10); }

static int int21(dos_frame_t *f) {
    registers_t *r = &f->r;
    uint8_t ah = r->eax >> 8, al = r->eax;
    uint16_t ds = f->vds, dx = r->edx, cx = r->ecx;
    uint8_t *p;
    int c, index;
    switch (ah) {
    case 0x00: dos_status = 0; dos_stop(r, 0, ""); return 1;
    case 0x4C: dos_status = al; dos_stop(r, 0, ""); return 1;
    case 0x01: case 0x07: case 0x08:
        c = character(1);
        if (c >= 0) { set_al(r, c); if (ah == 1) output(c); }
        return 1;
    case 0x02: output(dx); set_al(r, dx); return 1;
    case 0x06:
        if ((uint8_t)dx != 0xFF) { output(dx); set_al(r, dx); }
        else {
            c = character(0); set_al(r, c < 0 ? 0 : c);
            if (c < 0) r->eflags |= 0x40; else r->eflags &= ~0x40u;
        }
        return 1;
    case 0x09:
        for (unsigned i = 0; i+dx <= 0xFFFF; i++) {
            p = dos_pointer(ds, dx+i, 1, 0);
            if (!p) break;
            if (*p == '$') { set_al(r, '$'); return 1; }
            output(*p);
        }
        dos_stop(r, -3, "Invalid DOS string (INT 21h/09h)."); return 1;
    case 0x0A: {
        p = dos_pointer(ds, dx, 2, 1);
        if (!p || !p[0]) { carry(r, 5); return 1; }
        unsigned max = p[0], n = 0;
        if ((uint32_t)dx+max+2 > 0x10000 ||
            !(p = dos_pointer(ds, dx, max+2, 1))) { carry(r, 5); return 1; }
        while (!dos_cancelled) {
            c = character(1);
            if (c < 0) break;
            if (!c) { character(1); continue; }
            if (c == 13) break;
            if (c == 8) {
                if (n) { n--; output(8); output(' '); output(8); }
            } else if (n+1 < max) { p[2+n++] = c; output(c); }
        }
        p[1] = n; p[2+n] = 13; output(13); output(10);
        return 1;
    }
    case 0x0B:
        c = character(0);
        if (c >= 0) lookahead = c;
        set_al(r, c >= 0 ? 0xFF : 0); return 1;
    case 0x0E:
        if ((uint8_t)dx != filesystem.drive-'A') { carry(r, 15); return 1; }
        set_al(r, 4); return 1;
    case 0x19: set_al(r, filesystem.drive-'A'); return 1;
    case 0x1A: dta_seg = ds; dta_off = dx; return 1;
    case 0x2F: f->ves = dta_seg; r->ebx = dta_off; return 1;
    case 0x25: case 0x35:
        p = dos_pointer(0, al*4, 4, ah == 0x25);
        if (!p) { carry(r, 5); return 1; }
        if (ah == 0x25) { wr16(p, dx); wr16(p+2, ds); }
        else { r->ebx = rd16(p); f->ves = rd16(p+2); }
        return 1;
    case 0x30: set_ax(r, 5); r->ebx = r->ecx = 0; return 1;
    case 0x37:
        if (!al) { set_al(r, 0); set_dx(r, '/'); return 1; }
        return 0;
    case 0x50:
        if (r->ebx != DOS_PSP) return 0; /* one process per session */
        return 1;
    case 0x51: case 0x62: r->ebx = DOS_PSP; return 1;
    case 0x33:
        if (al <= 1) { if (!al) set_dx(r, 0); return 1; }
        return 0;
    case 0x2A: case 0x2C: {
        uint8_t v[6];
        if (!rtc(v)) { carry(r, 30); return 1; }
        if (ah == 0x2C) {
            r->ecx = ((uint16_t)v[2]<<8)|v[1];
            r->edx = ((uint16_t)v[0]<<8);
        } else {
            unsigned year = (v[5] < 80 ? 2000 : 1900)+v[5];
            static const unsigned months[] = {0,3,2,5,0,3,5,1,4,6,2,4};
            unsigned y = year-(v[4]<3);
            set_al(r, (y+y/4-y/100+y/400+months[v[4]-1]+v[3])%7);
            r->ecx = year; r->edx = ((uint16_t)v[4]<<8)|v[3];
        }
        return 1;
    }
    case 0x3D: {
        char path[260];
        if ((al&3) != 0) { carry(r, 5); return 1; } /* read-only */
        if (!guest_string(ds, dx, path, sizeof(path))) { carry(r, 3); return 1; }
        for (index = 0; index < 8 && files[index].used; index++) {}
        if (index == 8) { carry(r, 4); return 1; }
        if (!filesystem.open) { carry(r, 2); return 1; }
        __asm__ volatile("sti" ::: "memory");
        int e = filesystem.open(path, index, &files[index].size);
        __asm__ volatile("cli" ::: "memory");
        carry(r, e);
        if (!e) { files[index].used = 1; files[index].pos = 0; set_ax(r, index+5); }
        return 1;
    }
    case 0x3E:
        index = handle_index(r);
        carry(r, index < 0 ? 6 : 0);
        if (index >= 0) files[index].used = 0;
        return 1;
    case 0x3F: {
        if (!cx) { carry(r, 0); set_ax(r, 0); return 1; }
        if ((uint32_t)dx+cx > 0x10000 || !(p = dos_pointer(ds, dx, cx, 1))) {
            carry(r, 5); return 1;
        }
        if ((uint16_t)r->ebx == 0) {
            unsigned n = 0;
            while (n < cx && (c = character(1)) >= 0) {
                p[n++] = c; if (c == 13) break;
            }
            carry(r, 0); set_ax(r, n); return 1;
        }
        index = handle_index(r);
        if (index < 0 || !filesystem.read) { carry(r, 6); return 1; }
        uint32_t available = files[index].pos < files[index].size ?
                             files[index].size-files[index].pos : 0;
        if (cx > available) cx = available;
        __asm__ volatile("sti" ::: "memory");
        int32_t n = cx ? filesystem.read(index, files[index].pos, p, cx) : 0;
        __asm__ volatile("cli" ::: "memory");
        if (n < 0 || n > cx) carry(r, n < 0 ? -n : 30);
        else { files[index].pos += n; carry(r, 0); set_ax(r, n); }
        return 1;
    }
    case 0x40:
        if ((uint16_t)r->ebx != 1 && (uint16_t)r->ebx != 2) {
            carry(r, 5); return 1;
        }
        p = dos_pointer(ds, dx, cx, 0);
        if ((uint32_t)dx+cx > 0x10000 || (!p && cx)) { carry(r, 5); return 1; }
        for (unsigned i = 0; i < cx; i++) output(p[i]);
        carry(r, 0); set_ax(r, cx); return 1;
    case 0x42: {
        index = handle_index(r);
        if (index < 0) { carry(r, 6); return 1; }
        int32_t delta = (int32_t)(((uint32_t)cx<<16)|dx);
        uint32_t base = al == 0 ? 0 : al == 1 ? files[index].pos : files[index].size;
        if (al > 2 || (delta < 0 && (uint32_t)(-(delta+1))+1 > base) ||
            (delta >= 0 && (uint32_t)delta > 0xFFFFFFFFu-base)) {
            carry(r, 1); return 1;
        }
        files[index].pos = base+(uint32_t)delta;
        carry(r, 0); set_ax(r, files[index].pos);
        set_dx(r, files[index].pos>>16); return 1;
    }
    case 0x44:
        if (!al) {
            unsigned h = (uint16_t)r->ebx;
            if (h <= 2) { set_dx(r, h ? 0x80C2 : 0x80C1); carry(r, 0); }
            else if (handle_index(r) >= 0) { set_dx(r, filesystem.drive-'A'); carry(r, 0); }
            else carry(r, 6);
            return 1;
        }
        carry(r, 1); return 1;
    case 0x47: {
        p = dos_pointer(ds, r->esi, 64, 1);
        if (!p) { carry(r, 5); return 1; }
        if ((uint8_t)dx && (uint8_t)dx != filesystem.drive-'A'+1) {
            carry(r, 15); return 1;
        }
        /* Guest root is the LaunchPad directory selected for this session. */
        p[0] = 0; carry(r, 0); return 1;
    }
    case 0x48: case 0x49: case 0x4A: {
        uint16_t largest = 0, segment = 0;
        int e;
        if (ah == 0x48) {
            segment = dos_allocate(r->ebx, &largest);
            e = segment ? 0 : 8;
        } else if (ah == 0x49) e = dos_free(f->ves);
        else e = dos_resize(f->ves, r->ebx, &largest);
        carry(r, e);
        if (e == 8) r->ebx = largest;
        else if (!e && ah == 0x48) set_ax(r, segment);
        return 1;
    }
    /* Explicit read-only failures, rather than pretending that saves worked. */
    case 0x39: case 0x3A: case 0x3C: case 0x41: case 0x43: case 0x56:
        carry(r, 5); return 1;
    case 0x4E: case 0x4F: /* directory enumeration not yet implemented */
        return 0;
    default: return 0;
    }
}

static int int10(dos_frame_t *f) {
    registers_t *r = &f->r;
    uint8_t ah = r->eax>>8, al = r->eax;
    int row, col;
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    vga_get_cursor(&row, &col);
    switch (ah) {
    case 0:
        if ((al&0x7F) != 3) return 0;
        if (!(al&0x80)) vga_clear();
        return 1;
    case 1: return 1; /* cursor shape: retain the kernel's text cursor */
    case 2:
        if ((r->ebx&0xFF00) || ((r->edx>>8)&255)>=25 || (r->edx&255)>=80) return 0;
        text_cursor((r->edx>>8)&255, r->edx&255); return 1;
    case 3:
        if (r->ebx&0xFF00) return 0;
        r->edx = (row<<8)|col; r->ecx = 0x0607; return 1;
    case 5: return al == 0;
    case 6: case 7: {
        unsigned top = (r->ecx>>8)&255, left = r->ecx&255;
        unsigned bottom = (r->edx>>8)&255, right = r->edx&255;
        if (top>bottom || left>right || bottom>=25 || right>=80) return 0;
        unsigned height = bottom-top+1, n = al ? al : height;
        if (n > height) n = height;
        uint16_t blank = (r->ebx&0xFF00)|' ';
        for (unsigned y = 0; y < height; y++) {
            unsigned dest = ah == 6 ? top+y : bottom-y;
            for (unsigned x = left; x <= right; x++)
                vga[dest*80+x] = y+n < height ?
                    vga[(ah == 6 ? dest+n : dest-n)*80+x] : blank;
        }
        return 1;
    }
    case 8:
        if (r->ebx&0xFF00) return 0;
        set_ax(r, vga[row*80+col]); return 1;
    case 9: case 10:
        if (r->ebx&0xFF00) return 0;
        for (unsigned i = row*80+col, n = (uint16_t)r->ecx; n && i<2000; i++, n--)
            vga[i] = ((ah == 9 ? (r->ebx&255)<<8 : vga[i]&0xFF00)) | al;
        return 1;
    case 14: output(al); return 1;
    case 15: set_ax(r, 0x5003); r->ebx &= 255; return 1;
    case 0x11: {
        uint16_t seg, off;
        if (al != 0x30 || !dos_font_info((r->ebx>>8)&255, &seg, &off)) return 0;
        f->ves = seg; r->ebp = off;
        r->ecx = 16; /* BIOS reports the current text-mode character height */
        r->edx = (r->edx&~255u)|24;
        return 1;
    }
    case 0x1A:
        if (al) { set_al(r, 0); return 1; }
        set_al(r, 0x1A); r->ebx = 8; return 1; /* VGA color, no second display */
    case 0x1B: {
        if ((uint16_t)r->ebx) { set_al(r, 0); return 1; }
        uint8_t *state = dos_pointer(f->ves, r->edi, 64, 1);
        if (!state) { dos_stop(r, -3, "Invalid VGA state buffer."); return 1; }
        for (unsigned i = 0; i < 64; i++) state[i] = 0;
        wr16(state, 0x800); wr16(state+2, 0x1000);
        state[4] = 3; wr16(state+5, 80); wr16(state+7, 4096);
        state[11] = col; state[12] = row;
        state[27] = cursor_shape[1]; state[28] = cursor_shape[0];
        wr16(state+30, 0x3D4); state[34] = 25; wr16(state+35, 16);
        state[37] = 8; wr16(state+39, 16); state[41] = 1; state[42] = 2;
        set_al(r, 0x1B); return 1;
    }
    case 0xEF:
        /* Hercules vendor probe: leave DX=FFFFh (no extension installed).
         * Reporting VGA does not imply this optional Hercules BIOS exists. */
        return (uint16_t)r->edx == 0xFFFF;
    default: return 0;
    }
}
static int dispatch_interrupt(dos_frame_t *f, uint8_t number);
int dos_inject_interrupt(dos_frame_t *f, uint8_t number) {
    registers_t *r = &f->r;
    uint8_t *vector = dos_pointer(0, number*4, 4, 0);
    uint16_t sp = (uint16_t)(r->useresp-6);
    uint8_t *stack = sp <= 0xFFFA ? dos_pointer(r->ss, sp, 6, 1) : NULL;
    if (!vector || !stack || !dos_pointer(rd16(vector+2), rd16(vector), 1, 0)) {
        dos_stop(r, -3, "Invalid virtual interrupt vector or stack."); return 1;
    }
    wr16(stack, r->eip); wr16(stack+2, r->cs); wr16(stack+4, dos_virtual_flags(r));
    dos_virtual_interrupts(0);
    r->useresp = sp; r->eip = rd16(vector); r->cs = rd16(vector+2);
    r->eflags &= ~0x100u;
    return 1;
}
int dos_interrupt(dos_frame_t *f, uint8_t number) {
    registers_t *r = &f->r;
    uint8_t *vector = dos_pointer(0, number*4, 4, 0);
    uint16_t default_off = 0x200+number*4;
    if (vector && (rd16(vector) != default_off || rd16(vector+2) != 0x1000) &&
        !(r->cs == 0x1000 && r->eip == (uint32_t)default_off+2)) {
        return dos_inject_interrupt(f, number);
    }
    int chained = r->cs == 0x1000 && r->eip == (uint32_t)default_off+2;
    int handled = dispatch_interrupt(f, number);
    if (chained && handled && (r->eflags & DOS_VM)) {
        /* The caller reached the original service by far-calling its saved
         * vector. Its IRET must report the service's carry/zero flags. */
        if ((uint16_t)r->useresp > 0xFFF9) {
            dos_stop(r, -3, "Invalid DOS chained interrupt stack."); return 1;
        }
        uint8_t *saved_flags = dos_pointer(r->ss, (uint16_t)r->useresp+4, 2, 1);
        if (!saved_flags) { dos_stop(r, -3, "Invalid DOS chained interrupt stack."); return 1; }
        wr16(saved_flags, dos_virtual_flags(r));
    }
    return handled;
}
static int dispatch_interrupt(dos_frame_t *f, uint8_t number) {
    registers_t *r = &f->r;
    switch (number) {
    case 4:
        dos_stop(r, -3, "Unhandled DOS overflow interrupt 04h.");
        return 1;
    case 8:
        dos_hardware_eoi();
        return dos_inject_interrupt(f, 0x1C); /* BIOS timer callback */
    case 0x1C: return 1;
    case 0x20: dos_status = 0; dos_stop(r, 0, ""); return 1;
    case 0x21: return int21(f);
    case 0x10: return int10(f);
    case 0x11: set_ax(r, 0x21); return 1; /* VGA, one floppy */
    case 0x12: set_ax(r, 640); return 1;
    case 0x16: {
        uint8_t ah = r->eax>>8;
        if (ah == 2) { set_al(r, 0); return 1; }
        if (ah != 0 && ah != 1 && ah != 0x10 && ah != 0x11) return 0;
        int c = (ah == 1 || ah == 0x11) ? dos_key_peek() : dos_key(1);
        if (ah == 1 || ah == 0x11) {
            if (c < 0) r->eflags |= 0x40;
            else r->eflags &= ~0x40u;
        }
        if (c >= 0) set_ax(r, ((uint16_t)scan(c)<<8)|(scan(c) ? 0 : c));
        return 1;
    }
    case 0x1A: {
        uint8_t ah = r->eax>>8;
        if (ah == 0) {
            uint32_t elapsed = timer_get_ticks()-clock_base;
            uint32_t t = ((elapsed/10000u)*182u +
                          (elapsed%10000u)*182u/10000u) % 0x1800B0u;
            r->ecx = t>>16; r->edx = t&0xFFFF; set_al(r, 0); carry(r, 0); return 1;
        }
        if (ah != 2 && ah != 4) return 0;
        uint8_t v[6];
        if (!rtc(v)) { carry(r, 1); return 1; }
        if (ah == 2) {
            r->ecx = (bcd(v[2])<<8)|bcd(v[1]); r->edx = bcd(v[0])<<8;
        } else {
            r->ecx = ((v[5]<80 ? 0x20 : 0x19)<<8)|bcd(v[5]);
            r->edx = (bcd(v[4])<<8)|bcd(v[3]);
        }
        carry(r, 0); return 1;
    }
    case 0x28: return 1; /* DOS idle */
    case 0x2A:
        if (((r->eax>>8)&255) == 0) {
            r->eax &= ~0xFF00u; return 1; /* no DOS network installed */
        }
        return 0;
    case 0x29: output(r->eax); return 1;
    case 0x2F: /* conventional absence probes, not a DOS extender */
        if ((uint16_t)r->eax == 0x1687) { set_ax(r, 1); return 1; } /* no DPMI */
        if ((uint16_t)r->eax == 0x1600) { set_al(r, 0); return 1; } /* no Windows */
        return 0;
    default: return 0;
    }
}
