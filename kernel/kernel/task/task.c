#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/heap.h>
#include <kernel/semaphore.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <kernel/cpu.h>
#include <kernel/spinlock.h>
#include <kernel/paging.h>
#include <kernel/address_space.h>
#include <kernel/process.h>
#include <kernel/logger.h>

#include "../arch/i386/interrupts.h"

/*
 * Kernel task stack.
 *
 * Filesystem syscalls currently traverse ext2 code which still uses
 * several 4 KiB automatic I/O buffers in nested call chains.
 *
 * 16 KiB is therefore insufficient and can allow the kernel stack to
 * run below its allocation, corrupting the adjacent task_t/heap data.
 *
 * Keep this comfortably larger until ext2 scratch buffers are moved
 * off task stacks.
 */
#define KERNEL_TASK_STACK_SIZE (64u * 1024u)

#define INITIAL_EFLAGS 0x002u

/*
 * --------------------------------------------------------------------------
 * SPECIAL TASKS
 * --------------------------------------------------------------------------
 */

static task_t bootstrap_tasks[SMP_MAX_CPUS];

static task_id_t next_task_id = 1;
static size_t live_task_count = 0;

static spinlock_t task_id_lock = SPINLOCK_INITIALIZER;
static spinlock_t cleanup_lock = SPINLOCK_INITIALIZER;

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
 * cleanup_head / cleanup_tail / cleanup_pending /
 * cleanup_total_reaped are protected by cleanup_lock.
 *
 * The lock is SMP-safe and also disables local interrupts
 * while held through spin_lock_irqsave().
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

static void task_first_entry(void)
    __attribute__((noreturn));

static void idle_thread(void *argument);

static void task_reaper_thread(void *argument);

/*
 * Add a zombie to the cleanup queue.
 *
 * Caller MUST hold cleanup_lock.
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
 * Caller MUST hold cleanup_lock.
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

static task_t *task_bootstrap_for_cpu(size_t cpu_index)
{
    if (cpu_index >= SMP_MAX_CPUS)
        return NULL;

    return &bootstrap_tasks[cpu_index];
}

static bool task_is_bootstrap_task(const task_t *task)
{
    if (task == NULL)
        return false;

    size_t count = smp_cpu_count();

    for (size_t i = 0; i < count; ++i)
        if (&bootstrap_tasks[i] == task)
            return true;

    return false;
}

void task_internal_finish_switch(task_t *previous)
{
    if (previous == NULL)
        return;

    /*
     * Normal context switch.
     * Only exited tasks require deferred cleanup.
     */
    if (previous->state != TASK_ZOMBIE)
        return;

    /*
     * We are already executing on another task's stack.
     * It is now safe to expose this task to the reaper.
     */
    uint32_t flags =
        spin_lock_irqsave(
            &cleanup_lock);

    cleanup_queue_push(
        previous);

    spin_unlock_irqrestore(
        &cleanup_lock,
        flags);

    semaphore_signal(
        &cleanup_semaphore);
}

static bool task_is_idle_task(const task_t *task)
{
    if (task == NULL)
        return false;

    size_t count = smp_cpu_count();

    for (size_t i = 0; i < count; ++i)
    {
        cpu_local_t *cpu = cpu_get(i);

        if (cpu != NULL && cpu->idle_task == task)
            return true;
    }

    return false;
}

/*
 * Halt forever if a special task incorrectly attempts to terminate.
 *
 * There is no safe meaningful recovery for this stage.
 */
