/*
 * Initial 80386 DOS compatibility runtime: real v86 execution, not an x86
 * instruction interpreter. The monitor emulates privileged flag operations
 * and software interrupts, keeping hardware IRQs under the kernel's control.
 * Paging exposes conventional memory and text VGA, but not kernel memory.
 */
#include "dos_internal.h"
#include "gdt.h"
#include "keyboard.h"
#include "timer.h"
#include <stddef.h>

#define TABLES 16
#define PRESENT 1u
#define WRITE 2u
#define USER 4u
static uint32_t directory[1024] __attribute__((aligned(4096)));
static uint32_t tables[TABLES][1024] __attribute__((aligned(4096)));
static uint8_t low_page[4096] __attribute__((aligned(4096)));
static uint8_t monitor_stack[16384] __attribute__((aligned(16)));
static unsigned used_tables;
static uint32_t old_cr0, old_cr3, old_stack;
static int active, result, virtual_if;
static const char *error;
static char diagnostic[160];
static char *diagnostic_text(char *out, const char *text) {
    while (*text) *out++ = *text++;
    return out;
}
static char *diagnostic_hex(char *out, uint32_t value, unsigned digits) {
    static const char hex[] = "0123456789ABCDEF";
    while (digits--) *out++ = hex[(value >> (digits*4)) & 15];
    return out;
}
static void diagnostic_location(char *out, uint16_t cs, uint16_t ip) {
    out = diagnostic_text(out, " at ");
    out = diagnostic_hex(out, cs, 4);
    *out++ = ':';
    out = diagnostic_hex(out, ip, 4);
    *out = 0;
}
uint8_t dos_status;
int dos_cancelled;
static uint8_t keys[32];
static unsigned key_head, key_tail;
static int held_key;
static uint32_t ticks;
static uint16_t font_seg[2], font_off[2];
int dos_font_info(unsigned which, uint16_t *seg, uint16_t *off) {
    if (which > 1) return 0;
    uint32_t address = (uint32_t)font_seg[which]*16u + font_off[which];
    if (address < 0xC0000u || address > 0x100000u-4096u) return 0;
    *seg = font_seg[which]; *off = font_off[which];
    return 1;
}
uint16_t dos_virtual_flags(registers_t *r) {
    return (r->eflags & ~(0x3000u | 0x200u)) | (virtual_if ? 0x200u : 0);
}
void dos_virtual_interrupts(int enabled) { virtual_if = enabled; }

