#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/heap.h>
#include <kernel/semaphore.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>

#include "../arch/i386/interrupts.h"


#define KERNEL_TASK_STACK_SIZE (16u * 1024u)

#define INITIAL_EFLAGS 0x202u


/*
 * --------------------------------------------------------------------------
 * SPECIAL TASKS
 * --------------------------------------------------------------------------
 */

static task_t bootstrap_task;

static task_t *current_task = NULL;

static task_id_t next_task_id = 1;


/*
 * Idle is never inserted into a normal scheduler queue.
 */
static task_t *idle_task = NULL;


/*
 * Dedicated task which destroys zombies.
 *
 * It must NEVER destroy itself.
 */
static task_t *reaper_task = NULL;


/*
 * --------------------------------------------------------------------------
 * ZOMBIE CLEANUP QUEUE
 * --------------------------------------------------------------------------
 *
 * cleanup_head/cleanup_tail contain TASK_ZOMBIE tasks whose stacks and
 * task_t structures must be freed.
 *
 * The queue is protected by interrupt disabling.
 *
 * That is sufficient for the current single-core kernel.
 */

static task_t *cleanup_head = NULL;
static task_t *cleanup_tail = NULL;


/*
 * Number of zombies that have been queued but not completely destroyed.
 *
 * This includes a task while the reaper is actively freeing it.
 */
static size_t cleanup_pending = 0;


/*
 * Lifetime count of successfully reaped tasks.
 */
static uint64_t cleanup_total_reaped = 0;


/*
 * One semaphore permit is produced for every zombie inserted into the
 * cleanup queue.
 *
 * The reaper blocks here when there is no work.
 */
static semaphore_t cleanup_semaphore;


/*
 * --------------------------------------------------------------------------
 * INTERNAL FUNCTIONS
 * --------------------------------------------------------------------------
 */

static void task_bootstrap(void)
    __attribute__((noreturn));

static void idle_thread(void *argument);

static void task_reaper_thread(void *argument);


/*
 * Add a zombie to the cleanup queue.
 *
 * Caller MUST already have interrupts disabled.
 */
static void cleanup_queue_push(task_t *task)
{
    task->cleanup_next = NULL;

    if (cleanup_tail != NULL)
    {
        cleanup_tail->cleanup_next = task;
    }
    else
    {
        cleanup_head = task;
    }

    cleanup_tail = task;

    ++cleanup_pending;
}


/*
 * Remove one zombie from the cleanup queue.
 *
 * Caller MUST already have interrupts disabled.
 */
static task_t *cleanup_queue_pop(void)
{
    task_t *task = cleanup_head;

    if (task == NULL)
        return NULL;

    cleanup_head =
        task->cleanup_next;

    if (cleanup_head == NULL)
        cleanup_tail = NULL;

    task->cleanup_next = NULL;

    /*
     * IMPORTANT:
     *
     * Do NOT decrement cleanup_pending here.
     *
     * The task has not been completely destroyed yet.
     *
     * cleanup_pending is decremented only AFTER stack/task_t memory
     * has been freed.
     */

    return task;
}


/*
 * Halt forever if a special task incorrectly attempts to terminate.
 *
 * There is no safe meaningful recovery for this stage.
 */
static void task_halt_forever(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}


/*
 * --------------------------------------------------------------------------
 * IDLE THREAD
 * --------------------------------------------------------------------------
 */

static void idle_thread(void *argument)
{
    (void)argument;

    for (;;)
    {
        /*
         * Sleep until an interrupt occurs.
         */
        __asm__ volatile("hlt");

        scheduler_yield();
    }
}


/*
 * --------------------------------------------------------------------------
 * REAPER THREAD
 * --------------------------------------------------------------------------
 */

static void task_reaper_thread(void *argument)
{
    (void)argument;

    for (;;)
    {
        /*
         * Sleep until at least one zombie exists.
         *
         * There is exactly one signal per queued zombie.
         */
        if (!semaphore_wait(&cleanup_semaphore))
        {
            /*
             * This should never fail for the reaper.
             */
            task_yield();
            continue;
        }


        /*
         * Remove exactly one zombie.
         */
        uint32_t flags =
            interrupt_save_disable();

        task_t *task =
            cleanup_queue_pop();

        interrupt_restore(flags);


        if (task == NULL)
        {
            /*
             * Semaphore/queue inconsistency.
             *
             * Don't crash the kernel in this beginner-stage
             * implementation; simply continue.
             */
            continue;
        }


        /*
         * Absolute safety checks.
         *
         * The task being destroyed must:
         *
         *     - not be the current reaper
         *     - not be bootstrap
         *     - not be idle
         *     - be a zombie
         */
        if (task == task_current() ||
            task == &bootstrap_task ||
            task == idle_task ||
            task == reaper_task ||
            task->state != TASK_ZOMBIE)
        {
            /*
             * This represents a serious lifecycle invariant failure.
             *
             * Reinsert nothing and halt rather than freeing unsafe
             * memory.
             */
            task_halt_forever();
        }


        /*
         * We are running on reaper_task's stack here.
         *
         * Therefore it is now safe to free the zombie's stack.
         */
        if (task->stack_base != 0)
        {
            kfree(
                (void *)task->stack_base
            );

            task->stack_base = 0;
            task->stack_size = 0;
            task->stack_pointer = 0;
        }


        /*
         * The task object itself is the final thing we free.
         *
         * Do not access *task after this call.
         */
        kfree(task);


        /*
         * Record completion.
         *
         * Protect 64-bit cleanup_total_reaped because this is i386
         * and 64-bit loads/stores are not inherently atomic.
         */
        flags =
            interrupt_save_disable();

        --cleanup_pending;
        ++cleanup_total_reaped;

        interrupt_restore(flags);


        /*
         * Give normal scheduler activity another opportunity.
         *
         * This is not required for correctness, but keeps a large
         * cleanup burst from monopolizing the CPU.
         */
        task_yield();
    }
}


