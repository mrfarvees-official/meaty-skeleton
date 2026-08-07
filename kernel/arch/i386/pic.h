#ifndef KERNEL_ARCH_I386_PIC_H
#define KERNEL_ARCH_I386_PIC_H

#include <stdbool.h>
#include <stdint.h>

#define PIC_MASTER_VECTOR_OFFSET 0x20u
#define PIC_SLAVE_VECTOR_OFFSET  0x28u
#define PIC_IRQ_COUNT            16u

void pic_initialize(void);

void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);

bool pic_is_masked(uint8_t irq);

void pic_send_master_eoi(void);
void pic_send_eoi(uint8_t irq);

/*
 * Needed mainly for IRQ7 and IRQ15.
 */
bool pic_is_spurious(uint8_t irq);

static inline uint8_t pic_irq_to_vector(uint8_t irq)
{
    if (irq < 8u)
        return PIC_MASTER_VECTOR_OFFSET + irq;

    return PIC_SLAVE_VECTOR_OFFSET + (irq - 8u);
}

inline bool pic_vector_is_irq(uint32_t vector)
{
    return vector >= PIC_MASTER_VECTOR_OFFSET && vector < PIC_SLAVE_VECTOR_OFFSET + PIC_IRQ_COUNT;
}

inline uint8_t pic_vector_to_irq(uint32_t vector)
{
    return (uint8_t)(vector - PIC_MASTER_VECTOR_OFFSET);
}

#endif