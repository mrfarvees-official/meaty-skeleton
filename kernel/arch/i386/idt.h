#ifndef KERNEL_ARCH_I386_IDT_H
#define KERNEL_ARCH_I386_IDT_H

#include <stdint.h>

#define IDT_ENTRY_COUNT    256
#define IDT_INTERRUPT_GATE 0x8E

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t attributes;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void idt_initialize(void);
void idt_load(void);
void idt_set_gate(uint8_t vector, void (*handler)(void), uint8_t attributes);

#endif