/*
 * --------------------------------------------------------------------------
 * CURRENT TASK
 * --------------------------------------------------------------------------
 */

void task_internal_set_current(task_t *task)
{
    current_task = task;
}


task_t *task_current(void)
{
    return current_task;
}


/*
 * --------------------------------------------------------------------------
 * INITIAL STACK
 * --------------------------------------------------------------------------
 */

static bool initialize_new_stack(task_t *task)
{
    uintptr_t stack_end =
        task->stack_base +
        task->stack_size;


    /*
     * Keep the initial stack 16-byte aligned.
     */
    stack_end &=
        ~(uintptr_t)0xFu;


    uintptr_t *stack =
        (uintptr_t *)stack_end;


    /*
     * MUST match arch_context_switch().
     *
     * arch_context_switch restores:
     *
     *     pop edi
     *     pop esi
     *     pop ebx
     *     pop ebp
     *     popf
     *     ret
     *
     * Stack from low to high:
     *
     *     edi
     *     esi
     *     ebx
     *     ebp
     *     eflags
     *     return address
     */

    *--stack =
        (uintptr_t)task_bootstrap;

    *--stack =
        INITIAL_EFLAGS;

    *--stack = 0; /* ebp */
    *--stack = 0; /* ebx */
    *--stack = 0; /* esi */
    *--stack = 0; /* edi */


    task->stack_pointer =
        (uintptr_t)stack;


    return true;
}


/*
 * --------------------------------------------------------------------------
 * TASK ALLOCATION
 * --------------------------------------------------------------------------
 */

static task_t *allocate_kernel_task(
    void (*entry)(void *),
    void *argument,
    sched_policy_t policy)
{
    if (entry == NULL)
        return NULL;


    if (policy < 0 ||
        policy >= SCHED_POLICY_COUNT)
    {
        return NULL;
    }


    task_t *task =
        kmalloc(sizeof(task_t));

    if (task == NULL)
        return NULL;


    memset(
        task,
        0,
        sizeof(task_t)
    );


    void *stack =
        kmalloc(KERNEL_TASK_STACK_SIZE);

    if (stack == NULL)
    {
        kfree(task);

        return NULL;
    }


    /*
     * Protect next_task_id against timer preemption.
     *
     * kmalloc() itself is synchronized, but ID allocation is separate
     * shared task-system state.
     */
    uint32_t flags =
        interrupt_save_disable();

    task->id =
        next_task_id++;

    interrupt_restore(flags);


    task->state =
        TASK_NEW;

    task->policy =
        policy;

    task->stack_base =
        (uintptr_t)stack;

    task->stack_size =
        KERNEL_TASK_STACK_SIZE;

    task->page_directory = 0;

    task->priority = 0;

    task->runtime_ticks = 0;

    task->entry =
        entry;

    task->argument =
        argument;


    /*
     * Scheduler linkage.
     */
    task->sched_previous = NULL;
    task->sched_next = NULL;


    /*
     * Wait-queue linkage.
     */
    task->wait_previous = NULL;
    task->wait_next = NULL;
    task->waiting_on = NULL;


    /*
     * Sleep-queue linkage.
     */
    task->sleep_previous = NULL;
    task->sleep_next = NULL;
    task->wake_tick = 0;


    /*
     * Cleanup linkage.
     */
    task->cleanup_next = NULL;


    if (!initialize_new_stack(task))
    {
        kfree(stack);
        kfree(task);

        return NULL;
    }


    return task;
}


/*
 * --------------------------------------------------------------------------
 * PUBLIC CREATION
 * --------------------------------------------------------------------------
 */

task_t *task_create_kernel_with_policy(
    void (*entry)(void *),
    void *argument,
    sched_policy_t policy)
{
    task_t *task =
        allocate_kernel_task(
            entry,
            argument,
            policy
        );


    if (task == NULL)
        return NULL;


    scheduler_make_ready(task);


    return task;
}


task_t *task_create_kernel(
    void (*entry)(void *),
    void *argument)
{
    return task_create_kernel_with_policy(
        entry,
        argument,
        SCHED_POLICY_NORMAL
    );
}


/*
 * --------------------------------------------------------------------------
 * TASK SYSTEM INITIALIZATION
 * --------------------------------------------------------------------------
 */

