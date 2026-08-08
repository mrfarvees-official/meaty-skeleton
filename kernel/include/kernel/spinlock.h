#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <stdint.h>

typedef struct
{
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INITIALIZER { 0u }

void spinlock_initialize(spinlock_t *lock);

uint32_t spin_lock_irqsave(spinlock_t *lock);

void spin_unlock_irqrestore(
    spinlock_t *lock,
    uint32_t flags
);

#endif