extern void v86_enter(const uint32_t *);
extern void v86_return(void);
static void zero(void *p, uint32_t n) {
    uint8_t *b = p;
    while (n--) *b++ = 0;
}
static uint16_t word(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static void put_word(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static uint32_t irq_lock(void) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static void irq_unlock(uint32_t flags) {
    __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
}

/* Identity-map a supervisor 4 MB region, using ordinary 386 page tables.
 * Extra tables allow supervisor IRQ handlers to touch sparse PCI MMIO. */
static int map_region(uint32_t index) {
    if (directory[index] & PRESENT) return 1;
    if (used_tables == TABLES) return 0;
    uint32_t *table = tables[used_tables++];
    for (unsigned i = 0; i < 1024; i++)
        table[i] = (index << 22) | (i << 12) | PRESENT | WRITE;
    directory[index] = (uint32_t)(uintptr_t)table | PRESENT | WRITE;
    return 1;
}
static void paging_begin(void) {
    /* Capture BIOS font pointers before replacing the host IVT with the
     * guest-private page. Expose ROM data, never execute the BIOS handler. */
    __asm__ volatile("movw 0x10c, %0; movw 0x10e, %1"
                     : "=r"(font_off[0]), "=r"(font_seg[0]));
    __asm__ volatile("movw 0x7c, %0; movw 0x7e, %1"
                     : "=r"(font_off[1]), "=r"(font_seg[1]));
    zero(directory, sizeof(directory));
    zero(low_page, sizeof(low_page));
    used_tables = 0;
    for (unsigned i = 0; i < 4; i++) map_region(i);
    directory[0] |= USER;
    /* A private IVT/BDA instead of exposing the host's BIOS/DMA metadata. */
    tables[0][0] = (uint32_t)(uintptr_t)low_page | PRESENT | WRITE | USER;
    for (unsigned i = 0x10; i < 0xA0; i++) tables[0][i] |= USER;
    tables[0][0xB8] |= USER;
    for (unsigned i = 0xC0; i < 0x100; i++)
        tables[0][i] = (i << 12) | PRESENT | USER; /* ROM: read-only */
    low_page[0x449] = 3;
    put_word(low_page + 0x44A, 80);
    put_word(low_page + 0x413, 640);
    low_page[0x484] = 24;
    put_word(low_page + 0x485, 16);
    put_word(low_page + 0x44C, 4096);
    put_word(low_page + 0x463, 0x3D4);
    uint32_t cr3 = (uint32_t)(uintptr_t)directory;
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
    uint32_t cr0 = old_cr0 | 0x80000000u;
    __asm__ volatile("mov %0, %%cr0; jmp 1f; 1:" :: "r"(cr0) : "memory");
}
void dos_cleanup(void) {
    active = 0;
    dos_hardware_cleanup();
    __asm__ volatile("mov %0, %%cr0; jmp 1f; 1:" :: "r"(old_cr0) : "memory");
    __asm__ volatile("mov %0, %%cr3" :: "r"(old_cr3) : "memory");
    gdt_set_kernel_stack(old_stack);
}
uint8_t *dos_pointer(uint16_t seg, uint16_t off, uint32_t size, int write) {
    uint32_t addr = ((uint32_t)seg << 4) + off;
    if (size > 0x100000u || addr >= 0x100000u || size > 0x100000u - addr)
        return NULL;
    if (size) {
        unsigned first = addr >> 12, last = (addr + size - 1) >> 12;
        for (unsigned p = first; p <= last; p++)
            if (!(tables[0][p] & USER) || (write && !(tables[0][p] & WRITE)))
                return NULL;
    }
    /* A valid guest pointer at 0000:0000 must not become C's NULL sentinel.
     * Use the backing page for low-memory services, including vector zero. */
    if (addr < sizeof(low_page)) return low_page+addr;
    return (uint8_t *)(uintptr_t)addr;
}
void dos_stop(registers_t *r, int code, const char *why) {
    result = code;
    error = why;
    r->eip = (uint32_t)(uintptr_t)v86_return;
    r->cs = 8;
    r->eflags = 2; /* no VM, no interrupts until cleanup */
    r->ds = r->es = r->fs = r->gs = 0x10;
}
uint8_t dos_exit_code(void) { return dos_status; }
const char *dos_error(void) { return error; }

int dos_key(int wait) {
    for (;;) {
        if (dos_cancelled) return -1;
        if (held_key >= 0) { int c = held_key; held_key = -1; return c; }
        if (key_tail != key_head) {
            int c = keys[key_tail];
            key_tail = (key_tail + 1) % sizeof(keys);
            return c;
        }
        int c = keyboard_poll();
        if (c == KEY_F8) { dos_cancelled = 1; return -1; }
        if (c >= 0) return c;
        if (!wait) return -1;
        /* DOS services are entered with IRQs disabled; let PS/2 and PIT run. */
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
}
int dos_key_peek(void) {
    if (held_key < 0) held_key = dos_key(0);
    return held_key;
}
void dos_irq(registers_t *r) {
    if (!active) return;
    if (r->int_no == 32) {
        ticks++;
        dos_hardware_tick();
        int c = keyboard_poll();
        if (c == KEY_F8) dos_cancelled = 1;
        else if (c >= 0) {
            unsigned next = (key_head + 1) % sizeof(keys);
            if (next != key_tail) { keys[key_head] = c; key_head = next; }
        }
        /* Keep the guest BDA clock advancing at the BIOS 18.2 Hz rate. */
        uint32_t bios_ticks = ((ticks / 10000u)*182u +
                               (ticks % 10000u)*182u/10000u) % 0x1800B0u;
        for (unsigned i = 0; i < 4; i++) low_page[0x46C+i] = bios_ticks >> (8*i);
    }
    if (dos_cancelled && (r->eflags & DOS_VM))
        dos_stop(r, -4, "Stopped with F8.");
    else if ((r->eflags & DOS_VM) && virtual_if && dos_hardware_take_timer())
        dos_inject_interrupt((dos_frame_t *)r, 8);
}

/* Conventional-memory allocation blocks (paragraphs, initially one process).
 * A program can shrink its initial block with INT 21h/4Ah, then allocate. */
typedef struct { uint16_t seg, count; uint8_t used; } block_t;
static block_t blocks[64];
static unsigned block_count;
static void coalesce(void) {
    for (unsigned i = 0; i + 1 < block_count;) {
        if (!blocks[i].used && !blocks[i+1].used) {
            blocks[i].count += blocks[i+1].count;
            for (unsigned j = i+1; j+1 < block_count; j++) blocks[j] = blocks[j+1];
            block_count--;
        } else i++;
    }
}
static uint16_t largest_free(void) {
    uint16_t n = 0;
    for (unsigned i = 0; i < block_count; i++)
        if (!blocks[i].used && blocks[i].count > n) n = blocks[i].count;
    return n;
}
uint16_t dos_allocate(uint16_t count, uint16_t *largest) {
    *largest = largest_free();
    if (!count) return 0;
    for (unsigned i = 0; i < block_count; i++) {
        if (blocks[i].used || blocks[i].count < count) continue;
        uint16_t seg = blocks[i].seg;
        blocks[i].used = 1;
        if (dos_resize(seg, count, largest)) { blocks[i].used = 0; return 0; }
        return seg;
    }
    return 0;
}
int dos_resize(uint16_t seg, uint16_t count, uint16_t *largest) {
    for (unsigned i = 0; i < block_count; i++) {
        if (blocks[i].seg != seg || !blocks[i].used) continue;
        uint32_t available = blocks[i].count;
        if (i+1 < block_count && !blocks[i+1].used) available += blocks[i+1].count;
        *largest = available;
        if (!count || count > available) return 8;
        if (count == blocks[i].count) return 0;
        if (i+1 < block_count && !blocks[i+1].used) {
            blocks[i].count = available;
            for (unsigned j = i+1; j+1 < block_count; j++) blocks[j] = blocks[j+1];
            block_count--;
        }
        if (count < blocks[i].count) {
            if (block_count == 64) return 8;
            for (unsigned j = block_count; j > i+1; j--) blocks[j] = blocks[j-1];
            blocks[i+1] = (block_t){seg + count, blocks[i].count - count, 0};
            block_count++;
        }
        blocks[i].count = count;
        if (seg == DOS_PSP)
            put_word((uint8_t *)(uintptr_t)(DOS_PSP*16u+2), seg+count);
        coalesce();
        return 0;
    }
    return 9;
}
int dos_free(uint16_t seg) {
    for (unsigned i = 0; i < block_count; i++)
        if (blocks[i].seg == seg && blocks[i].used && seg != DOS_PSP) {
            blocks[i].used = 0;
            coalesce();
            return 0;
        }
    return 9;
}

static int prepare(const uint8_t *image, uint32_t size, int com, uint32_t entry[6]) {
    uint32_t load_bytes, header = 0, allocation = DOS_TOP - DOS_PSP;
    uint16_t ip = 0x100, cs = DOS_PSP, sp = 0xFFFE, ss = DOS_PSP;
    if (!size) return -1;
    if (com) {
        if (size > 0xFF00) return -1;
        load_bytes = size;
    } else {
        if (size < 28 || word(image) != 0x5A4D) return -1;
        uint32_t pages = word(image+4), last = word(image+2);
        if (!pages || last > 511 || word(image+26)) return -1; /* no overlays */
        uint32_t file_bytes = pages * 512u - (last ? 512u-last : 0u);
        header = word(image+8) * 16u;
        uint32_t relocs = word(image+6), reloc_off = word(image+24);
        if (header < 28 || header > file_bytes || file_bytes > size ||
            reloc_off > header || relocs*4u > header-reloc_off) return -1;
        /* A DOS stub in NE/LE/LX/PE is not a DOS application. */
        if (header >= 64 && size >= 64) {
            uint32_t ext = image[60] | ((uint32_t)image[61]<<8) |
                           ((uint32_t)image[62]<<16) | ((uint32_t)image[63]<<24);
            if (ext <= size-2 &&
                ((image[ext]=='N' && image[ext+1]=='E') ||
                 (image[ext]=='L' && (image[ext+1]=='E' || image[ext+1]=='X')) ||
                 (ext <= size-4 && image[ext]=='P' && image[ext+1]=='E' &&
                  !image[ext+2] && !image[ext+3]))) return -1;
        }
        load_bytes = file_bytes - header;
        uint32_t paras = (load_bytes + 15) / 16;
        uint32_t minimum = 16u + paras + word(image+10);
        uint32_t maximum = 16u + paras + word(image+12);
        if (!load_bytes || minimum > allocation || maximum < minimum) return -1;
        if (maximum < allocation) allocation = maximum;
        uint32_t entry_cs = DOS_PSP + 16u + word(image+22);
        uint32_t entry_ss = DOS_PSP + 16u + word(image+14);
        ip = word(image+20); sp = word(image+16);
        if (entry_cs > 0xFFFF || entry_ss > 0xFFFF ||
            entry_cs*16u+ip >= (DOS_PSP+16u)*16u+load_bytes ||
            entry_ss*16u+(sp ? sp : 0x10000u) > (DOS_PSP+allocation)*16u ||
            entry_ss*16u+(sp ? sp : 0x10000u) < DOS_PSP*16u+258u) return -1;
        cs = entry_cs; ss = entry_ss;
        for (uint32_t i = 0; i < relocs; i++) {
            const uint8_t *p = image + reloc_off + i*4;
            uint32_t offset = word(p) + word(p+2)*16u;
            if (offset >= load_bytes || load_bytes-offset < 2) return -1;
        }
    }
    /* This region is reserved from the OS physical allocator (low 1 MB). */
    zero((void *)(uintptr_t)(DOS_PSP*16u), (DOS_TOP-DOS_PSP)*16u);
    uint8_t *psp = (uint8_t *)(uintptr_t)(DOS_PSP*16u);
    psp[0] = 0xCD; psp[1] = 0x20;
    put_word(psp+2, DOS_PSP+allocation);
    put_word(psp+0x2C, 0x1000); /* private environment */
    psp[0x50] = 0xCD; psp[0x51] = 0x21; psp[0x52] = 0xCB;
    psp[0x80] = 0; psp[0x81] = 13; /* empty command tail */
    uint8_t *dest = psp + 256;
    for (uint32_t i = 0; i < load_bytes; i++) dest[i] = image[header+i];
    if (!com) {
        for (uint32_t i = 0; i < word(image+6); i++) {
            const uint8_t *p = image + word(image+24) + i*4;
            uint32_t offset = word(p) + word(p+2)*16u;
            put_word(dest+offset, word(dest+offset)+DOS_PSP+16u);
        }
    }
    /* RET at the COM entry stack terminates through PSP:0000 (INT 20h). */
    if (com) put_word(psp+0xFFFE, 0);
    blocks[0] = (block_t){DOS_PSP, allocation, 1};
    block_count = 1;
    if (DOS_PSP+allocation < DOS_TOP)
        blocks[block_count++] = (block_t){DOS_PSP+allocation, DOS_TOP-DOS_PSP-allocation, 0};
    entry[0]=ip; entry[1]=cs; entry[2]=sp; entry[3]=ss;
    entry[4]=DOS_PSP; entry[5]=DOS_PSP;
    return 0;
}

int dos_exec(const uint8_t *image, uint32_t size, int is_com, const dos_filesystem_t *fs) {
    uint32_t entry[6];
    __asm__ volatile("mov %%cr0, %0; mov %%cr3, %1" : "=r"(old_cr0), "=r"(old_cr3));
    error = "Malformed, oversized, or non-DOS executable.";
    dos_status = 0; dos_cancelled = 0;
    if (old_cr0 & 0x80000000u) { error = "Host paging is already enabled."; return -5; }
    if (active || prepare(image, size, is_com, entry)) return -1;
    uint32_t flags = irq_lock();
    while (keyboard_poll() >= 0) {} /* discard launch-time input */
    key_head = key_tail = ticks = 0;
    held_key = -1;
    virtual_if = 1; result = 0; error = "";
    zero((void *)0x10000u, 4096);
    uint8_t *env = (uint8_t *)0x10000u;
    const char environment[] = "PATH=.\0COMSPEC=\0\0\1\0PROGRAM\0";
    for (unsigned i = 0; i < sizeof(environment); i++) env[i] = environment[i];
    old_stack = gdt_set_kernel_stack((uint32_t)(uintptr_t)(monitor_stack+sizeof(monitor_stack)));
    paging_begin();
    dos_services_init(fs);
    active = 1;
    v86_enter(entry);
    irq_unlock(flags);
    return result;
}

static int stack_value(dos_frame_t *f, uint32_t *value, unsigned bytes, int push) {
    uint16_t sp = f->r.useresp;
    if (push) sp -= bytes;
    if ((uint32_t)sp + bytes > 0x10000u) return 0;
    uint8_t *p = dos_pointer(f->r.ss, sp, bytes, push);
    if (!p) return 0;
    if (push) for (unsigned i = 0; i < bytes; i++) p[i] = *value >> (8*i);
    else {
        *value = 0;
        for (unsigned i = 0; i < bytes; i++) *value |= (uint32_t)p[i] << (8*i);
        sp += bytes;
    }
    f->r.useresp = sp;
    return 1;
}
int dos_exception(registers_t *r) {
    if (!active) return 0;
    if (!(r->eflags & DOS_VM)) {
        if (r->int_no == 14) {
            uint32_t addr;
            __asm__ volatile("mov %%cr2, %0" : "=r"(addr));
            if (!(directory[addr >> 22] & PRESENT) && map_region(addr >> 22)) {
                __asm__ volatile("mov %0, %%cr3" :: "r"((uint32_t)(uintptr_t)directory) : "memory");
                return 1;
            }
        }
        return 0; /* do not hide genuine kernel faults */
    }
    if (dos_cancelled) { dos_stop(r, -4, "Stopped with F8."); return 1; }
    /* Some CPUs/emulators deliver the overflow trap directly; others fault
     * the v86 INTO instruction first because guest IOPL is zero. */
    if (r->int_no == 4) return dos_interrupt((dos_frame_t *)r, 4);
    if (r->int_no != 13) {
        char *out = diagnostic_text(diagnostic, "DOS CPU fault ");
        out = diagnostic_hex(out, r->int_no, 2);
        out = diagnostic_text(out, "h ERR=");
        out = diagnostic_hex(out, r->err_code, 8);
        if (r->int_no == 14) {
            uint32_t addr;
            __asm__ volatile("mov %%cr2, %0" : "=r"(addr));
            out = diagnostic_text(out, " ADDR=");
            out = diagnostic_hex(out, addr, 8);
        }
        diagnostic_location(out, r->cs, r->eip);
        dos_stop(r, -3, diagnostic);
        return 1;
    }
    dos_frame_t *f = (dos_frame_t *)r;
    uint8_t instruction[3];
    for (unsigned i = 0; i < 3; i++) {
        uint8_t *byte = dos_pointer(r->cs, (uint16_t)(r->eip+i), 1, 0);
        if (!byte) { dos_stop(r, -3, "Invalid DOS instruction address."); return 1; }
        instruction[i] = *byte;
    }
    uint8_t *p = instruction;
    unsigned prefix = p[0] == 0x66, width = prefix ? 4 : 2;
    uint8_t op = p[prefix];
    uint16_t fault_cs = r->cs, fault_ip = r->eip;
    uint32_t value = 0;
    r->eip = (r->eip + prefix + 1) & 0xFFFF;
    switch (op) {
    case 0xE4: case 0xE5: case 0xE6: case 0xE7:
    case 0xEC: case 0xED: case 0xEE: case 0xEF: {
        if (prefix) goto unsupported;
        uint16_t port = op <= 0xE7 ? p[1] : (uint16_t)r->edx;
        int output = !!(op&2);
        unsigned bytes = (op&1) ? 2 : 1;
        /* Validate both adjacent registers before a word I/O has any effect. */
        if (bytes == 2 && port != 0x3D4) goto unsupported;
        uint32_t input = 0;
        for (unsigned i = 0; i < bytes; i++) {
            uint8_t byte = r->eax>>(i*8);
            if (!dos_legacy_port(port+i, output, &byte)) goto unsupported;
            input |= (uint32_t)byte<<(i*8);
        }
        if (!output) {
            uint32_t mask = bytes == 1 ? 0xFFu : 0xFFFFu;
            r->eax = (r->eax&~mask)|input;
        }
        if (op <= 0xE7) r->eip = (r->eip+1)&0xFFFF;
        break;
    }
    case 0xCD:
        if (prefix) goto unsupported;
        r->eip = (r->eip + 1) & 0xFFFF;
        if (!dos_interrupt(f, p[1])) goto unsupported;
        break;
    case 0xCE:
        if (prefix) goto unsupported; /* only the 16-bit guest frame is supported */
        if ((r->eflags & 0x800u) && !dos_interrupt(f, 4)) goto unsupported;
        break;
    case 0xFA: virtual_if = 0; break;
    case 0xFB: virtual_if = 1; break;
    case 0x9C:
        value = dos_virtual_flags(r);
        if (!stack_value(f, &value, width, 1)) goto bad_stack;
        break;
    case 0x9D:
        if (!stack_value(f, &value, width, 0)) goto bad_stack;
        virtual_if = !!(value & 0x200);
        r->eflags = (value & 0xDD5u) | DOS_VM | 0x202u;
        break;
    case 0xCF:
        if (!stack_value(f, &value, width, 0)) goto bad_stack;
        r->eip = value & 0xFFFF;
        if (!stack_value(f, &value, width, 0)) goto bad_stack;
        r->cs = value & 0xFFFF;
        if (!stack_value(f, &value, width, 0)) goto bad_stack;
        virtual_if = !!(value & 0x200);
        r->eflags = (value & 0xDD5u) | DOS_VM | 0x202u;
        break;
    default: goto unsupported;
    }
    if (dos_cancelled && (r->eflags & DOS_VM)) dos_stop(r, -4, "Stopped with F8.");
    else if ((r->eflags & DOS_VM) && virtual_if && dos_hardware_take_timer())
        dos_inject_interrupt(f, 8);
    return 1;
bad_stack:
    dos_stop(r, -3, "Invalid DOS stack.");
    return 1;
unsupported:
    {
        char *out;
        if (op == 0xCD && !prefix) {
            out = diagnostic_text(diagnostic, "Unsupported INT ");
            out = diagnostic_hex(out, p[1], 2);
            out = diagnostic_text(out, "h");
        } else {
            out = diagnostic_text(diagnostic, "Unsupported opcode ");
            for (unsigned i = 0; i < sizeof(instruction); i++) {
                if (i) *out++ = ' ';
                out = diagnostic_hex(out, instruction[i], 2);
            }
            if ((op >= 0xE4 && op <= 0xE7) || (op >= 0xEC && op <= 0xEF)) {
                out = diagnostic_text(out, " PORT=");
                out = diagnostic_hex(out, op <= 0xE7 ? p[prefix+1] : (uint16_t)r->edx, 4);
            }
        }
        out = diagnostic_text(out, " AX=");
        out = diagnostic_hex(out, r->eax, 4);
        out = diagnostic_text(out, " BX=");
        out = diagnostic_hex(out, r->ebx, 4);
        out = diagnostic_text(out, " CX=");
        out = diagnostic_hex(out, r->ecx, 4);
        out = diagnostic_text(out, " DX=");
        out = diagnostic_hex(out, r->edx, 4);
        diagnostic_location(out, fault_cs, fault_ip);
        dos_stop(r, -2, diagnostic);
    }
    return 1;
}