void task_initialize(void)
{
    memset(
        &bootstrap_task,
        0,
        sizeof(bootstrap_task)
    );


    /*
     * Bootstrap task represents the kernel execution context that
     * already exists before normal tasks are created.
     */
    bootstrap_task.id = 0;

    bootstrap_task.state =
        TASK_RUNNING;

    bootstrap_task.policy =
        SCHED_POLICY_NORMAL;

    bootstrap_task.priority = 0;

    bootstrap_task.runtime_ticks = 0;

    bootstrap_task.stack_pointer = 0;

    bootstrap_task.stack_base = 0;

    bootstrap_task.stack_size = 0;


    bootstrap_task.sched_previous = NULL;
    bootstrap_task.sched_next = NULL;

    bootstrap_task.wait_previous = NULL;
    bootstrap_task.wait_next = NULL;
    bootstrap_task.waiting_on = NULL;

    bootstrap_task.sleep_previous = NULL;
    bootstrap_task.sleep_next = NULL;
    bootstrap_task.wake_tick = 0;

    bootstrap_task.cleanup_next = NULL;


    current_task =
        &bootstrap_task;


    /*
     * Initialize zombie-cleanup state before creating the reaper.
     */
    cleanup_head = NULL;
    cleanup_tail = NULL;

    cleanup_pending = 0;
    cleanup_total_reaped = 0;

    semaphore_initialize(
        &cleanup_semaphore,
        0
    );


    /*
     * --------------------------------------------------------------
     * IDLE TASK
     * --------------------------------------------------------------
     */

    idle_task =
        allocate_kernel_task(
            idle_thread,
            NULL,
            SCHED_POLICY_BACKGROUND
        );


    if (idle_task == NULL)
        task_halt_forever();


    /*
     * Idle is scheduler fallback only.
     */
    idle_task->state =
        TASK_READY;

    scheduler_set_idle_task(
        idle_task
    );


    /*
     * --------------------------------------------------------------
     * REAPER TASK
     * --------------------------------------------------------------
     *
     * IMPORTANT:
     *
     * Use NORMAL policy, not BACKGROUND.
     *
     * Your scheduler strictly checks higher policy bands first.
     * If the reaper were BACKGROUND while NORMAL tasks remained
     * continuously runnable, the reaper could starve forever.
     */

    reaper_task =
        allocate_kernel_task(
            task_reaper_thread,
            NULL,
            SCHED_POLICY_NORMAL
        );


    if (reaper_task == NULL)
        task_halt_forever();


    scheduler_make_ready(
        reaper_task
    );
}


/*
 * --------------------------------------------------------------------------
 * NEW TASK BOOTSTRAP
 * --------------------------------------------------------------------------
 */

static void task_bootstrap(void)
{
    task_t *task =
        task_current();


    if (task == NULL ||
        task->entry == NULL)
    {
        task_exit();
    }


    task->entry(
        task->argument
    );


    /*
     * Returning from a kernel thread means exit.
     */
    task_exit();
}


/*
 * --------------------------------------------------------------------------
 * YIELD
 * --------------------------------------------------------------------------
 */

void task_yield(void)
{
    scheduler_yield();
}


/*
 * --------------------------------------------------------------------------
 * EXIT
 * --------------------------------------------------------------------------
 */

void task_exit(void)
{
    task_t *task =
        task_current();


    /*
     * Special kernel execution contexts must never be destroyed.
     */
    if (task == NULL ||
        task == &bootstrap_task ||
        task == idle_task ||
        task == reaper_task)
    {
        task_halt_forever();
    }


    /*
     * Keep the entire:
     *
     *     mark zombie
     *       ->
     *     enqueue cleanup
     *       ->
     *     notify reaper
     *       ->
     *     schedule away
     *
     * transition protected from timer preemption.
     */
    uint32_t flags =
        interrupt_save_disable();


    task->state =
        TASK_ZOMBIE;


    cleanup_queue_push(task);


    /*
     * One semaphore permit per zombie.
     *
     * semaphore_signal() nests interrupt disabling safely.
     */
    semaphore_signal(
        &cleanup_semaphore
    );


    /*
     * TASK_ZOMBIE is not TASK_RUNNING, therefore scheduler_schedule()
     * will NOT return this task to a runnable queue.
     *
     * The scheduler switches to another task while this stack still
     * exists.
     *
     * Later the reaper frees this stack.
     */
    scheduler_schedule();


    /*
     * Correct execution can NEVER resume here.
     *
     * Do not restore flags because this task must never execute again.
     */
    (void)flags;


    task_halt_forever();
}


/*
 * --------------------------------------------------------------------------
 * CLEANUP DIAGNOSTICS
 * --------------------------------------------------------------------------
 */

size_t task_cleanup_pending_count(void)
{
    uint32_t flags =
        interrupt_save_disable();


    size_t pending =
        cleanup_pending;


    interrupt_restore(flags);


    return pending;
}


uint64_t task_cleanup_total_reaped(void)
{
    /*
     * 64-bit read on i386 requires protection from concurrent update.
     */
    uint32_t flags =
        interrupt_save_disable();


    uint64_t total =
        cleanup_total_reaped;


    interrupt_restore(flags);


    return total;
}