#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include <stdbool.h>

#include <kernel/task.h>
#include <kernel/wait_queue.h>


/*
 * Sleeping kernel mutex.
 *
 * This implementation is currently designed for the kernel's
 * single-core preemptive scheduler.
 *
 * A task that cannot acquire the mutex is placed on waiters and
 * blocked rather than spinning.
 *
 * This mutex is NON-RECURSIVE.
 */
typedef struct mutex
{
    bool locked;

    /*
     * Task currently owning the mutex.
     *
     * NULL when unlocked.
     */
    task_t *owner;

    /*
     * Tasks waiting for the mutex.
     */
    wait_queue_t waiters;

} mutex_t;


/*
 * Initialize a mutex before first use.
 */
void mutex_initialize(mutex_t *mutex);


/*
 * Acquire mutex.
 *
 * Returns true when ownership has been acquired.
 *
 * Returns false for invalid usage such as:
 *
 *     - mutex == NULL
 *     - no current task
 *     - current task already owns this mutex
 *
 * If another task owns the mutex, the current task sleeps until
 * the mutex becomes available.
 */
bool mutex_lock(mutex_t *mutex);


/*
 * Try to acquire without blocking.
 *
 * Returns true if acquired.
 * Returns false if unavailable or invalid.
 */
bool mutex_try_lock(mutex_t *mutex);


/*
 * Release mutex.
 *
 * Only the owning task may unlock it.
 *
 * Returns true on success.
 * Returns false if the mutex is invalid, unlocked, or owned by
 * another task.
 */
bool mutex_unlock(mutex_t *mutex);


/*
 * Query the current locked state.
 *
 * Intended mainly for debugging/tests.
 */
bool mutex_is_locked(mutex_t *mutex);


#endif