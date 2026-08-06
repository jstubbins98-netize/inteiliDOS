/*
 * inteilidOS -- kernel/isr.c
 * ISR/IRQ dispatch and PIC remapping
 */

#include "isr.h"
#include "idt.h"
#include "vga.h"
#include <stdint.h>

/* ---- Port I/O ---- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) { outb(0x80, 0); }

/* ---- 8259 PIC ---- */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_remap(int offset1, int offset2) {
    /* Initialisation sequence (ICW1-4) */
    outb(PIC1_CMD,  0x11); io_wait();
    outb(PIC2_CMD,  0x11); io_wait();
    outb(PIC1_DATA, (uint8_t)offset1); io_wait();   /* ICW2: master vector base */
    outb(PIC2_DATA, (uint8_t)offset2); io_wait();   /* ICW2: slave vector base  */
    outb(PIC1_DATA, 4); io_wait();                  /* ICW3: slave on IRQ2      */
    outb(PIC2_DATA, 2); io_wait();                  /* ICW3: cascade identity   */
    outb(PIC1_DATA, 0x01); io_wait();               /* ICW4: 8086 mode          */
    outb(PIC2_DATA, 0x01); io_wait();

    /* Unmask all IRQs (do NOT restore old mask -- it may have IRQ1 masked) */
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}

/* ---- Handler table ---- */
static isr_handler_t isr_handlers[256];

void isr_register_handler(int num, isr_handler_t handler) {
    if (num >= 0 && num < 256)
        isr_handlers[num] = handler;
}

