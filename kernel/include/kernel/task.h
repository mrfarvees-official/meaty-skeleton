#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/address_space.h>

typedef uint32_t task_id_t;

struct process;
typedef struct process process_t;

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
    task_id_t id;

    task_state_t state;
    sched_policy_t policy;

    /*
     * Saved kernel stack pointer.
     */
    uintptr_t stack_pointer;

    /*
     * Kernel stack allocation.
     *
     * Every normal task/thread owns its own kernel stack.
     */
    uintptr_t stack_base;
    size_t stack_size;

    /*
     * Address space used while this task executes.
     *
     * Multiple tasks may reference the same address_space_t.
     *
     * This is the foundation required for future userspace
     * multithreading:
     *
     *     one address_space_t
     *          |
     *          +-- task TID 5
     *          +-- task TID 6
     *          +-- task TID 7
     *
     * task_t itself no longer owns a page directory directly.
     */
    address_space_t *address_space;

    /*
     * Owning userspace process.
     *
     * Kernel/internal tasks may keep this NULL for now.
     *
     * If non-NULL, task owns one retained reference to process.
     */
    process_t *process;

    /*
     * Generic scheduler/accounting information.
     */
    int priority;
    uint64_t runtime_ticks;

    /*
     * Kernel-mode entry point used when this task starts.
     */
    void (*entry)(void *);
    void *argument;

    /*
     * Scheduler run-queue linkage.
     */
    struct task *sched_previous;
    struct task *sched_next;

    /*
     * Wait-queue linkage.
     */
    struct task *wait_previous;
    struct task *wait_next;

    /*
     * Sleep-queue linkage.
     */
    struct task *sleep_previous;
    struct task *sleep_next;

    uint64_t wake_tick;

    /*
     * Queue this task is currently waiting on.
     */
    struct wait_queue *waiting_on;

    /*
     * Zombie/reaper linkage.
     *
     * This is completely separate from scheduler/wait/sleep links.
     */
    struct task *cleanup_next;

} task_t;

/*
 * Initialize bootstrap task, idle task, and zombie reaper.
 */
void task_initialize(void);

/*
 * Initialize bootstrap and idle contexts for one already-online AP.
 */
bool task_initialize_cpu(void);

/*
 * --------------------------------------------------------------------------
 * KERNEL TASK CREATION
 * --------------------------------------------------------------------------
 */

task_t *task_create_kernel(
    void (*entry)(void *),
    void *argument);

task_t *task_create_kernel_with_policy(
    void (*entry)(void *),
    void *argument,
    sched_policy_t policy);

/*
 * --------------------------------------------------------------------------
 * USER TASK CREATION
 * --------------------------------------------------------------------------
 *
 * address_space is a BORROWED caller reference.
 *
 * On successful task creation the task retains its own independent
 * reference to address_space.
 *
 * Therefore:
 *
 *     caller reference
 *          +
 *     task reference
 *
 * may coexist.
 *
 * The caller remains responsible for releasing its original reference.
 */

/*
 * Create a userspace task but do not make it runnable yet.
 *
 * The returned task remains TASK_NEW.
 *
 * The task retains one reference to address_space.
 */
task_t *task_create_user_unpublished(
    void (*entry)(void *),
    void *argument,
    address_space_t *address_space,
    sched_policy_t policy);

/*
 * Make a previously-created TASK_NEW task runnable.
 *
 * IMPORTANT:
 *
 * After this call the task may immediately run on another CPU.
 */
void task_publish(
    task_t *task);

/*
 * Convenience API which creates and immediately publishes a user task.
 *
 * The caller still owns its original address_space reference and must
 * release it separately.
 */
task_t *task_create_user_with_policy(
    void (*entry)(void *),
    void *argument,
    address_space_t *address_space,
    sched_policy_t policy);

/*
 * Current task.
 */
task_t *task_current(void);

/*
 * Return the top of this task's managed kernel stack.
 *
 * Returns 0 for bootstrap contexts because their stacks are not owned
 * by the normal task allocator.
 */
uintptr_t task_kernel_stack_top(
    const task_t *task);

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
 *
 * Reaping releases this task's reference to its address space.
 */
void task_exit(void)
    __attribute__((noreturn));

/*
 * --------------------------------------------------------------------------
 * CLEANUP / REAPER DIAGNOSTICS
 * --------------------------------------------------------------------------
 */

size_t task_cleanup_pending_count(void);

uint64_t task_cleanup_total_reaped(void);

size_t task_live_count(void);

/*
 * Internal scheduler handoff helper.
 */
void task_internal_finish_switch(
    task_t *previous);

/*
 * Bind an unpublished userspace task to a process.
 *
 * Requirements:
 *     task->state == TASK_NEW
 *     task->process == NULL
 *     process address space == task address space
 *
 * On success:
 *     process retained
 *     process thread count incremented
 *     task->process assigned
 */
bool task_bind_process(
    task_t *task,
    process_t *process);

#endif