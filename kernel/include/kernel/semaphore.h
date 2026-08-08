#ifndef KERNEL_SEMAPHORE_H
#define KERNEL_SEMAPHORE_H

#include <stdbool.h>
#include <stddef.h>

#include <kernel/wait_queue.h>


/*
 * Counting semaphore.
 *
 * count:
 *
 *     Number of currently available permits.
 *
 * waiters:
 *
 *     Tasks blocked because count was zero.
 *
 *
 * Example:
 *
 *     semaphore_initialize(&semaphore, 3);
 *
 * allows up to three successful semaphore_wait() operations
 * before another caller must wait for semaphore_signal().
 */
typedef struct semaphore
{
    size_t count;

    wait_queue_t waiters;

} semaphore_t;


/*
 * Initialize a semaphore.
 *
 * initial_count is the number of permits initially available.
 */
void semaphore_initialize(
    semaphore_t *semaphore,
    size_t initial_count
);


/*
 * Acquire one permit.
 *
 * If count > 0:
 *
 *     decrement count and return.
 *
 * If count == 0:
 *
 *     block the current task until a permit becomes available.
 *
 * Returns true when a permit has been acquired.
 * Returns false for invalid usage.
 */
bool semaphore_wait(
    semaphore_t *semaphore
);


/*
 * Attempt to acquire one permit without blocking.
 *
 * Returns true if a permit was acquired.
 * Returns false if count == 0 or the semaphore is invalid.
 */
bool semaphore_try_wait(
    semaphore_t *semaphore
);


/*
 * Return one permit to the semaphore.
 *
 * Wakes one waiting task when waiters exist.
 *
 * Returns false only for invalid input or count overflow.
 */
bool semaphore_signal(
    semaphore_t *semaphore
);


/*
 * Return the current available permit count.
 *
 * Primarily useful for debugging/tests.
 */
size_t semaphore_get_count(
    semaphore_t *semaphore
);


#endif