static void task_halt_forever(void)
    __attribute__((noreturn));

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
        __asm__ volatile(
            "sti; hlt"
            :
            :
            : "memory");

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
         * There is exactly one semaphore signal per queued zombie.
         */
        if (!semaphore_wait(
                &cleanup_semaphore))
        {
            task_yield();
            continue;
        }

        /*
         * Remove exactly one zombie from the cleanup queue.
         */
        uint32_t flags =
            spin_lock_irqsave(
                &cleanup_lock);

        task_t *task =
            cleanup_queue_pop();

        spin_unlock_irqrestore(
            &cleanup_lock,
            flags);

        if (task == NULL)
        {
            /*
             * Semaphore/queue inconsistency.
             */
            continue;
        }

        /*
         * Absolute lifecycle safety checks.
         */
        if (cpu_current() == NULL)
            task_halt_forever();

        if (task == task_current() ||
            task_is_bootstrap_task(task) ||
            task_is_idle_task(task) ||
            task == reaper_task ||
            task->state != TASK_ZOMBIE)
        {
            task_halt_forever();
        }

        /*
         * Drop this task/thread's reference to its address space.
         *
         * For a userspace address space:
         *
         *     refs > 1
         *         another task still uses it, so it survives
         *
         *     refs == 1
         *         this is the final task/reference, so releasing
         *         it destroys the private page directory
         *
         * Kernel address space is immortal.
         *
         * The reaper itself executes in the canonical kernel
         * address space, which satisfies final-release requirements.
         */
        /*
         * Drop this task's membership/reference to its process.
         */
        process_t *process =
            task->process;

        task->process =
            NULL;

        if (process != NULL)
        {
            process_thread_detach(
                process);
        }

        /*
         * Drop this task/thread's reference to its address space.
         */
        address_space_t *address_space =
            task->address_space;

        if (address_space == NULL)
            task_halt_forever();

        task->address_space =
            NULL;

        if (!address_space_release(
                address_space))
        {
            task_halt_forever();
        }

        /*
         * Drop this task's retained process reference.
         */
        if (process != NULL)
        {
            process_release(
                process);
        }

        /*
         * We are running on reaper_task's stack, so the zombie's
         * managed kernel stack can now be released safely.
         */
        if (task->stack_base != 0)
        {
            kfree(
                (void *)task->stack_base);

            task->stack_base = 0;
            task->stack_size = 0;
            task->stack_pointer = 0;
        }

        /*
         * task_t is the final object released.
         */
        kfree(task);

        flags =
            spin_lock_irqsave(
                &task_id_lock);

        --live_task_count;

        spin_unlock_irqrestore(
            &task_id_lock,
            flags);

        /*
         * Record completed cleanup.
         */
        flags =
            spin_lock_irqsave(
                &cleanup_lock);

        --cleanup_pending;
        ++cleanup_total_reaped;

        spin_unlock_irqrestore(
            &cleanup_lock,
            flags);

        /*
         * Avoid monopolizing the CPU during cleanup bursts.
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
    cpu_local_t *cpu = cpu_current();

    if (cpu == NULL)
        return;

    cpu->current_task = task;
}

task_t *task_current(void)
{
    cpu_local_t *cpu = cpu_current();

    if (cpu == NULL)
        return NULL;

    return cpu->current_task;
}

uintptr_t task_kernel_stack_top(
    const task_t *task)
{
    if (task == NULL)
        return 0;

    if (task->stack_base == 0 ||
        task->stack_size == 0)
    {
        return 0;
    }

    uintptr_t stack_top =
        task->stack_base +
        task->stack_size;

    /*
     * Match initialize_new_stack().
     */
    stack_top &=
        ~(uintptr_t)0xFu;

    return stack_top;
}

/*
 * --------------------------------------------------------------------------
 * INITIAL STACK
 * --------------------------------------------------------------------------
 */

