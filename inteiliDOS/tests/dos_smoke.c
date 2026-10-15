/* Boot the actual monitor, ISR trampolines, text/keyboard/timer drivers and
 * ISO9660/FAT12 bridge. Test-only debug ports report results to the runner. */
#include "../kernel/dos.h"
#include "../kernel/dos_internal.h"
#include "../kernel/gdt.h"
#include "../kernel/idt.h"
#include "../kernel/isr.h"
#include "../kernel/timer.h"
#include "../kernel/keyboard.h"
#include "../kernel/vga.h"
#include "../kernel/cdrom.h"
#include "../kernel/ata.h"
#include "../kernel/fdc.h"
#include "../kernel/iso9660.h"
#include "../kernel/fat12.h"
#include "../kernel/loader.h"
#include "../shell/launchpad_dos.h"
#include <stddef.h>
static int force_into_nop, into_nop_traps;
int __real_dos_exception(registers_t *r);
int __wrap_dos_exception(registers_t *r) {
    /* Test-only linker wrapper: reproduce a v86 GP fault on INTO with OF=0.
     * QEMU normally executes that case without entering the monitor. */
    if (force_into_nop && r->int_no == 13 && (r->eflags&DOS_VM) && r->cs == DOS_PSP) {
        uint8_t *code = dos_pointer(r->cs, (uint16_t)r->eip, 3, 1);
        if (code && code[0] == 0xFA && code[1] == 0x90 && code[2] == 0x90) {
            code[0] = 0xCE;
            int handled = __real_dos_exception(r);
            code[0] = 0xFA;
            into_nop_traps++;
            return handled;
        }
    }
    return __real_dos_exception(r);
}
static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v;
}
void *memset(void *p, int c, size_t n) {
    unsigned char *b = p; while (n--) *b++ = c; return p;
}
/* Standalone test links the kernel drivers without the unrelated ATA scanner. */
int ata_get_pata_ports(uint8_t drive, uint16_t *io, uint16_t *ctrl) {
    if (drive>=4) return -1;
    *io=drive<2 ? 0x1F0 : 0x170;
    *ctrl=drive<2 ? 0x3F6 : 0x376;
    return 0;
}
static void log(const char *s) { while (*s) outb(0xE9, *s++); }
static int contains(const char *text, const char *needle) {
    for (; *text; text++) {
        unsigned i = 0;
        while (needle[i] && text[i] == needle[i]) i++;
        if (!needle[i]) return 1;
    }
    return 0;
}
static inline void put32(uint8_t *p, uint32_t n) {
    for (unsigned i=0; i<4; i++) p[i]=n>>(i*8);
}
static void finish(int ok) {
    log(ok ? "PASS\n" : "FAIL\n");
    outb(0xF4, ok ? 0x10 : 0x11);
    for (;;) __asm__ volatile("cli; hlt");
}
static int execute(const char *name, int com, int expected, int status) {
    uint8_t *buf = (uint8_t *)(uintptr_t)IPGM_LOAD_ADDR;
    int32_t bytes;
#if DOS_FLOPPY
    fat12_dirent_t entries[FAT12_MAX_FILES];
    int n = fat12_read_dir(0, entries), found = -1;
    for (int i=0; i<n; i++) {
        unsigned j=0;
        while (name[j] && name[j]==entries[i].name[j]) j++;
        if (!name[j] && !entries[i].name[j]) { found=i; break; }
    }
    if (found<0) { log(name); log(": not on floppy\n"); return 0; }
    bytes = fat12_read_file(0, &entries[found], buf, 524288);
#else
    iso9660_dirent_t ent;
    if (iso9660_find_file(0, name, &ent)!=1) { log(name); log(": not on CD\n"); return 0; }
    bytes = iso9660_read_file(0, &ent, buf, 524288);
#endif
    if (bytes <= 0) { log("read failed\n"); return 0; }
    uint8_t pic1=inb(0x21), pic2=inb(0xA1);
    uint32_t cr0, cr3, flags;
    __asm__ volatile("mov %%cr0,%0; mov %%cr3,%1; pushfl; popl %2"
                     :"=r"(cr0),"=r"(cr3),"=r"(flags));
    log(name); log(": RUN\n");
    force_into_nop = name[0]=='I' && name[4]=='.';
    into_nop_traps = 0;
    int rc=dos_exec(buf, bytes, com, launchpad_dos_source(DOS_FLOPPY ? 4 : 0, "/"));
    if (force_into_nop && into_nop_traps != 1) {
        log("INTO no-overflow GP path was not exercised\n"); return 0;
    }
    force_into_nop = 0;
    uint32_t after0, after3, afterflags;
    __asm__ volatile("mov %%cr0,%0; mov %%cr3,%1; pushfl; popl %2"
                     :"=r"(after0),"=r"(after3),"=r"(afterflags));
    log(name); log(": RETURN\n");
    if (rc != expected || (!rc && dos_exit_code()!=status)) {
        static const char hex[]="0123456789ABCDEF";
        log("wrong result, DOS exit=");
        outb(0xE9,hex[dos_exit_code()>>4]); outb(0xE9,hex[dos_exit_code()&15]);
        log(": "); log(dos_error()); log("\n"); return 0;
    }
    /* Errors must identify the missing service, not just say unsupported. */
    if (expected == -2) {
        const char *detail = name[0]=='P' ? "PORT=0020" :
                             name[0]=='F' ? "PORT=0040" :
                             name[0]=='T' ? "PORT=03D4" :
                             name[0]=='E' ? "INT 21h AX=4B00" :
                                            "INT 10h AX=0013";
        if (!contains(dos_error(), detail) || !contains(dos_error(), " at 2000:")) {
            log("missing diagnostic: "); log(dos_error()); log("\n"); return 0;
        }
    }
    if (expected == -3 && (name[0]=='I' ?
                          !contains(dos_error(), "overflow interrupt 04h") :
                          (!contains(dos_error(), "DOS CPU fault 0Eh") ||
                           !contains(dos_error(), " ADDR=")))) {
        log("missing fault address: "); log(dos_error()); log("\n"); return 0;
    }
    if (cr0!=after0 || cr3!=after3 || ((flags^afterflags)&0x200) ||
        pic1!=inb(0x21) || pic2!=inb(0xA1) || (inb(0x61)&3)) {
        log("host state not restored\n"); return 0;
    }
    return 1;
}
void test_main(void) {
    gdt_init(); idt_init(); isr_init(); irq_init();
    vga_init(); timer_init(1000); keyboard_init();
    __asm__ volatile("sti");
#if DOS_FLOPPY
    if (fdc_init()) { log("fdc init failed\n"); finish(0); }
#else
    cdrom_init();
    if (!cdrom_count()) { log("cd init failed\n"); finish(0); }
#endif
#if DOS_INTERACTIVE
    if (!execute(DOS_INTERACTIVE==1 ? "KEY.COM" : "LOOP.COM",1,-4,0)) finish(0);
#else
    if (!execute("TEST.COM",1,0,42) || !execute("TEST.EXE",0,0,42) ||
        !execute("PORT.COM",1,-2,0) || !execute("FAULT.COM",1,-3,0) ||
        !execute("EXEC.COM",1,-2,0) || !execute("VIDEO.COM",1,-2,0) ||
        !execute("RET.COM",1,0,0) || !execute("KERNEL.COM",1,-3,0) ||
        !execute("BDA.COM",1,0,0) || !execute("CRT.COM",1,0,42) ||
        !execute("TIMING.COM",1,-2,0) || !execute("VIRT.COM",1,0,42) ||
        !execute("FAST.COM",1,-2,0) || !execute("TEST.COM",1,0,42)) finish(0);
    if (!execute("INTO.COM",1,0,42) || !execute("INTOBAD.COM",1,-3,0) ||
        !execute("TEST.COM",1,0,42)) finish(0);
    /* Reject bad images before entering v86. */
    uint8_t bad[32]={0};
    if (dos_exec(bad, sizeof(bad),0,NULL)!=-1 || dos_exec(bad,65537,1,NULL)!=-1) finish(0);
    /* Native IPGM dispatch remains available after multiple guest faults. */
    uint8_t native[17]={'I','P','G','M',1,0,16,0,0,0,0,0,0,0,0,0,0xC3};
    uint8_t *staging=(uint8_t *)(uintptr_t)IPGM_LOAD_ADDR;
    for (unsigned i=0; i<sizeof(native); i++) staging[i]=native[i];
    if (loader_exec(staging,sizeof(native))) { log("native loader failed\n"); finish(0); }
    /* Native ELF at 9 MB, safely outside its file staged at 5 MB. */
    uint8_t elf[85]={0x7F,'E','L','F',1};
    elf[16]=2; elf[18]=3; elf[42]=32; elf[44]=1; elf[84]=0xC3;
    put32(elf+24,0x900000); put32(elf+28,52);
    put32(elf+52,1); put32(elf+56,84); put32(elf+60,0x900000);
    put32(elf+64,0x900000); put32(elf+68,1); put32(elf+72,1);
    for (unsigned i=0; i<sizeof(elf); i++) staging[i]=elf[i];
    if (loader_exec_elf(staging,sizeof(elf))) { log("native ELF failed\n"); finish(0); }
#endif
    finish(1);
}
