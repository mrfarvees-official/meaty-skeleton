#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/cpu.h>
#include <kernel/smp.h>
#include <kernel/spinlock.h>
#include <kernel/paging.h>

#include "../arch/i386/gdt.h"

typedef struct
{
    const scheduler_algorithm_t *algorithm;
    void *algorithm_state;
} scheduler_policy_slot_t;

static scheduler_policy_slot_t policy_slots[SCHED_POLICY_COUNT];
static bool scheduler_initialized = false;
static spinlock_t scheduler_lock = SPINLOCK_INITIALIZER;

/*
 * Internal functions provided by task.c.
 *
 * Do not expose these as normal public task APIs.
 */
extern void task_internal_set_current(task_t *task);
extern void task_internal_finish_switch(task_t *previous);

/*
 * Low level i386 context switch.
 */
extern void arch_context_switch(uintptr_t *old_stack_pointer, uintptr_t new_stack_pointer);

/*
 * Highest scheduling requirement first.
 *
 * This ordering is part of the scheduler CORE,
 * not the RR algorithm.
 *
 * Later this can become configurable.
 */
static const sched_policy_t policy_order[] =
    {
        SCHED_POLICY_REALTIME,
        SCHED_POLICY_INTERACTIVE,
        SCHED_POLICY_NORMAL,
        SCHED_POLICY_BACKGROUND};

static bool policy_valid(sched_policy_t policy)
{
    return policy >= 0 && policy < SCHED_POLICY_COUNT;
}

static scheduler_policy_slot_t *get_slot(sched_policy_t policy)
{
    if (!policy_valid(policy))
        return NULL;

    return &policy_slots[policy];
}

static task_t *scheduler_idle_task(void)
{
    cpu_local_t *cpu = cpu_current();

    if (cpu == NULL)
        return NULL;

    return cpu->idle_task;
}

void scheduler_finish_switch(void)
{
    cpu_local_t *cpu =
        cpu_current();

    if (cpu == NULL)
        return;

    /*
     * Capture and clear previous_task BEFORE releasing
     * scheduler_lock.
     *
     * Once interrupts are restored, another scheduling
     * operation may occur on this CPU.
     */
    task_t *previous =
        cpu->previous_task;

    cpu->previous_task =
        NULL;

    if (cpu->scheduler_switch_lock_held)
    {
        uint32_t flags =
            cpu->scheduler_switch_flags;

        cpu->scheduler_switch_flags =
            0;

        cpu->scheduler_switch_lock_held =
            false;

        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);
    }

    /*
     * Scheduler lock is no longer held.
     *
     * It is now safe for cleanup processing to signal
     * semaphores/wake tasks.
     */
    task_internal_finish_switch(
        previous);
}
void scheduler_initialize(void)
{
    for (size_t i = 0; i < SCHED_POLICY_COUNT; ++i)
    {
        policy_slots[i].algorithm = NULL;
        policy_slots[i].algorithm_state = NULL;
    }

    scheduler_initialized = true;

    /*
     * For NOW all policy bands use RR.
     *
     * This does NOT mean they must forever use RR.
     *
     * Later:
     *
     * REALTIME:
     *     EDF / fixed priority / etc.
     *
     * INTERACTIVE:
     *     priority scheduler
     *
     * NORMAL:
     *     MLFQ / CFS-like / etc.
     *
     * BACKGROUND:
     *     RR
     */
    scheduler_bind_algorithm(SCHED_POLICY_REALTIME, &scheduler_round_robin_algorithm);
    scheduler_bind_algorithm(SCHED_POLICY_INTERACTIVE, &scheduler_round_robin_algorithm);
    scheduler_bind_algorithm(SCHED_POLICY_NORMAL, &scheduler_round_robin_algorithm);
    scheduler_bind_algorithm(SCHED_POLICY_BACKGROUND, &scheduler_round_robin_algorithm);
}

/*
 * BSP initialization only.
 *
 * Do not replace scheduler algorithms after AP scheduling
 * has started until live migration/locking is implemented.
 */
