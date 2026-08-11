#include <stddef.h>
#include <stdint.h>

#include <kernel/wait_queue.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/spinlock.h>

static void wait_queue_push_back(
    wait_queue_t *queue,
    task_t *task)
{
    task->wait_previous =
        queue->tail;

    task->wait_next =
        NULL;

    if (queue->tail != NULL)
    {
        queue->tail->wait_next =
            task;
    }
    else
    {
        queue->head =
            task;
    }

    queue->tail =
        task;

    ++queue->count;
}

static task_t *wait_queue_pop_front(
    wait_queue_t *queue)
{
    task_t *task =
        queue->head;

    if (task == NULL)
        return NULL;

    queue->head =
        task->wait_next;

    if (queue->head != NULL)
    {
        queue->head->wait_previous =
            NULL;
    }
    else
    {
        queue->tail =
            NULL;
    }

    task->wait_previous =
        NULL;

    task->wait_next =
        NULL;

    task->waiting_on =
        NULL;

    --queue->count;

    return task;
}

void wait_queue_initialize(
    wait_queue_t *queue)
{
    if (queue == NULL)
        return;

    queue->head =
        NULL;

    queue->tail =
        NULL;

    queue->count =
        0;

    spinlock_t unlocked =
        SPINLOCK_INITIALIZER;

    queue->lock =
        unlocked;
}

void wait_queue_block(
    wait_queue_t *queue)
{
    if (queue == NULL)
        return;

    uint32_t flags =
        spin_lock_irqsave(
            &queue->lock
        );

    wait_queue_block_locked(
        queue,
        flags
    );
}

task_t *wait_queue_wake_one(
    wait_queue_t *queue)
{
    if (queue == NULL)
        return NULL;

    uint32_t flags =
        spin_lock_irqsave(
            &queue->lock
        );

    task_t *task =
        wait_queue_pop_front(
            queue
        );

    spin_unlock_irqrestore(
        &queue->lock,
        flags
    );

    /*
     * Do not call into the scheduler while holding
     * the wait-queue lock.
     */
    if (task != NULL)
    {
        scheduler_wake(
            task
        );
    }

    return task;
}

size_t wait_queue_wake_all(
    wait_queue_t *queue)
{
    if (queue == NULL)
        return 0;

    size_t awakened =
        0;

    for (;;)
    {
        uint32_t flags =
            spin_lock_irqsave(
                &queue->lock
            );

        task_t *task =
            wait_queue_pop_front(
                queue
            );

        spin_unlock_irqrestore(
            &queue->lock,
            flags
        );

        if (task == NULL)
            break;

        scheduler_wake(
            task
        );

        ++awakened;
    }

    return awakened;
}

size_t wait_queue_count(
    wait_queue_t *queue)
{
    if (queue == NULL)
        return 0;

    uint32_t flags =
        spin_lock_irqsave(
            &queue->lock
        );

    size_t count =
        queue->count;

    spin_unlock_irqrestore(
        &queue->lock,
        flags
    );

    return count;
}

void wait_queue_block_locked(
    wait_queue_t *queue,
    uint32_t lock_flags)
{
    if (queue == NULL)
        return;

    task_t *current =
        task_current();

    if (current == NULL)
    {
        spin_unlock_irqrestore(
            &queue->lock,
            lock_flags
        );

        return;
    }

    if (current->waiting_on != NULL)
    {
        spin_unlock_irqrestore(
            &queue->lock,
            lock_flags
        );

        return;
    }

    current->waiting_on =
        queue;

    wait_queue_push_back(
        queue,
        current
    );

    /*
     * Atomic transition:
     *
     * queue membership published
     * RUNNING -> BLOCKED
     * queue lock released
     * context switched away
     */
    scheduler_block_current_wait(
        TASK_BLOCKED,
        &queue->lock,
        lock_flags
    );
}

task_t *wait_queue_pop_locked(
    wait_queue_t *queue)
{
    if (queue == NULL)
        return NULL;

    return wait_queue_pop_front(
        queue
    );
}