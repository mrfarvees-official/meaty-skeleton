#ifndef KERNEL_ARCH_I386_INTERRUPTS_H
#define KERNEL_ARCH_I386_INTERRUPTS_H

#include <stdint.h>

#define INTERRUPT_VECTOR_COUNT 256

struct interrupt_frame {
    /* Saved manually by isr_common.S */
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

     /* Saved by pusha */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    
    /* Pushed by the ISR stub */
    uint32_t vector;
    uint32_t error_code;

    /* Pushed automatically by the CPU */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;

    /*
     * user_esp and user_ss exist only when the interrupt crosses
     * from user mode into kernel mode.
     *
     * Do not read them unless (cs & 3) != 0.
     */
    uint32_t user_esp;
    uint32_t user_ss;
};

typedef void (*interrupt_handler_t)(struct interrupt_frame *frame);

void interrupt_initialization(void);

void interrupt_dispatch(struct interrupt_frame *frame);

int interrupt_register_handler(uint8_t vector, interrupt_handler_t handler);

void interrupt_unregister_handler(uint8_t vector);

uint32_t interrupt_save_disable(void);
void interrupt_restore(uint32_t flags);

void interrupt_disable(void);
void interrupt_enable(void);

#endif