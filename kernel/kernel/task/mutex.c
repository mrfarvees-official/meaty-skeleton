#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/mutex.h>
#include <kernel/task.h>
#include <kernel/wait_queue.h>

#include "../arch/i386/interrupts.h"


/*
 * --------------------------------------------------------------------------
 * MUTEX MODEL
 * --------------------------------------------------------------------------
 *
 * This mutex is intended for normal kernel TASK context.
 *
 * It may block, therefore:
 *
 *     DO NOT use mutex_lock() from an IRQ handler.
 *
 *     DO NOT call mutex_lock() while holding one of the low-level
 *     PMM/paging/heap spinlocks.
 *
 *
 * Why interrupts are briefly disabled:
 *
 * Consider:
 *
 *     Task A:
 *
 *         sees mutex locked
 *
 *                    <--- preemption here
 *
 *     Task B:
 *
 *         unlocks mutex
 *         wake_one() sees nobody waiting
 *
 *     Task A:
 *
 *         enters wait queue
 *         sleeps forever
 *
 * That is a lost wakeup.
 *
 * We prevent it by keeping interrupts disabled across:
 *
 *     inspect mutex state
 *            ->
 *     enqueue current task
 *            ->
 *     block current task
 *
 * wait_queue_block() itself also disables interrupts. Nested interrupt
 * disabling is safe because interrupt_restore() restores the original
 * saved EFLAGS state.
 *
 *
 * This is correct for the CURRENT SINGLE-CORE kernel.
 *
 * An SMP implementation will later require an internal atomic/spinlock
 * protecting mutex state and its wait queue.
 */


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


    /*
     * Disable preemption while examining the mutex and possibly
     * entering its wait queue.
     *
     * This closes the lost-wakeup window.
     */
    uint32_t flags =
        interrupt_save_disable();


    /*
     * Non-recursive mutex.
     *
     * Trying to lock a mutex already owned by this task would
     * otherwise cause the task to sleep waiting for itself.
     */
    if (mutex->locked &&
        mutex->owner == current)
    {
        interrupt_restore(flags);

        return false;
    }


    /*
     * Always re-check after waking.
     *
     * mutex_unlock() makes the mutex available and wakes one waiter,
     * but another runnable task could acquire it before this task
     * actually gets scheduled again.
     */
    while (mutex->locked)
    {
        /*
         * interrupts are already disabled here.
         *
         * wait_queue_block() saves that disabled state, places this
         * task onto the queue, changes it to TASK_BLOCKED, and switches
         * away.
         *
         * When eventually scheduled again, wait_queue_block() returns
         * with interrupts still disabled because that was the state on
         * entry.
         */
        wait_queue_block(
            &mutex->waiters
        );

        /*
         * We are running again.
         *
         * Loop and re-check mutex->locked.
         */
    }


    /*
     * Mutex is available.
     *
     * Interrupts remain disabled, so no other task on this single CPU
     * can acquire it between these assignments.
     */
    mutex->locked = true;
    mutex->owner = current;


    interrupt_restore(flags);

    return true;
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
        interrupt_save_disable();


    /*
     * Already locked by somebody, including ourselves.
     */
    if (mutex->locked)
    {
        interrupt_restore(flags);

        return false;
    }


    mutex->locked = true;
    mutex->owner = current;


    interrupt_restore(flags);

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
        interrupt_save_disable();


    /*
     * An unlocked mutex cannot be unlocked again.
     */
    if (!mutex->locked)
    {
        interrupt_restore(flags);

        return false;
    }


    /*
     * Only the owner may release the mutex.
     */
    if (mutex->owner != current)
    {
        interrupt_restore(flags);

        return false;
    }


    /*
     * Make mutex available BEFORE waking a waiter.
     *
     * The woken task will loop and acquire it when it gets CPU time.
     */
    mutex->owner = NULL;
    mutex->locked = false;


    /*
     * Wake the oldest waiter.
     *
     * wait_queue_wake_one() performs its own nested interrupt
     * disable/restore.
     */
    wait_queue_wake_one(
        &mutex->waiters
    );


    interrupt_restore(flags);

    return true;
}


bool mutex_is_locked(mutex_t *mutex)
{
    if (mutex == NULL)
        return false;


    uint32_t flags =
        interrupt_save_disable();

    bool locked =
        mutex->locked;

    interrupt_restore(flags);

    return locked;
}