bool scheduler_bind_algorithm(sched_policy_t policy, const scheduler_algorithm_t *algorithm)
{
    if (!scheduler_initialized)
        return false;

    if (!policy_valid(policy))
        return false;

    if (algorithm == NULL)
        return false;

    if (algorithm->create == NULL ||
        algorithm->enqueue == NULL ||
        algorithm->dequeue == NULL ||
        algorithm->pick_next == NULL)
    {
        return false;
    }

    scheduler_policy_slot_t *slot = get_slot(policy);

    /*
     * For now only allow replacing an algorithm before tasks are
     * inserted into it.
     *
     * Live migration can be implemented later.
     */
    if (slot->algorithm != NULL)
    {
        if (slot->algorithm->destroy != NULL && slot->algorithm_state != NULL)
            slot->algorithm->destroy(slot->algorithm_state);

        slot->algorithm = NULL;
        slot->algorithm_state = NULL;
    }

    void *state = algorithm->create();

    if (state == NULL)
        return false;

    slot->algorithm = algorithm;
    slot->algorithm_state = state;

    return true;
}

void scheduler_make_ready(task_t *task)
{
    if (task == NULL)
        return;

    uint32_t flags =
        spin_lock_irqsave(
            &scheduler_lock);

    if (!policy_valid(task->policy))
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);
        return;
    }

    if (task->state == TASK_READY)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);
        return;
    }

    scheduler_policy_slot_t *slot =
        get_slot(task->policy);

    if (slot == NULL ||
        slot->algorithm == NULL)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);
        return;
    }

    task->state =
        TASK_READY;

    task->sched_previous = NULL;
    task->sched_next = NULL;

    slot->algorithm->enqueue(
        slot->algorithm_state,
        task);

    spin_unlock_irqrestore(
        &scheduler_lock,
        flags);

    smp_request_reschedule();
}

void scheduler_remove(task_t *task)
{
    if (task == NULL)
        return;

    uint32_t flags = spin_lock_irqsave(&scheduler_lock);
    if (task->state != TASK_READY)
    {
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }

    scheduler_policy_slot_t *slot = get_slot(task->policy);

    if (slot == NULL ||
        slot->algorithm == NULL)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);

        return;
    }

    slot->algorithm->dequeue(slot->algorithm_state, task);

    task->state = TASK_NEW;
    spin_unlock_irqrestore(
        &scheduler_lock,
        flags);
}

static task_t *scheduler_pick_next(void)
{
    for (size_t i = 0; i < sizeof(policy_order) / sizeof(policy_order)[0]; ++i)
    {
        sched_policy_t policy = policy_order[i];

        scheduler_policy_slot_t *slot = get_slot(policy);

        if (slot == NULL || slot->algorithm == NULL)
            continue;

        task_t *task = slot->algorithm->pick_next(slot->algorithm_state);

        if (task != NULL)
            return task;
    }

    return scheduler_idle_task();
}

static void scheduler_update_kernel_entry_stack(
    task_t *task)
{
    uintptr_t stack_top =
        task_kernel_stack_top(task);

    /*
     * Bootstrap contexts do not own a normal allocated task stack.
     *
     * They execute only in CPL0 at this stage, so the TSS privilege
     * transition stack is irrelevant while they are current.
     */
    if (stack_top == 0)
        return;

    if (!gdt_set_current_kernel_stack(
            stack_top))
    {
        /*
         * At this point CPU-local state must already exist.
         * Failure means an architectural scheduler invariant broke.
         */
        for (;;)
            __asm__ volatile("cli; hlt");
    }
}

static void scheduler_update_address_space(
    task_t *task)
{
    if (task == NULL ||
        task->page_directory == 0)
    {
        for (;;)
            __asm__ volatile("cli; hlt");
    }

    uintptr_t current_directory =
        paging_current_directory();

    if (current_directory ==
        task->page_directory)
    {
        return;
    }

    if (!paging_switch_directory(
            task->page_directory))
    {
        /*
         * A task with an unusable page directory is an unrecoverable
         * scheduler invariant violation at this stage.
         */
        for (;;)
            __asm__ volatile("cli; hlt");
    }
}

static void scheduler_notify_switch_in(task_t *task)
{
    if (task == NULL)
        return;

    /*
     * Idle isn't in normal run queues.
     */
    if (task == scheduler_idle_task())
        return;

    scheduler_policy_slot_t *slot = get_slot(task->policy);

    if (slot == NULL || slot->algorithm == NULL)
        return;

    if (slot->algorithm->on_switch_in != NULL)
        slot->algorithm->on_switch_in(slot->algorithm_state, task);
}

