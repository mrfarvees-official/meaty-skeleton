#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t task_id_t;

typedef enum
{
    TASK_NEW = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_ZOMBIE
} task_state_t;


/*
 * Scheduling requirement bands.
 */
typedef enum
{
    SCHED_POLICY_REALTIME = 0,
    SCHED_POLICY_INTERACTIVE,
    SCHED_POLICY_NORMAL,
    SCHED_POLICY_BACKGROUND,

    SCHED_POLICY_COUNT
} sched_policy_t;


struct wait_queue;


typedef struct task
{
    task_id_t           id;

    task_state_t        state;
    sched_policy_t      policy;

    /*
     * Saved kernel stack pointer.
     */
    uintptr_t           stack_pointer;

    /*
     * Stack allocation.
     */
    uintptr_t           stack_base;
    size_t              stack_size;

    /*
     * Address space.
     *
     * Kernel threads currently share the kernel page directory.
     */
    uintptr_t           page_directory;

    /*
     * Generic scheduler/accounting information.
     */
    int                 priority;
    uint64_t            runtime_ticks;

    /*
     * Kernel-thread entry point.
     */
    void                (*entry)(void *);
    void                *argument;

    /*
     * Scheduler run-queue linkage.
     */
    struct task         *sched_previous;
    struct task         *sched_next;

    /*
     * Wait-queue linkage.
     */
    struct task         *wait_previous;
    struct task         *wait_next;

    /*
     * Sleep-queue linkage.
     */
    struct task         *sleep_previous;
    struct task         *sleep_next;

    uint64_t            wake_tick;

    /*
     * Queue this task is currently waiting on.
     */
    struct wait_queue   *waiting_on;

    /*
     * Zombie/reaper linkage.
     *
     * This is completely separate from scheduler/wait/sleep links.
     */
    struct task         *cleanup_next;

} task_t;


/*
 * Initialize bootstrap task, idle task, and zombie reaper.
 */
void task_initialize(void);

/* Initialize bootstrap and idle contexts for one already-online AP. */
bool task_initialize_cpu(void);


/*
 * Create kernel threads.
 */
task_t *task_create_kernel(
    void (*entry)(void *),
    void *argument
);

task_t *task_create_kernel_with_policy(
    void (*entry)(void *),
    void *argument,
    sched_policy_t policy
);


/*
 * Current task.
 */
task_t *task_current(void);


/*
 * Voluntarily yield the CPU.
 */
void task_yield(void);


/*
 * Terminate the current task.
 *
 * Does not return.
 *
 * The task becomes TASK_ZOMBIE and is later destroyed by
 * the reaper from a different kernel stack.
 */
void task_exit(void)
    __attribute__((noreturn));


/*
 * --------------------------------------------------------------------------
 * CLEANUP / REAPER DIAGNOSTICS
 * --------------------------------------------------------------------------
 *
 * Primarily useful for tests/debugging.
 */

/*
 * Number of exited tasks that have been queued for cleanup but have
 * not yet been completely destroyed.
 */
size_t task_cleanup_pending_count(void);


/*
 * Total number of tasks successfully destroyed by the reaper since
 * task initialization.
 */
uint64_t task_cleanup_total_reaped(void);

/* Number of currently allocated kernel task contexts. */
size_t task_live_count(void);

void task_internal_finish_switch(task_t *previous);

#endif
