#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/semaphore.h>
#include <kernel/scheduler.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/wait_queue.h>


void semaphore_initialize(
    semaphore_t *semaphore,
    size_t initial_count)
{
    if (semaphore == NULL)
        return;

    semaphore->count =
        initial_count;

    wait_queue_initialize(
        &semaphore->waiters
    );
}


bool semaphore_wait(
    semaphore_t *semaphore)
{
    if (semaphore == NULL)
        return false;

    if (task_current() == NULL)
        return false;

    for (;;)
    {
        /*
         * The wait queue lock is also the semaphore
         * state lock.
         *
         * Therefore count and waiter membership cannot
         * race between CPUs.
         */
        uint32_t flags =
            spin_lock_irqsave(
                &semaphore->waiters.lock
            );

        /*
         * Permit available.
         *
         * Consume it while holding the same lock used
         * by semaphore_signal().
         */
        if (semaphore->count != 0)
        {
            --semaphore->count;

            spin_unlock_irqrestore(
                &semaphore->waiters.lock,
                flags
            );

            return true;
        }

        /*
         * No permit.
         *
         * wait_queue_block_locked() assumes that
         * waiters.lock is already held.
         *
         * It:
         *
         *   - puts current onto the waiter queue
         *   - marks current TASK_BLOCKED
         *   - releases waiters.lock
         *   - switches away
         *
         * There is therefore no check->sleep
         * lost-wakeup window.
         */
        wait_queue_block_locked(
            &semaphore->waiters,
            flags
        );

        /*
         * When this task eventually runs again, start
         * over and compete for a permit.
         *
         * A wakeup does not itself grant ownership of
         * a permit.
         */
    }
}


bool semaphore_try_wait(
    semaphore_t *semaphore)
{
    if (semaphore == NULL)
        return false;

    uint32_t flags =
        spin_lock_irqsave(
            &semaphore->waiters.lock
        );

    if (semaphore->count == 0)
    {
        spin_unlock_irqrestore(
            &semaphore->waiters.lock,
            flags
        );

        return false;
    }

    --semaphore->count;

    spin_unlock_irqrestore(
        &semaphore->waiters.lock,
        flags
    );

    return true;
}


bool semaphore_signal(
    semaphore_t *semaphore)
{
    if (semaphore == NULL)
        return false;

    /*
     * Protect both count and waiter removal with the
     * same lock.
     */
    uint32_t flags =
        spin_lock_irqsave(
            &semaphore->waiters.lock
        );

    if (semaphore->count == SIZE_MAX)
    {
        spin_unlock_irqrestore(
            &semaphore->waiters.lock,
            flags
        );

        return false;
    }

    /*
     * Publish the permit before making a waiter READY.
     */
    ++semaphore->count;

    /*
     * Remove a waiter while still holding the same lock
     * that protects count.
     *
     * Do NOT call scheduler_wake() while holding this
     * lock.
     */
    task_t *task =
        wait_queue_pop_locked(
            &semaphore->waiters
        );

    spin_unlock_irqrestore(
        &semaphore->waiters.lock,
        flags
    );

    /*
     * Scheduler locking happens after the semaphore/
     * wait-queue lock has been released.
     */
    if (task != NULL)
    {
        scheduler_wake(
            task
        );
    }

    return true;
}


size_t semaphore_get_count(
    semaphore_t *semaphore)
{
    if (semaphore == NULL)
        return 0;

    uint32_t flags =
        spin_lock_irqsave(
            &semaphore->waiters.lock
        );

    size_t count =
        semaphore->count;

    spin_unlock_irqrestore(
        &semaphore->waiters.lock,
        flags
    );

    return count;
}