void scheduler_schedule(void)
{
    cpu_local_t *cpu =
        cpu_current();

    if (cpu == NULL)
        return;

    task_t *current =
        task_current();

    uint32_t flags =
        spin_lock_irqsave(
            &scheduler_lock);

    /*
     * If the current normal task is still runnable,
     * return it to the global run queue.
     *
     * Idle tasks are never placed into a normal
     * scheduler queue.
     */
    if (current != NULL &&
        current != cpu->idle_task &&
        current->state == TASK_RUNNING)
    {
        scheduler_policy_slot_t *slot =
            get_slot(current->policy);

        if (slot != NULL &&
            slot->algorithm != NULL)
        {
            current->state =
                TASK_READY;

            current->sched_previous = NULL;
            current->sched_next = NULL;

            slot->algorithm->enqueue(
                slot->algorithm_state,
                current);
        }
    }

    /*
     * scheduler_pick_next() must only manipulate
     * scheduler queues while scheduler_lock is held.
     */
    task_t *next =
        scheduler_pick_next();

    /*
     * No normal runnable task.
     *
     * Use this CPU's private idle task.
     */
    if (next == NULL)
        next = cpu->idle_task;

    if (next == NULL)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);

        for (;;)
            __asm__ volatile("cli; hlt");
    }

    /*
     * This can occur if the current task was placed
     * back into the run queue and immediately chosen
     * again.
     */
    if (next == current)
    {
        next->state =
            TASK_RUNNING;

        scheduler_notify_switch_in(
            next);

        /*
         * Keep this CPU's TSS synchronized with the task the
         * scheduler says is running.
         */
        scheduler_update_kernel_entry_stack(
            next);

        scheduler_update_address_space(
            next);

        cpu->reschedule_pending =
            false;

        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);

        return;
    }

    /*
     * next has been removed from its run queue.
     *
     * Marking it RUNNING while scheduler_lock remains
     * held prevents another CPU from selecting the
     * same task.
     */
    next->state =
        TASK_RUNNING;

    scheduler_notify_switch_in(
        next);

    /*
     * Every active CPU should already have a bootstrap
     * or normal current task before entering scheduling.
     */
    if (current == NULL)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);

        for (;;)
            __asm__ volatile("cli; hlt");
    }

    /*
     * Remember the context we're physically leaving.
     *
     * task_internal_finish_switch() runs only after
     * execution is safely on the new stack.
     */
    cpu->previous_task =
        current;

    /*
     * If scheduler_switch_flags was supplied by the interrupt
     * dispatcher, preserve the interrupted context's EFLAGS.
     *
     * Otherwise this is normal task-context scheduling and the
     * flags captured while taking scheduler_lock are correct.
     */
    if (!cpu->scheduler_switch_lock_held &&
        cpu->scheduler_switch_flags != 0)
    {
        uint32_t restore_flags =
            cpu->scheduler_switch_flags;

        cpu->scheduler_switch_flags =
            restore_flags;
    }
    else
    {
        if (cpu->scheduler_preempt_restore_flags != 0)
        {
            cpu->scheduler_switch_flags =
                cpu->scheduler_preempt_restore_flags;

            cpu->scheduler_preempt_restore_flags =
                0;
        }
        else
        {
            cpu->scheduler_switch_flags =
                flags;
        }
    }

    cpu->scheduler_switch_lock_held =
        true;

    task_internal_set_current(
        next);

    /*
     * Install architectural state belonging to the incoming task before
     * physically switching to its saved kernel stack.
     */
    scheduler_update_kernel_entry_stack(
        next);

    scheduler_update_address_space(
        next);

    cpu->reschedule_pending =
        false;

    arch_context_switch(
        &current->stack_pointer,
        next->stack_pointer);

    /*
     * Existing tasks resume here after some future
     * context switch selects them again.
     */
    scheduler_finish_switch();
}

void scheduler_yield(void)
{
    cpu_local_t *cpu = cpu_current();

    if (cpu == NULL)
        return;

    cpu->reschedule_pending = false;

    scheduler_schedule();
}

void scheduler_block_current(task_state_t block_state)
{
    if (block_state != TASK_BLOCKED &&
        block_state != TASK_SLEEPING)
    {
        return;
    }

    task_t *current =
        task_current();

    if (current == NULL)
        return;

    uint32_t flags =
        spin_lock_irqsave(
            &scheduler_lock);

    if (current->state != TASK_RUNNING)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);

        return;
    }

    current->state =
        block_state;

    spin_unlock_irqrestore(
        &scheduler_lock,
        flags);

    scheduler_schedule();
}

