#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "idt.h"
#include "pic.h"
#include "syscall.h"

static struct idt_entry idt[IDT_ENTRY_COUNT];
static struct idt_pointer idtr;

extern void *isr_stub_table[32];
extern void *irq_stub_table[16];
extern void isr_apic_timer(void);
extern void isr_apic_reschedule(void);
extern void isr_128(void);

void idt_set_gate(uint8_t vector, void (*handler)(void), uint8_t attributes)
{
    uint32_t address = (uint32_t)handler;

    idt[vector].offset_low = address & 0xFFFF;
    idt[vector].selector = 0x08;
    idt[vector].zero = 0;
    idt[vector].attributes = attributes;
    idt[vector].offset_high = (address >> 16) & 0xFFFF;
}

void idt_initialize(void)
{
    memset(idt, 0, sizeof(idt));

    idtr.limit = (uint16_t)(sizeof(idt) - 1u);
    idtr.base = (uint32_t)&idt[0];

    /*
     * CPU exceptions.
     *
     * vectors 0-31
     */
    for (uint8_t vector = 0; vector < 32; vector++)
    {
        idt_set_gate(vector, (void (*)(void))isr_stub_table[vector], IDT_INTERRUPT_GATE);
    }

    /*
     * U1 ring-3 transition test.
     *
     * Vector 3 is deliberately callable from CPL3 so a tiny user-mode
     * test can execute INT3 and prove that the interrupt frame records
     * a ring-3 CS.
     *
     * 0xEE:
     *     present
     *     DPL = 3
     *     32-bit interrupt gate
     */
    idt_set_gate(
        3u,
        (void (*)(void))isr_stub_table[3],
        0xEEu);

    /*
     * U2 syscall gate.
     *
     * DPL=3 allows INT 0x80 from user mode.
     * Keep this an interrupt gate for now so IF is cleared while
     * executing the initial syscall entry path.
     */
    idt_set_gate(
        I386_SYSCALL_VECTOR,
        isr_128,
        0xEEu);

    /*
     * Legacy PIC hardware interrupts.
     *
     * IRQ0  -> 0x20
     * IRQ1  -> 0x21
     * ...
     * IRQ15 -> 0x2F
     */
    for (uint8_t irq = 0; irq < PIC_IRQ_COUNT; ++irq)
    {
        idt_set_gate(pic_irq_to_vector(irq), (void (*)(void))irq_stub_table[irq], IDT_INTERRUPT_GATE);
    }

    idt_set_gate(0xF0u, isr_apic_timer, IDT_INTERRUPT_GATE);
    idt_set_gate(0xF1u, isr_apic_reschedule, IDT_INTERRUPT_GATE);

    idt_load();
}

void idt_load(void)
{
    __asm__ volatile("lidt %0" : : "m"(idtr));
}
