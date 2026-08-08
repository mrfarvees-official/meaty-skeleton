#include <stddef.h>
#include <stdint.h>

#include <kernel/wait_queue.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>

#include "../arch/i386/interrupts.h"

static void wait_queue_push_back(wait_queue_t* queue, task_t* task)
{
    task->wait_previous = queue->tail;
    task->wait_next = NULL;

    if (queue->tail != NULL) queue->tail->wait_next = task;
    else queue->head = task;

    queue->tail = task;

    ++queue->count;
}

static task_t* wait_queue_pop_front(wait_queue_t* queue)
{
    task_t* task = queue->head;

    if (task == NULL) return NULL;

    queue->head = task->wait_next;

    if (queue->head != NULL) queue->head->wait_previous = NULL;
    else queue->tail = NULL;

    task->wait_previous = NULL;
    task->wait_next = NULL;
    task->waiting_on = NULL;

    --queue->count;

    return task;
}

void wait_queue_initialize(wait_queue_t* queue)
{
    if (queue == NULL) return;

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

void wait_queue_block(wait_queue_t* queue)
{
    if (queue == NULL) return;

    uint32_t flags = interrupt_save_disable();

    task_t* current = task_current();

    if (current == NULL)
    {
        interrupt_restore(flags);
        return;
    }

    /*
     * A task should never be waiting on two queues.
     */
    if (current->waiting_on != NULL)
    {
        interrupt_restore(flags);
        return;
    }

    current->waiting_on = queue;

    wait_queue_push_back(queue, current);

    /*
     * This changes the current task to TASK_BLOCKED and
     * switches to another runnable task.
     *
     * When this task is eventually woken and scheduled again,
     * execution resumes immediately after this call.
     */
    scheduler_block_current(TASK_BLOCKED);

    /*
     * Restore the interrupt state that existed before blocking.
     */
    interrupt_restore(flags);
}

task_t* wait_queue_wake_one(wait_queue_t* queue)
{
    if (queue == NULL) return NULL;

    uint32_t flags = interrupt_save_disable();

    task_t* task = wait_queue_pop_front(queue);

    if (task != NULL) scheduler_wake(task);

    interrupt_restore(flags);

    return task;
}

size_t wait_queue_wake_all(wait_queue_t* queue)
{
    if (queue == NULL) return 0;

    uint32_t flags = interrupt_save_disable();

    size_t awakened = 0;

    for (;;)
    {
        task_t* task = wait_queue_pop_front(queue);

        if (task == NULL) break;

        scheduler_wake(task);

        ++awakened;
    }

    interrupt_restore(flags);

    return awakened;
}

size_t wait_queue_count(const wait_queue_t* queue)
{
    if (queue == NULL) return 0;

    uint32_t flags = interrupt_save_disable();

    size_t count = queue->count;

    interrupt_restore(flags);

    return count;
}