void scheduler_block_current_wait(
    task_state_t block_state,
    spinlock_t *wait_lock,
    uint32_t wait_flags)
{
    if (wait_lock == NULL)
        return;

    /*
     * Only blocking states are valid here.
     */
    if (block_state != TASK_BLOCKED &&
        block_state != TASK_SLEEPING)
    {
        spin_unlock_irqrestore(
            wait_lock,
            wait_flags);

        return;
    }

    cpu_local_t *cpu =
        cpu_current();

    task_t *current =
        task_current();

    if (cpu == NULL ||
        current == NULL)
    {
        spin_unlock_irqrestore(
            wait_lock,
            wait_flags);

        return;
    }

    /*
     * wait_lock is already held and interrupts are
     * disabled at this point.
     *
     * Acquire scheduler_lock BEFORE releasing wait_lock.
     *
     * Lock order:
     *
     *     wait/sleep lock -> scheduler_lock
     *
     * Wake paths release the wait/sleep lock before
     * calling scheduler_wake(), so this does not create
     * the reverse lock order.
     */
    uint32_t scheduler_flags =
        spin_lock_irqsave(
            &scheduler_lock);

    if (current->state != TASK_RUNNING)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            scheduler_flags);

        spin_unlock_irqrestore(
            wait_lock,
            wait_flags);

        return;
    }

    /*
     * The task is already present in the wait/sleep
     * structure while wait_lock is held.
     *
     * Publish the blocked state while scheduler_lock
     * is also held.
     */
    current->state =
        block_state;

    /*
     * Release the external wait/sleep lock WITHOUT
     * restoring interrupts.
     *
     * Interrupts must remain disabled until the context
     * switch handoff has completed.
     */
    spin_unlock(
        wait_lock);

    /*
     * From this point another CPU may remove current
     * from the wait queue, but scheduler_wake() cannot
     * make it READY until we release scheduler_lock.
     *
     * scheduler_lock remains held across the physical
     * context switch.
     */
    task_t *next =
        scheduler_pick_next();

    if (next == NULL)
        next = cpu->idle_task;

    if (next == NULL)
    {
        /*
         * There is no valid context to switch to.
         *
         * Keep interrupts disabled and stop this CPU.
         */
        spin_unlock_irqrestore(
            &scheduler_lock,
            scheduler_flags);

        for (;;)
            __asm__ volatile("cli; hlt");
    }

    /*
     * A BLOCKED/SLEEPING current task must never be
     * selected as next.
     */
    if (next == current)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            scheduler_flags);

        for (;;)
            __asm__ volatile("cli; hlt");
    }

    next->state =
        TASK_RUNNING;

    scheduler_notify_switch_in(
        next);

    /*
     * Save the task whose stack we are leaving.
     */
    cpu->previous_task =
        current;

    /*
     * Transfer scheduler_lock ownership across the
     * context switch.
     *
     * IMPORTANT:
     *
     * Use wait_flags here, NOT scheduler_flags.
     *
     * scheduler_lock was acquired while interrupts were
     * already disabled by wait_lock, so scheduler_flags
     * contains IF=0.
     *
     * wait_flags contains the interrupt state that
     * existed before the original wait/sleep operation.
     */
    cpu->scheduler_switch_flags =
        wait_flags;

    cpu->scheduler_switch_lock_held =
        true;

    task_internal_set_current(
        next);

    cpu->reschedule_pending =
        false;

    /*
     * scheduler_lock is intentionally still held.
     *
     * Therefore another CPU cannot wake current and
     * execute it before this CPU physically leaves
     * current's stack.
     */
    arch_context_switch(
        &current->stack_pointer,
        next->stack_pointer);

    /*
     * Existing tasks resume here when scheduled again.
     *
     * Brand-new tasks instead call scheduler_finish_switch()
     * from task_first_entry().
     */
    scheduler_finish_switch();
}

void scheduler_wake(task_t *task)
{
    if (task == NULL)
        return;

    uint32_t flags =
        spin_lock_irqsave(
            &scheduler_lock);

    if (task->state != TASK_BLOCKED &&
        task->state != TASK_SLEEPING)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);

        return;
    }

    scheduler_policy_slot_t *slot =
        get_slot(task->policy);

    if (slot == NULL ||
        slot->algorithm == NULL)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);

        return;
    }

    task->state =
        TASK_READY;

    task->sched_previous = NULL;
    task->sched_next = NULL;

    slot->algorithm->enqueue(
        slot->algorithm_state,
        task);

    spin_unlock_irqrestore(
        &scheduler_lock,
        flags);

    cpu_local_t *cpu =
        cpu_current();

    if (cpu != NULL)
        cpu->reschedule_pending = true;

    smp_request_reschedule();
}

