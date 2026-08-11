#ifndef KERNEL_WAIT_QUEUE_H
#define KERNEL_WAIT_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include <kernel/task.h>
#include <kernel/spinlock.h>

/*
 * FIFO queue of blocked tasks.
 *
 * This structure does NOT schedule tasks itself.
 *
 * Responsibilities:
 *
 *   wait queue:
 *       remembers WHO is waiting
 *
 *   scheduler:
 *       controls whether a task is
 *       RUNNING / READY / BLOCKED
 */
typedef struct wait_queue
{
    task_t      *head;
    task_t      *tail;
    size_t      count;
    spinlock_t  lock;

} wait_queue_t;

/*
 * Initialize an empty wait queue.
 *
 * Must be called before the queue is used.
 */
void wait_queue_initialize(
    wait_queue_t *queue);

/*
 * Block the currently-running task on this queue.
 *
 * Execution returns from this function only after another task
 * or IRQ wakes this task and the scheduler eventually runs it
 * again.
 */
void wait_queue_block(
    wait_queue_t *queue);

/*
 * Wake the oldest task waiting on the queue.
 *
 * Returns:
 *
 *     awakened task
 *
 * or:
 *
 *     NULL
 *
 * if nobody was waiting.
 */
task_t *wait_queue_wake_one(
    wait_queue_t *queue);

/*
 * Wake every task currently waiting.
 *
 * Returns the number of tasks awakened.
 */
size_t wait_queue_wake_all(
    wait_queue_t *queue);

/*
 * Number of tasks currently waiting.
 */
size_t wait_queue_count(
    wait_queue_t *queue);

/*
 * Caller MUST already hold queue->lock.
 *
 * Blocks the current task and releases queue->lock as part
 * of the scheduler blocking handoff.
 */
void wait_queue_block_locked(
    wait_queue_t *queue,
    uint32_t lock_flags
);

/*
 * Caller MUST already hold queue->lock.
 *
 * Removes one waiter from the queue but does NOT call
 * scheduler_wake().
 */
task_t *wait_queue_pop_locked(
    wait_queue_t *queue
);

#endif