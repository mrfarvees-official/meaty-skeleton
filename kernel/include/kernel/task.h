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
 * This describes scheduling REQUIREMENTS, not the implementation
 * algorithm.
 *
 * For example:
 *
 * REALTIME:
 *     very strict latency requirements
 *
 * INTERACTIVE:
 *     GUI/input/etc.
 *
 * NORMAL:
 *     normal applications and workers
 *
 * BACKGROUND:
 *     work that can wait
 *
 * These are policy bands. They do NOT imply that the current kernel
 * provides true real-time guarantees.
 */
typedef enum
{
    SCHED_POLICY_REALTIME = 0,
    SCHED_POLICY_INTERACTIVE,
    SCHED_POLICY_NORMAL,
    SCHED_POLICY_BACKGROUND,

    SCHED_POLICY_COUNT
} sched_policy_t;

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
     * Stack allocation.
     */
    uintptr_t stack_base;
    size_t stack_size;

    /*
     * Address space.
     *
     * All kernel threads currently share the kernel page directory.
     * Later this can point to a process/address-space object.
     */
    uintptr_t page_directory;

    /*
     * Generic scheduling/accounting information.
     *
     * Do NOT put round-robin specific data here.
     */
    int priority;

    uint64_t runtime_ticks;

    /*
     * Entry point for kernel threads.
     */
    void (*entry)(void*);
    void* argument;

    /*
     * Owned by scheduler algorithms.
     *
     * A task can be on one scheduler run queue at a time.
     */
    struct task* sched_previous;
    struct task* sched_next;

    /*
     * Separate lifetime/cleanup linkage.
     */
    struct task* cleanup_next;
} task_t;

void task_initialize(void);

task_t* task_create_kernel(void (*entry)(void*), void* argument);

task_t* task_create_kernel_with_policy(void (*entry)(void*), void* argument, sched_policy_t policy);

task_t* task_current(void);

void task_yield(void);

void task_exit(void) __attribute__((noreturn));

#endif