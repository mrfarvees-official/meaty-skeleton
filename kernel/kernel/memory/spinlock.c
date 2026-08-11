#include <stddef.h>
#include <stdint.h>

#include <kernel/spinlock.h>

#include "../arch/i386/interrupts.h"

static uint32_t atomic_exchange(
    volatile uint32_t *address,
    uint32_t value)
{
    __asm__ volatile(
        "xchgl %0, %1"
        : "+r"(value),
          "+m"(*address)
        :
        : "memory"
    );

    return value;
}

void spinlock_initialize(spinlock_t *lock)
{
    if (lock == NULL)
        return;

    lock->locked = 0;
}

uint32_t spin_lock_irqsave(spinlock_t *lock)
{
    uint32_t flags = interrupt_save_disable();

    while (atomic_exchange(&lock->locked, 1u) != 0u)
    {
        while (lock->locked != 0u)
        {
            __asm__ volatile("pause");
        }
    }

    return flags;
}

void spin_unlock_irqrestore(
    spinlock_t *lock,
    uint32_t flags)
{
    spin_unlock(lock);

    interrupt_restore(flags);
}

void spin_unlock(spinlock_t *lock)
{
    if (lock == NULL)
        return;

    __asm__ volatile("" ::: "memory");

    lock->locked = 0u;
}
