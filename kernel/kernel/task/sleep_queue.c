#include <stddef.h>
#include <stdint.h>

#include <kernel/sleep_queue.h>
#include <kernel/scheduler.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/timer.h>

static task_t *sleep_head = NULL;
static task_t *sleep_tail = NULL;

static spinlock_t sleep_lock =
    SPINLOCK_INITIALIZER;


/*
 * sleep_lock MUST already be held.
 */
static void sleep_queue_insert(task_t *task)
{
    task->sleep_previous = NULL;
    task->sleep_next = NULL;

    /*
     * Empty queue.
     */
    if (sleep_head == NULL)
    {
        sleep_head = task;
        sleep_tail = task;
        return;
    }

    /*
     * Keep queue sorted by wake_tick.
     * Earliest deadline remains at the head.
     */
    task_t *current =
        sleep_head;

    while (current != NULL &&
           current->wake_tick <= task->wake_tick)
    {
        current =
            current->sleep_next;
    }

    /*
     * Append to end.
     */
    if (current == NULL)
    {
        task->sleep_previous =
            sleep_tail;

        sleep_tail->sleep_next =
            task;

        sleep_tail =
            task;

        return;
    }

    /*
     * Insert before current.
     */
    task->sleep_next =
        current;

    task->sleep_previous =
        current->sleep_previous;

    if (current->sleep_previous != NULL)
    {
        current->sleep_previous->sleep_next =
            task;
    }
    else
    {
        sleep_head =
            task;
    }

    current->sleep_previous =
        task;
}


/*
 * sleep_lock MUST already be held.
 */
static task_t *sleep_queue_remove_head(void)
{
    task_t *task =
        sleep_head;

    if (task == NULL)
        return NULL;

    sleep_head =
        task->sleep_next;

    if (sleep_head != NULL)
    {
        sleep_head->sleep_previous =
            NULL;
    }
    else
    {
        sleep_tail =
            NULL;
    }

    task->sleep_previous =
        NULL;

    task->sleep_next =
        NULL;

    return task;
}


void sleep_queue_initialize(void)
{
    sleep_head =
        NULL;

    sleep_tail =
        NULL;

    spinlock_t unlocked =
        SPINLOCK_INITIALIZER;

    sleep_lock =
        unlocked;
}


void task_sleep(uint64_t milliseconds)
{
    /*
     * Zero-duration sleep is simply a yield.
     */
    if (milliseconds == 0)
    {
        task_yield();
        return;
    }

    /*
     * Protect the global sleep queue from every CPU,
     * including the CPU handling the timer interrupt.
     *
     * Local interrupt disabling alone is not sufficient
     * on SMP.
     */
    uint32_t flags =
        spin_lock_irqsave(
            &sleep_lock
        );

    task_t *current =
        task_current();

    if (current == NULL)
    {
        spin_unlock_irqrestore(
            &sleep_lock,
            flags
        );

        return;
    }

    uint64_t now =
        timer_ticks();

    uint64_t delay_ticks =
        timer_ms_to_ticks(
            milliseconds
        );

    /*
     * A non-zero sleep must always sleep for at least
     * one timer tick.
     */
    if (delay_ticks == 0)
        delay_ticks = 1;

    /*
     * Saturating addition prevents wake_tick wrapping.
     */
    if (UINT64_MAX - now < delay_ticks)
    {
        current->wake_tick =
            UINT64_MAX;
    }
    else
    {
        current->wake_tick =
            now + delay_ticks;
    }

    /*
     * Publish the task into the sleep queue while
     * sleep_lock is held.
     */
    sleep_queue_insert(
        current
    );

    /*
     * Atomic SMP blocking handoff:
     *
     *   acquire scheduler_lock
     *   RUNNING -> SLEEPING
     *   release sleep_lock
     *   switch away while scheduler_lock remains owned
     *
     * This prevents the timer CPU from removing this
     * task and attempting to wake it before it has
     * actually become TASK_SLEEPING.
     *
     * scheduler_block_current_wait() releases
     * sleep_lock; do NOT unlock it again here.
     */
    scheduler_block_current_wait(
        TASK_SLEEPING,
        &sleep_lock,
        flags
    );

    /*
     * Execution reaches here only after this task has
     * been awakened and scheduled again.
     *
     * The task is no longer in the sleep queue.
     */
    current->wake_tick =
        0;
}


void sleep_queue_tick(uint64_t current_tick)
{
    /*
     * This may execute on one CPU while tasks running
     * on other CPUs enter the sleep queue.
     *
     * Therefore the global queue requires its spinlock
     * even though this function runs in interrupt context.
     */
    for (;;)
    {
        uint32_t flags =
            spin_lock_irqsave(
                &sleep_lock
            );

        /*
         * Nothing expired.
         */
        if (sleep_head == NULL ||
            sleep_head->wake_tick > current_tick)
        {
            spin_unlock_irqrestore(
                &sleep_lock,
                flags
            );

            break;
        }

        task_t *task =
            sleep_queue_remove_head();

        spin_unlock_irqrestore(
            &sleep_lock,
            flags
        );

        /*
         * Never enter scheduler_wake() while holding
         * sleep_lock.
         */
        if (task != NULL)
        {
            scheduler_wake(
                task
            );
        }
    }
}