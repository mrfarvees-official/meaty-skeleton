#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "idt.h"

static struct idt_entry idt[IDT_ENTRY_COUNT];
static struct idt_pointer idtr;

extern void *isr_stub_table[32];

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

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)&idt;

    for (uint8_t vector = 0; vector < 32; vector++)
    {
        idt_set_gate(vector, (void (*)(void))isr_stub_table[vector], IDT_INTERRUPT_GATE);
    }

    __asm__ volatile ("lidt %0" : : "m"(idtr));
}