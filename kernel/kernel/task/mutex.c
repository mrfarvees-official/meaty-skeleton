#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/mutex.h>
#include <kernel/scheduler.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/wait_queue.h>


void mutex_initialize(mutex_t *mutex)
{
    if (mutex == NULL)
        return;

    mutex->locked = false;
    mutex->owner = NULL;

    wait_queue_initialize(
        &mutex->waiters
    );
}


bool mutex_lock(mutex_t *mutex)
{
    if (mutex == NULL)
        return false;

    task_t *current =
        task_current();

    if (current == NULL)
        return false;

    for (;;)
    {
        /*
         * waiters.lock protects:
         *
         *   mutex->locked
         *   mutex->owner
         *   mutex->waiters
         *
         * Using one lock closes the SMP lost-wakeup
         * window between checking the mutex and
         * entering the wait queue.
         */
        uint32_t flags =
            spin_lock_irqsave(
                &mutex->waiters.lock
            );

        /*
         * Non-recursive mutex.
         */
        if (mutex->locked &&
            mutex->owner == current)
        {
            spin_unlock_irqrestore(
                &mutex->waiters.lock,
                flags
            );

            return false;
        }

        /*
         * Mutex available.
         *
         * Claim ownership while still holding the
         * mutex/wait-queue spinlock.
         */
        if (!mutex->locked)
        {
            mutex->locked = true;
            mutex->owner = current;

            spin_unlock_irqrestore(
                &mutex->waiters.lock,
                flags
            );

            return true;
        }

        /*
         * Mutex owned by another task.
         *
         * wait_queue_block_locked() expects
         * waiters.lock to already be held.
         *
         * It atomically:
         *
         *   - adds current to waiters
         *   - changes RUNNING -> BLOCKED
         *   - releases waiters.lock
         *   - switches away
         *
         * When this task runs again we loop and
         * compete for ownership normally.
         */
        wait_queue_block_locked(
            &mutex->waiters,
            flags
        );
    }
}


bool mutex_try_lock(mutex_t *mutex)
{
    if (mutex == NULL)
        return false;

    task_t *current =
        task_current();

    if (current == NULL)
        return false;

    uint32_t flags =
        spin_lock_irqsave(
            &mutex->waiters.lock
        );

    /*
     * Already owned by any task, including current.
     */
    if (mutex->locked)
    {
        spin_unlock_irqrestore(
            &mutex->waiters.lock,
            flags
        );

        return false;
    }

    mutex->locked = true;
    mutex->owner = current;

    spin_unlock_irqrestore(
        &mutex->waiters.lock,
        flags
    );

    return true;
}


bool mutex_unlock(mutex_t *mutex)
{
    if (mutex == NULL)
        return false;

    task_t *current =
        task_current();

    if (current == NULL)
        return false;

    uint32_t flags =
        spin_lock_irqsave(
            &mutex->waiters.lock
        );

    /*
     * Cannot unlock an unlocked mutex.
     */
    if (!mutex->locked)
    {
        spin_unlock_irqrestore(
            &mutex->waiters.lock,
            flags
        );

        return false;
    }

    /*
     * Only the owning task may release it.
     */
    if (mutex->owner != current)
    {
        spin_unlock_irqrestore(
            &mutex->waiters.lock,
            flags
        );

        return false;
    }

    /*
     * Make the mutex available while still holding
     * the same lock that protects the waiter queue.
     */
    mutex->owner = NULL;
    mutex->locked = false;

    /*
     * Remove one waiter while still protected by
     * waiters.lock.
     *
     * Do NOT call scheduler_wake() until after the
     * spinlock has been released.
     */
    task_t *task =
        wait_queue_pop_locked(
            &mutex->waiters
        );

    spin_unlock_irqrestore(
        &mutex->waiters.lock,
        flags
    );

    if (task != NULL)
    {
        scheduler_wake(
            task
        );
    }

    return true;
}


bool mutex_is_locked(mutex_t *mutex)
{
    if (mutex == NULL)
        return false;

    uint32_t flags =
        spin_lock_irqsave(
            &mutex->waiters.lock
        );

    bool locked =
        mutex->locked;

    spin_unlock_irqrestore(
        &mutex->waiters.lock,
        flags
    );

    return locked;
}