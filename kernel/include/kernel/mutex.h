#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include <stdbool.h>

#include <kernel/task.h>
#include <kernel/wait_queue.h>


/*
 * SMP-safe sleeping kernel mutex.
 *
 * This mutex is NON-RECURSIVE.
 *
 * waiters.lock protects all mutex state:
 *
 *     locked
 *     owner
 *     waiter queue
 *
 * mutex_lock() may block and therefore must only
 * be used from normal task context.
 */
typedef struct mutex
{
    bool         locked;

    /*
     * Current owner.
     *
     * NULL when unlocked.
     *
     * Protected by waiters.lock.
     */
    task_t       *owner;

    /*
     * Tasks waiting for ownership.
     *
     * waiters.lock is also the mutex state lock.
     */
    wait_queue_t waiters;

} mutex_t;


/*
 * Initialize before first use.
 */
void mutex_initialize(
    mutex_t *mutex
);


/*
 * Acquire the mutex.
 *
 * May block.
 *
 * Returns false for invalid use or recursive
 * acquisition by the current owner.
 */
bool mutex_lock(
    mutex_t *mutex
);


/*
 * Attempt acquisition without blocking.
 */
bool mutex_try_lock(
    mutex_t *mutex
);


/*
 * Release the mutex.
 *
 * Only the owning task may unlock it.
 */
bool mutex_unlock(
    mutex_t *mutex
);


/*
 * Return the current locked state.
 *
 * Primarily for debugging/tests.
 */
bool mutex_is_locked(
    mutex_t *mutex
);

#endif