static bool initialize_new_stack(
    task_t *task)
{
    if (task == NULL)
        return false;

    uintptr_t stack_end =
        task->stack_base +
        task->stack_size;

    /*
     * Keep initial ESP 16-byte aligned.
     */
    stack_end &=
        ~(uintptr_t)0xFu;

    uintptr_t *stack =
        (uintptr_t *)stack_end;

    /*
     * arch_context_switch() restores:
     *
     *     pop edi
     *     pop esi
     *     pop ebx
     *     pop ebp
     *     popf
     *     ret
     *
     * A brand-new task must enter task_first_entry(),
     * not task_bootstrap() directly, because the former
     * completes the scheduler lock handoff.
     */

    *--stack =
        (uintptr_t)task_first_entry;

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

static task_t *allocate_task(
    void (*entry)(void *),
    void *argument,
    address_space_t *address_space,
    sched_policy_t policy)
{
    if (entry == NULL ||
        address_space == NULL)
    {
        return NULL;
    }

    if (policy < 0 ||
        policy >= SCHED_POLICY_COUNT)
    {
        return NULL;
    }

    uintptr_t page_directory =
        address_space_page_directory(
            address_space);

    if (page_directory == 0 ||
        (page_directory &
         (PAGE_SIZE - 1u)) != 0)
    {
        return NULL;
    }

    /*
     * The caller owns one reference.
     *
     * The new task must obtain an independent reference so its
     * address-space lifetime does not depend on the creator.
     */
    if (!address_space_retain(
            address_space))
    {
        return NULL;
    }

    task_t *task =
        kmalloc(
            sizeof(task_t));

    if (task == NULL)
    {
        address_space_release(
            address_space);

        return NULL;
    }

    memset(
        task,
        0,
        sizeof(task_t));

    void *stack =
        kmalloc(
            KERNEL_TASK_STACK_SIZE);

    if (stack == NULL)
    {
        kfree(task);

        address_space_release(
            address_space);

        return NULL;
    }

    /*
     * Protect next_task_id against concurrent allocation
     * from multiple CPUs.
     */
    uint32_t flags =
        spin_lock_irqsave(
            &task_id_lock);

    task->id =
        next_task_id++;

    spin_unlock_irqrestore(
        &task_id_lock,
        flags);

    task->state =
        TASK_NEW;

    task->policy =
        policy;

    task->stack_base =
        (uintptr_t)stack;

    task->stack_size =
        KERNEL_TASK_STACK_SIZE;

    /*
     * The task now holds its own reference.
     *
     * Several tasks may legally point at this same object.
     */
    task->address_space =
        address_space;

    task->priority = 0;
    task->runtime_ticks = 0;

    task->entry =
        entry;

    task->argument =
        argument;

    task->sched_previous = NULL;
    task->sched_next = NULL;

    task->wait_previous = NULL;
    task->wait_next = NULL;
    task->waiting_on = NULL;

    task->sleep_previous = NULL;
    task->sleep_next = NULL;
    task->wake_tick = 0;

    task->cleanup_next = NULL;

    if (!initialize_new_stack(
            task))
    {
        task->address_space =
            NULL;

        kfree(stack);
        kfree(task);

        address_space_release(
            address_space);

        return NULL;
    }

    flags =
        spin_lock_irqsave(
            &task_id_lock);

    ++live_task_count;

    spin_unlock_irqrestore(
        &task_id_lock,
        flags);

    return task;
}

static void destroy_unpublished_task(
    task_t *task)
{
    if (task == NULL)
        return;

    process_t *process =
        task->process;

    task->process =
        NULL;

    address_space_t *address_space =
        task->address_space;

    task->address_space =
        NULL;

    if (task->stack_base != 0)
    {
        kfree(
            (void *)task->stack_base);

        task->stack_base =
            0;

        task->stack_size =
            0;

        task->stack_pointer =
            0;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &task_id_lock);

    if (live_task_count == 0u)
    {
        spin_unlock_irqrestore(
            &task_id_lock,
            flags);

        task_halt_forever();
    }

    --live_task_count;

    spin_unlock_irqrestore(
        &task_id_lock,
        flags);

    if (process != NULL)
    {
        process_thread_detach(
            process);

        process_release(
            process);
    }

    /*
     * Drop exactly the one address-space reference
     * owned by this unpublished task.
     */
    if (address_space != NULL)
    {
        if (!address_space_release(
                address_space))
        {
            task_halt_forever();
        }
    }

    kfree(
        task);
}

static task_t *allocate_kernel_task(
    void (*entry)(void *),
    void *argument,
    sched_policy_t policy)
{
    address_space_t *kernel_space =
        address_space_kernel();

    if (kernel_space == NULL)
        return NULL;

    return allocate_task(
        entry,
        argument,
        kernel_space,
        policy);
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
    address_space_t *kernel_space =
        address_space_kernel();

    if (kernel_space == NULL)
        return NULL;

    task_t *task =
        allocate_task(
            entry,
            argument,
            kernel_space,
            policy);

    if (task == NULL)
        return NULL;

    scheduler_make_ready(
        task);

    return task;
}

task_t *task_create_user_unpublished(
    void (*entry)(void *),
    void *argument,
    address_space_t *address_space,
    sched_policy_t policy)
{
    uintptr_t kernel_directory =
        paging_kernel_directory();

    if (kernel_directory == 0)
        return NULL;

    /*
     * User-task construction currently occurs from
     * the canonical kernel address space.
     */
    if (paging_current_directory() !=
        kernel_directory)
    {
        return NULL;
    }

    if (address_space == NULL)
        return NULL;

    /*
     * A userspace task may not use the canonical
     * kernel address space.
     */
    if (address_space_is_kernel(
            address_space))
    {
        return NULL;
    }

    uintptr_t page_directory =
        address_space_page_directory(
            address_space);

    if (page_directory == 0 ||
        page_directory ==
            kernel_directory ||
        (page_directory &
         (PAGE_SIZE - 1u)) != 0)
    {
        return NULL;
    }

    /*
     * allocate_task() takes an independent reference
     * to address_space.
     *
     * The caller keeps its original reference.
     */
    task_t *task =
        allocate_task(
            entry,
            argument,
            address_space,
            policy);

    if (task == NULL)
        return NULL;

    if (task->stack_base == 0 ||
        task->stack_size == 0)
    {
        destroy_unpublished_task(
            task);

        return NULL;
    }

    if (task->stack_base >
        UINTPTR_MAX -
            (task->stack_size - 1u))
    {
        destroy_unpublished_task(
            task);

        return NULL;
    }

    uintptr_t stack_last =
        task->stack_base +
        task->stack_size -
        1u;

    /*
     * The user page directory must be able to access:
     *
     *     - task_t
     *     - task kernel stack
     *     - shared address_space_t metadata
     *
     * All are supervisor-only kernel objects.
     */
    if (!paging_share_kernel_pde(
            page_directory,
            (uintptr_t)task) ||
        !paging_share_kernel_pde(
            page_directory,
            task->stack_base) ||
        !paging_share_kernel_pde(
            page_directory,
            stack_last) ||
        !paging_share_kernel_pde(
            page_directory,
            (uintptr_t)address_space))
    {
        destroy_unpublished_task(
            task);

        return NULL;
    }

    /*
     * Task remains TASK_NEW.
     *
     * It owns one reference to address_space but is
     * not scheduler-visible yet.
     */
    return task;
}

void task_publish(
    task_t *task)
{
    if (task == NULL)
    {
        log_error(
            "TASK: publish received NULL task\n");

        return;
    }

    if (task->state != TASK_NEW)
    {
        log_error(
            "TASK: tid=%u is not TASK_NEW\n",
            (unsigned)task->id);

        return;
    }

    scheduler_make_ready(
        task);

    /*
     * IMPORTANT:
     *
     * Don't access task here.
     *
     * Another CPU is allowed to run/reap it after
     * scheduler_make_ready().
     */
}

task_t *task_create_user_with_policy(
    void (*entry)(void *),
    void *argument,
    address_space_t *address_space,
    sched_policy_t policy)
{
    task_t *task =
        task_create_user_unpublished(
            entry,
            argument,
            address_space,
            policy);

    if (task == NULL)
        return NULL;

    task_publish(
        task);

    /*
     * Caller still owns its original address_space reference.
     */
    return task;
}

task_t *task_create_kernel(
    void (*entry)(void *),
    void *argument)
{
    return task_create_kernel_with_policy(
        entry,
        argument,
        SCHED_POLICY_NORMAL);
}

/*
 * --------------------------------------------------------------------------
 * TASK SYSTEM INITIALIZATION
 * --------------------------------------------------------------------------
 */

void task_initialize(void)
{
    cpu_local_t *cpu = cpu_current();

    if (cpu == NULL)
        task_halt_forever();

    task_t *bootstrap = task_bootstrap_for_cpu(cpu->index);

    if (bootstrap == NULL)
        task_halt_forever();

    memset(bootstrap, 0, sizeof(*bootstrap));

    bootstrap->id = 0;
    bootstrap->state = TASK_RUNNING;
    bootstrap->policy = SCHED_POLICY_NORMAL;

    bootstrap->priority = 0;
    bootstrap->runtime_ticks = 0;

    bootstrap->stack_pointer = 0;
    bootstrap->stack_base = 0;
    bootstrap->stack_size = 0;

    bootstrap->sched_previous = NULL;
    bootstrap->sched_next = NULL;

    bootstrap->wait_previous = NULL;
    bootstrap->wait_next = NULL;
    bootstrap->waiting_on = NULL;

    bootstrap->sleep_previous = NULL;
    bootstrap->sleep_next = NULL;
    bootstrap->wake_tick = 0;

    bootstrap->cleanup_next = NULL;

    address_space_t *kernel_space =
        address_space_kernel();

    if (kernel_space == NULL)
        task_halt_forever();

    bootstrap->address_space =
        kernel_space;

    cpu->current_task = bootstrap;

    live_task_count = 1;

    /*
     * Initialize zombie-cleanup state before creating the reaper.
     */
    cleanup_head = NULL;
    cleanup_tail = NULL;

    cleanup_pending = 0;
    cleanup_total_reaped = 0;

    semaphore_initialize(
        &cleanup_semaphore,
        0);

    /*
     * --------------------------------------------------------------
     * IDLE TASK
     * --------------------------------------------------------------
     */

    task_t *idle = allocate_kernel_task(idle_thread, NULL, SCHED_POLICY_BACKGROUND);

    if (idle == NULL)
        task_halt_forever();

    /*
     * Idle is not inserted into a normal run queue.
     * It is a private fallback context for this CPU.
     */
    idle->state = TASK_READY;

    scheduler_set_idle_task(idle);

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

    reaper_task = allocate_kernel_task(task_reaper_thread, NULL, SCHED_POLICY_NORMAL);

    if (reaper_task == NULL)
        task_halt_forever();

    scheduler_make_ready(reaper_task);
}

bool task_initialize_cpu(void)
{
    cpu_local_t *cpu =
        cpu_current();

    if (cpu == NULL)
        return false;

    task_t *bootstrap =
        task_bootstrap_for_cpu(cpu->index);

    if (bootstrap == NULL)
        return false;

    memset(
        bootstrap,
        0,
        sizeof(*bootstrap));

    /*
     * Give AP bootstrap contexts unique IDs.
     *
     * BSP keeps ID 0.
     */
    uint32_t flags =
        spin_lock_irqsave(
            &task_id_lock);

    bootstrap->id =
        next_task_id++;

    spin_unlock_irqrestore(
        &task_id_lock,
        flags);

    bootstrap->state =
        TASK_RUNNING;

    bootstrap->policy =
        SCHED_POLICY_NORMAL;

    bootstrap->priority = 0;
    bootstrap->runtime_ticks = 0;

    bootstrap->stack_pointer = 0;
    bootstrap->stack_base = 0;
    bootstrap->stack_size = 0;

    bootstrap->sched_previous = NULL;
    bootstrap->sched_next = NULL;

    bootstrap->wait_previous = NULL;
    bootstrap->wait_next = NULL;
    bootstrap->waiting_on = NULL;

    bootstrap->sleep_previous = NULL;
    bootstrap->sleep_next = NULL;
    bootstrap->wake_tick = 0;

    bootstrap->cleanup_next = NULL;

    /*
     * The AP bootstrap task runs in the canonical
     * kernel address space, just like the BSP
     * bootstrap task.
     *
     * memset() above cleared page_directory, so it
     * must be restored before this CPU starts using
     * the scheduler.
     */
    address_space_t *kernel_space =
        address_space_kernel();

    if (kernel_space == NULL)
        return false;

    bootstrap->address_space =
        kernel_space;

    cpu->current_task =
        bootstrap;

    flags =
        spin_lock_irqsave(
            &task_id_lock);

    ++live_task_count;

    spin_unlock_irqrestore(
        &task_id_lock,
        flags);

    /*
     * Every CPU needs its own idle task.
     */
    task_t *idle =
        allocate_kernel_task(
            idle_thread,
            NULL,
            SCHED_POLICY_BACKGROUND);

    if (idle == NULL)
        return false;

    idle->state =
        TASK_READY;

    cpu->idle_task =
        idle;

    cpu->reschedule_pending =
        false;

    return true;
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

    if (task == NULL || task->entry == NULL)
        task_exit();

    task->entry(task->argument);

    /*
     * Returning from a kernel thread means exit.
     */
    task_exit();
}

static void task_first_entry(void)
{
    /*
     * Brand-new tasks do not return through the bottom
     * of scheduler_schedule().
     *
     * Complete the scheduler lock handoff here.
     */
    scheduler_finish_switch();

    task_bootstrap();
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

    if (task == NULL ||
        task_is_bootstrap_task(task) ||
        task_is_idle_task(task) ||
        task == reaper_task)
    {
        task_halt_forever();
    }

    /*
     * Disable local interrupts while transitioning this
     * CPU away from the dying task.
     *
     * Do NOT place the task into cleanup_queue here:
     * this CPU is still executing on its stack.
     */
    uint32_t flags =
        interrupt_save_disable();

    task->state =
        TASK_ZOMBIE;

    scheduler_schedule();

    /*
     * A zombie can never legitimately return here.
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
    uint32_t flags = spin_lock_irqsave(&cleanup_lock);

    size_t pending = cleanup_pending;

    spin_unlock_irqrestore(&cleanup_lock, flags);

    return pending;
}

uint64_t task_cleanup_total_reaped(void)
{
    /*
     * 64-bit read on i386 requires protection from concurrent update.
     */
    uint32_t flags = spin_lock_irqsave(&cleanup_lock);

    uint64_t total = cleanup_total_reaped;

    spin_unlock_irqrestore(&cleanup_lock, flags);

    return total;
}

size_t task_live_count(void)
{
    uint32_t flags = spin_lock_irqsave(&task_id_lock);
    size_t count = live_task_count;
    spin_unlock_irqrestore(&task_id_lock, flags);

    return count;
}

bool task_bind_process(
    task_t *task,
    process_t *process)
{
    if (task == NULL ||
        process == NULL)
    {
        return false;
    }

    /*
     * Binding is allowed only before scheduler publication.
     */
    if (task->state != TASK_NEW ||
        task->process != NULL)
    {
        return false;
    }

    address_space_t *process_space =
        process_address_space(process);

    if (process_space == NULL ||
        task->address_space == NULL ||
        process_space != task->address_space)
    {
        return false;
    }

    /*
     * Task owns an independent process reference.
     */
    if (!process_retain(process))
        return false;

    if (!process_thread_attach(process))
    {
        process_release(process);
        return false;
    }

    task->process = process;

    return true;
}
