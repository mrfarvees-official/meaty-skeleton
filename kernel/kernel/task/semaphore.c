#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/semaphore.h>
#include <kernel/task.h>
#include <kernel/wait_queue.h>

#include "../arch/i386/interrupts.h"


/*
 * --------------------------------------------------------------------------
 * COUNTING SEMAPHORE
 * --------------------------------------------------------------------------
 *
 * This implementation is designed for the current single-core,
 * preemptive kernel.
 *
 *
 * semaphore_wait()
 *
 *     May BLOCK.
 *
 * Therefore:
 *
 *     - do not call semaphore_wait() from an interrupt handler
 *     - do not call semaphore_wait() while holding PMM/paging/heap
 *       spinlocks
 *
 *
 * semaphore_signal()
 *
 *     Does not block.
 *
 *
 * LOST-WAKEUP PROTECTION
 * ----------------------
 *
 * The important operation is:
 *
 *     check count
 *          ->
 *     enter wait queue
 *          ->
 *     become blocked
 *
 * Timer preemption must not occur between those operations.
 *
 * Therefore interrupts remain disabled while checking count and
 * entering wait_queue_block().
 *
 * wait_queue_block() also disables interrupts internally. This
 * nesting is safe because interrupt_restore() restores the saved
 * interrupt state.
 *
 *
 * SMP NOTE
 * --------
 *
 * This is sufficient for the CURRENT single-core kernel.
 *
 * A future SMP implementation will need an internal spinlock around:
 *
 *     semaphore->count
 *     semaphore->waiters
 */


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

    task_t *current =
        task_current();

    if (current == NULL)
        return false;


    /*
     * Protect the check -> block sequence against timer preemption.
     */
    uint32_t flags =
        interrupt_save_disable();


    /*
     * Always re-check after waking.
     *
     * semaphore_signal() adds a permit and wakes one waiter.
     *
     * Before that waiter actually gets CPU time, another runnable
     * task could acquire the permit.
     *
     * Therefore waking does not automatically mean ownership of a
     * permit.
     */
    while (semaphore->count == 0)
    {
        wait_queue_block(
            &semaphore->waiters
        );

        /*
         * When execution resumes here, interrupts are still in the
         * disabled state that existed when wait_queue_block() was
         * entered.
         *
         * Loop and re-check count.
         */
    }


    /*
     * Consume one permit.
     *
     * Interrupts are still disabled, so this operation is atomic
     * relative to other tasks on the current single CPU.
     */
    --semaphore->count;


    interrupt_restore(flags);

    return true;
}


bool semaphore_try_wait(
    semaphore_t *semaphore)
{
    if (semaphore == NULL)
        return false;


    uint32_t flags =
        interrupt_save_disable();


    if (semaphore->count == 0)
    {
        interrupt_restore(flags);

        return false;
    }


    --semaphore->count;


    interrupt_restore(flags);

    return true;
}


bool semaphore_signal(
    semaphore_t *semaphore)
{
    if (semaphore == NULL)
        return false;


    uint32_t flags =
        interrupt_save_disable();


    /*
     * Avoid wrapping SIZE_MAX back to zero.
     */
    if (semaphore->count == SIZE_MAX)
    {
        interrupt_restore(flags);

        return false;
    }


    /*
     * Publish the new permit before waking a waiter.
     */
    ++semaphore->count;


    /*
     * Wake one waiter if one exists.
     *
     * Waking merely makes the task READY.
     * The task will re-check count when it actually runs.
     */
    wait_queue_wake_one(
        &semaphore->waiters
    );


    interrupt_restore(flags);

    return true;
}


size_t semaphore_get_count(
    semaphore_t *semaphore)
{
    if (semaphore == NULL)
        return 0;


    uint32_t flags =
        interrupt_save_disable();


    size_t count =
        semaphore->count;


    interrupt_restore(flags);

    return count;
}