#include <stddef.h>
#include <stdint.h>

#include <kernel/sleep_queue.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/timer.h>

#include "../arch/i386/interrupts.h"

static task_t* sleep_head = NULL;
static task_t* sleep_tail = NULL;

static void sleep_queue_insert(task_t* task)
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
     * Keep the queue sorted by wake_tick.
     *
     * Earliest deadline stays at the head.
     */
    task_t* current = sleep_head;

    while (current != NULL && current->wake_tick <= task->wake_tick) current = current->sleep_next;

    /*
     * Append to end.
     */
    if (current == NULL)
    {
        task->sleep_previous = sleep_tail;
        sleep_tail->sleep_next = task;
        sleep_tail = task;

        return;
    }

    /*
     * Insert before current.
     */
    task->sleep_next = current;
    task->sleep_previous = current->sleep_previous;

    if (current->sleep_previous != NULL) current->sleep_previous->sleep_next = task;
    else sleep_head = task;

    current->sleep_previous = task;
}

static task_t* sleep_queue_remove_head(void)
{
    task_t* task = sleep_head;

    if (task == NULL) return NULL;

    sleep_head = task->sleep_next;

    if (sleep_head != NULL) sleep_head->sleep_previous = NULL;
    else sleep_tail = NULL;

    task->sleep_previous = NULL;
    task->sleep_next = NULL;

    return task;
}

void sleep_queue_initialize(void)
{
    sleep_head = NULL;
    sleep_tail = NULL;
}

void task_sleep(uint64_t milliseconds)
{
    /*
     * Sleeping zero milliseconds is effectively a yield.
     */
    if (milliseconds == 0)
    {
        task_yield();
        return;
    }

    uint32_t flags = interrupt_save_disable();

    task_t* current = task_current();

    if (current == NULL)
    {
        interrupt_restore(flags);
        return;
    }

    uint64_t now = timer_ticks();

    uint64_t delay_ticks = timer_ms_to_ticks(milliseconds);

    /*
     * timer_ms_to_ticks() rounds upward, but keep this guard
     * anyway so a non-zero sleep never becomes zero ticks.
     */
    if (delay_ticks == 0) delay_ticks = 1;

    /*
     * Saturating addition avoids wraparound for absurdly large
     * sleep requests.
     */
    if (UINT64_MAX - now < delay_ticks) current->wake_tick = UINT64_MAX;
    else current->wake_tick = now + delay_ticks;

    sleep_queue_insert(current);

    /*
     * Mark TASK_SLEEPING and switch away.
     *
     * Interrupts remain disabled while the task is inserted and
     * transitioned out of RUNNING state, preventing a timer tick
     * from racing with this operation.
     */
    scheduler_block_current(TASK_SLEEPING);

    /*
     * This executes only after the task has been awakened and
     * scheduled again.
     */
    current->wake_tick = 0;

    interrupt_restore(flags);
}

void sleep_queue_tick(uint64_t current_tick)
{
    /*
     * Called from PIT interrupt context.
     *
     * Interrupts are already disabled, so no additional locking
     * is needed for your current single-core kernel.
     */
    while (sleep_head != NULL && sleep_head->wake_tick <= current_tick)
    {
        task_t* task = sleep_queue_remove_head();

        scheduler_wake(task);
    }
}