bool scheduler_set_policy(
    task_t *task,
    sched_policy_t policy)
{
    if (task == NULL)
        return false;

    if (!policy_valid(policy))
        return false;

    uint32_t flags =
        spin_lock_irqsave(
            &scheduler_lock);

    /*
     * No change required.
     */
    if (task->policy == policy)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);

        return true;
    }

    /*
     * Validate the destination scheduler slot
     * before modifying the task.
     */
    scheduler_policy_slot_t *new_slot =
        get_slot(policy);

    if (new_slot == NULL ||
        new_slot->algorithm == NULL)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);

        return false;
    }

    bool was_ready =
        task->state == TASK_READY;

    /*
     * If the task is currently READY, it is present
     * in the old policy's run queue.
     *
     * Remove it before changing task->policy.
     */
    if (was_ready)
    {
        scheduler_policy_slot_t *old_slot =
            get_slot(task->policy);

        if (old_slot == NULL ||
            old_slot->algorithm == NULL)
        {
            spin_unlock_irqrestore(
                &scheduler_lock,
                flags);

            return false;
        }

        old_slot->algorithm->dequeue(
            old_slot->algorithm_state,
            task);

        /*
         * Temporarily make it non-runnable while
         * moving between scheduler queues.
         */
        task->state =
            TASK_NEW;

        task->sched_previous = NULL;
        task->sched_next = NULL;
    }

    /*
     * Now change the policy.
     */
    task->policy =
        policy;

    /*
     * If the task was runnable before the change,
     * insert it into the new policy's queue.
     */
    if (was_ready)
    {
        task->state =
            TASK_READY;

        task->sched_previous = NULL;
        task->sched_next = NULL;

        new_slot->algorithm->enqueue(
            new_slot->algorithm_state,
            task);
    }

    spin_unlock_irqrestore(
        &scheduler_lock,
        flags);

    /*
     * The policy change may affect scheduling order
     * on the CPU making this request.
     */
    cpu_local_t *cpu =
        cpu_current();

    if (cpu != NULL)
        cpu->reschedule_pending = true;

    return true;
}

void scheduler_tick(void)
{
    task_t *current = task_current();

    if (current == NULL)
        return;

    cpu_local_t *cpu = cpu_current();

    cpu_account_scheduler_tick(
        cpu != NULL && current == cpu->idle_task);

    if (cpu != NULL && current == cpu->idle_task)
        return;

    uint32_t flags =
        spin_lock_irqsave(
            &scheduler_lock);

    if (current->state != TASK_RUNNING)
    {
        spin_unlock_irqrestore(
            &scheduler_lock,
            flags);

        return;
    }

    ++current->runtime_ticks;

    scheduler_policy_slot_t *slot =
        get_slot(current->policy);

    bool request_reschedule = false;

    if (slot != NULL &&
        slot->algorithm != NULL &&
        slot->algorithm->tick != NULL)
    {
        request_reschedule =
            slot->algorithm->tick(
                slot->algorithm_state,
                current);
    }

    spin_unlock_irqrestore(
        &scheduler_lock,
        flags);

    if (request_reschedule)
    {
        cpu_local_t *cpu = cpu_current();

        if (cpu != NULL)
            cpu->reschedule_pending = true;
    }
}

bool scheduler_preemption_pending(void)
{
    cpu_local_t *cpu =
        cpu_current();

    if (cpu == NULL)
        return false;

    return cpu->reschedule_pending;
}

void scheduler_enable_preemption(void)
{
    cpu_local_t *cpu =
        cpu_current();

    if (cpu == NULL)
        return;

    cpu->preemption_enabled =
        true;
}

void scheduler_disable_preemption(void)
{
    cpu_local_t *cpu =
        cpu_current();

    if (cpu == NULL)
        return;

    cpu->preemption_enabled =
        false;
}

bool scheduler_preemption_enabled(void)
{
    cpu_local_t *cpu =
        cpu_current();

    if (cpu == NULL)
        return false;

    return cpu->preemption_enabled;
}

void scheduler_handle_safe_preemption_point(
    uint32_t restore_flags)
{
    cpu_local_t *cpu =
        cpu_current();

    if (cpu == NULL)
        return;

    if (!cpu->preemption_enabled)
        return;

    if (!cpu->reschedule_pending)
        return;

    cpu->scheduler_preempt_restore_flags =
        restore_flags;

    scheduler_schedule();
}

void scheduler_set_idle_task(task_t *task)
{
    cpu_local_t *cpu = cpu_current();

    if (cpu == NULL)
        return;

    cpu->idle_task = task;
}
