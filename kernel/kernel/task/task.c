#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <kernel/heap.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>

#define KERNEL_TASK_STACK_SIZE (16u * 1024u)

#define INITIAL_EFLAGS 0x202u

static task_t bootstrap_task;

static task_t* current_task = NULL;

static task_id_t next_task_id = 1;

/*
 * Idle task is never inserted into a normal scheduling queue.
 */
static task_t* idle_task = NULL;

static void task_bootstrap(void) __attribute__((noreturn));

static void idle_thread(void* argument)
{
    (void)argument;

    for (;;)
    {
        /*
         * Sleep until an interrupt occurs.
         *
         * After waking we give the scheduler another opportunity.
         */
        __asm__ volatile ("hlt");

        scheduler_yield();
    }
}

void task_internal_set_current(task_t* task)
{
    current_task = task;
}

task_t* task_current(void)
{
    return current_task;
}

static bool initialize_new_stack(task_t* task)
{
    uintptr_t stack_end = task->stack_base + task->stack_size;

    /*
     * Keep the initial stack 16-byte aligned.
     */
    stack_end &= ~(uintptr_t)0xFu;

    uintptr_t* stack = (uintptr_t*)stack_end;

    /*
     * MUST match arch_context_switch().
     *
     * arch_context_switch restores:
     *
     *   pop edi
     *   pop esi
     *   pop ebx
     *   pop ebp
     *   popf
     *   ret
     *
     * Therefore the stack from low to high is:
     *
     *   edi
     *   esi
     *   ebx
     *   ebp
     *   eflags
     *   return address
     */

    *--stack = (uintptr_t)task_bootstrap;
    *--stack = INITIAL_EFLAGS;
    *--stack = 0; // ebp
    *--stack = 0; // ebx
    *--stack = 0; // esi
    *--stack = 0; // edi

    task->stack_pointer = (uintptr_t)stack;

    return true;
}

static task_t* allocate_kernel_task(void (*entry)(void*), void* argument, sched_policy_t policy)
{
    if (entry == NULL) return NULL;

    if (policy < 0 || policy >= SCHED_POLICY_COUNT) return NULL;

    task_t* task = kmalloc(sizeof(task_t));

    if (task == NULL) return NULL;

    memset(task, 0, sizeof(task_t));

    void* stack = kmalloc(KERNEL_TASK_STACK_SIZE);

    if (stack == NULL)
    {
        kfree(task);
        return NULL;
    }

    task->id = next_task_id++;
    task->state = TASK_NEW;
    task->policy = policy;
    task->stack_base = (uintptr_t)stack;
    task->stack_size = KERNEL_TASK_STACK_SIZE;
    task->page_directory = 0;
    task->priority = 0;
    task->runtime_ticks = 0;
    task->entry = entry;
    task->argument = argument;
    task->sched_previous = NULL;
    task->sched_next = NULL;
    task->cleanup_next = NULL;

    if (!initialize_new_stack(task))
    {
        kfree(stack);
        kfree(task);

        return NULL;
    }

    return task;
}

task_t* task_create_kernel_with_policy(void (*entry)(void*), void* argument, sched_policy_t policy)
{
    task_t* task = allocate_kernel_task(entry, argument, policy);

    if (task == NULL) return NULL;

    scheduler_make_ready(task);

    return task;
}

task_t* task_create_kernel(void (*entry)(void*), void* argument)
{
    return task_create_kernel_with_policy(entry, argument, SCHED_POLICY_NORMAL);
}

void task_initialize(void)
{
    memset(&bootstrap_task, 0, sizeof(bootstrap_task));

    /*
     * This represents the kernel code that is already executing.
     *
     * Its stack pointer will be saved automatically during the first
     * context switch.
     */
    bootstrap_task.id             = 0;
    bootstrap_task.state          = TASK_RUNNING;
    bootstrap_task.policy         = SCHED_POLICY_NORMAL;
    bootstrap_task.priority       = 0;
    bootstrap_task.runtime_ticks  = 0;
    bootstrap_task.stack_pointer  = 0;
    bootstrap_task.stack_base     = 0;
    bootstrap_task.stack_size     = 0;
    bootstrap_task.sched_previous = NULL;
    bootstrap_task.sched_next     = NULL;

    current_task = &bootstrap_task;

    /*
     * Create special idle thread.
     */
    idle_task = allocate_kernel_task(idle_thread, NULL, SCHED_POLICY_BACKGROUND);

    if (idle_task == NULL)
    {
        for (;;)
            __asm__ volatile ("cli; hlt");
    }

    /*
     * Idle is NOT inserted into RR.
     *
     * It is scheduler fallback only.
     */
    idle_task->state = TASK_READY;

    scheduler_set_idle_task(idle_task);
}

static void task_bootstrap(void)
{
    task_t* task = task_current();

    if (task == NULL || task->entry == NULL) task_exit();

    task->entry(task->argument);

    /*
     * Returning from a kernel thread means exit.
     */
    task_exit();
}

void task_yield(void)
{
    scheduler_yield();
}

void task_exit(void)
{
    task_t* task = task_current();

    if (task != NULL) task->state = TASK_ZOMBIE;

    /*
     * Do NOT kfree(task->stack_base) here.
     *
     * We are currently executing on that stack.
     *
     * Later we'll add a reaper which destroys zombie tasks from
     * another task's stack.
     */

    scheduler_schedule();

    /*
     * A zombie must never become runnable again.
     */
    for (;;)
        __asm__ volatile ("cli; hlt");
}