/* Called from isr_stubs.asm */
void isr_dispatch(registers_t *regs) {
    if (isr_handlers[regs->int_no]) {
        isr_handlers[regs->int_no](regs);
    } else {
        static const char hex[] = "0123456789ABCDEF";
        volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
        uint32_t ino = regs->int_no;
        uint32_t err = regs->err_code;
        uint32_t eip = regs->eip;

        /* ── VGA diagnostic — bottom-right corner, white-on-red ────────────
         *  Row 22 cols 72-79:  "EIP=XXXX" (upper 4 nibbles)
         *  Row 23 cols 72-79:  "    XXXX" (lower 4 nibbles)
         *  Row 24 cols 72-79:  "EXC NN EE"                                 */
        /* Row 22: EIP upper */
        vga[22*80+72] = (uint16_t)(0x4F00u | 'E');
        vga[22*80+73] = (uint16_t)(0x4F00u | 'I');
        vga[22*80+74] = (uint16_t)(0x4F00u | 'P');
        vga[22*80+75] = (uint16_t)(0x4F00u | '=');
        vga[22*80+76] = (uint16_t)(0x4F00u | hex[(eip >> 28) & 0xF]);
        vga[22*80+77] = (uint16_t)(0x4F00u | hex[(eip >> 24) & 0xF]);
        vga[22*80+78] = (uint16_t)(0x4F00u | hex[(eip >> 20) & 0xF]);
        vga[22*80+79] = (uint16_t)(0x4F00u | hex[(eip >> 16) & 0xF]);
        /* Row 23: EIP lower */
        vga[23*80+72] = (uint16_t)(0x4F00u | ' ');
        vga[23*80+73] = (uint16_t)(0x4F00u | ' ');
        vga[23*80+74] = (uint16_t)(0x4F00u | ' ');
        vga[23*80+75] = (uint16_t)(0x4F00u | ' ');
        vga[23*80+76] = (uint16_t)(0x4F00u | hex[(eip >> 12) & 0xF]);
        vga[23*80+77] = (uint16_t)(0x4F00u | hex[(eip >>  8) & 0xF]);
        vga[23*80+78] = (uint16_t)(0x4F00u | hex[(eip >>  4) & 0xF]);
        vga[23*80+79] = (uint16_t)(0x4F00u | hex[ eip        & 0xF]);
        /* Row 24: EXC NN EE */
        vga[24*80+72] = (uint16_t)(0x4F00u | 'E');
        vga[24*80+73] = (uint16_t)(0x4F00u | 'X');
        vga[24*80+74] = (uint16_t)(0x4F00u | 'C');
        vga[24*80+75] = (uint16_t)(0x4F00u | ' ');
        vga[24*80+76] = (uint16_t)(0x4F00u | hex[(ino >> 4) & 0xF]);
        vga[24*80+77] = (uint16_t)(0x4F00u | hex[ ino       & 0xF]);
        vga[24*80+78] = (uint16_t)(0x4F00u | hex[(err >> 4) & 0xF]);
        vga[24*80+79] = (uint16_t)(0x4F00u | hex[ err       & 0xF]);

        /* ── Serial output (COM1 = 0x3F8) ──────────────────────────────────
         * Add  -serial stdio  to the QEMU command to see this in the
         * terminal.  QEMU's virtual UART accepts writes without full init.   */
        {
            /* Minimal COM1 init: 38400 8N1, FIFO enabled */
            outb(0x3F9, 0x00);  /* disable UART interrupts             */
            outb(0x3FB, 0x80);  /* DLAB=1: set baud divisor            */
            outb(0x3F8, 0x03);  /* divisor lo  (38400 baud)            */
            outb(0x3F9, 0x00);  /* divisor hi                          */
            outb(0x3FB, 0x03);  /* 8 bits, no parity, 1 stop bit       */
            outb(0x3FA, 0xC7);  /* enable/clear FIFO, 14-byte thresh   */

            /* Helper: spin until transmitter-holding-register empty   */
#define SERIAL_PUTC(c) do { \
    while (!(inb(0x3FD) & 0x20)) {} \
    outb(0x3F8, (uint8_t)(c));      \
} while (0)

            /* "*** PANIC INT=NN ERR=EE EIP=XXXXXXXX\r\n" */
            static const char hdr[] = "\r\n*** PANIC ***  INT=0x";
            for (const char *p = hdr; *p; p++) SERIAL_PUTC(*p);
            SERIAL_PUTC(hex[(ino >> 4) & 0xF]);
            SERIAL_PUTC(hex[ ino       & 0xF]);
            static const char emsg[] = "  ERR=0x";
            for (const char *p = emsg; *p; p++) SERIAL_PUTC(*p);
            SERIAL_PUTC(hex[(err >> 4) & 0xF]);
            SERIAL_PUTC(hex[ err       & 0xF]);
            static const char eipmsg[] = "  EIP=0x";
            for (const char *p = eipmsg; *p; p++) SERIAL_PUTC(*p);
            for (int i = 28; i >= 0; i -= 4)
                SERIAL_PUTC(hex[(eip >> i) & 0xF]);
            SERIAL_PUTC('\r'); SERIAL_PUTC('\n');

#undef SERIAL_PUTC
        }

        /* ── VGA printf (may fail if VGA state is corrupted) ───────────── */
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
        vga_printf("\n[EXCEPTION %u] err=%u EIP=%x\n",
                   regs->int_no, regs->err_code, regs->eip);
        __asm__ volatile ("cli; hlt");
    }
}

void irq_dispatch(registers_t *regs) {
    /* Send EOI */
    if (regs->int_no >= 40)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);

    if (isr_handlers[regs->int_no])
        isr_handlers[regs->int_no](regs);
}

/* ---- Initialise ISRs ---- */
void isr_init(void) {
    /* Set IDT gates for CPU exceptions 0-31 */
    idt_set_gate( 0, (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate( 1, (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate( 2, (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate( 3, (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate( 4, (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate( 5, (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate( 6, (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate( 7, (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate( 8, (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate( 9, (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);
}

/* ---- Initialise IRQs ---- */
void irq_init(void) {
    pic_remap(0x20, 0x28);   /* remap IRQs: master -> INT 32, slave -> INT 40 */

    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);
}
