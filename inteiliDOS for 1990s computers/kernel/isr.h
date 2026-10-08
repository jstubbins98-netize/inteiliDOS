#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* CPU state pushed by ISR stubs.
 * Layout must exactly match isr_stubs.asm irq_common_stub / isr_common_stub:
 *   push gs / push fs / push es / push ds  → gs at lowest address (esp)
 *   pusha                                  → edi…eax
 *   (stub pre-pushed) int_no, err_code
 *   (CPU auto-pushed) eip, cs, eflags [, useresp, ss on privilege change]
 */
typedef struct {
    uint32_t gs, fs, es, ds;                          /* segment saves     */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pusha             */
    uint32_t int_no, err_code;                        /* stub-pushed       */
    uint32_t eip, cs, eflags, useresp, ss;            /* CPU auto-push     */
} __attribute__((packed)) registers_t;

typedef void (*isr_handler_t)(registers_t *regs);

void isr_init(void);
void irq_init(void);
void isr_register_handler(int num, isr_handler_t handler);

/* ISR stubs (defined in isr_stubs.asm) */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

/* IRQ stubs */
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

#endif /* ISR_H */
