#ifndef KERNEL_SEMAPHORE_H
#define KERNEL_SEMAPHORE_H

#include <stdbool.h>
#include <stddef.h>

#include <kernel/wait_queue.h>

typedef struct semaphore
{
    /*
     * Protected by waiters.lock.
     */
    size_t      count;

    /*
     * waiters.lock is also the semaphore's state lock.
     *
     * This makes:
     *
     *     check count
     *     enqueue waiter
     *     become blocked
     *
     * one SMP-safe operation.
     */
    wait_queue_t waiters;

} semaphore_t;


/*
 * Initialize a counting semaphore.
 */
void semaphore_initialize(
    semaphore_t *semaphore,
    size_t initial_count
);


/*
 * Acquire one permit.
 *
 * May block.
 * Must not be called from interrupt context.
 */
bool semaphore_wait(
    semaphore_t *semaphore
);


/*
 * Attempt to acquire one permit without blocking.
 */
bool semaphore_try_wait(
    semaphore_t *semaphore
);


/*
 * Return one permit.
 *
 * Does not block.
 */
bool semaphore_signal(
    semaphore_t *semaphore
);


/*
 * Return the currently available permit count.
 */
size_t semaphore_get_count(
    semaphore_t *semaphore
);

#endif