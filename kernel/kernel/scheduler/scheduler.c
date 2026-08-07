#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/scheduler.h>
#include <kernel/task.h>

typedef struct
{
    const scheduler_algorithm_t* algorithm;
    void* algorithm_state;
} scheduler_policy_slot_t;

static scheduler_policy_slot_t policy_slots[SCHED_POLICY_COUNT];

static task_t* idle_task = NULL;
static bool reschedule_pending = false;
static bool scheduler_initialized = false;

/*
 * Internal functions provided by task.c.
 *
 * Do not expose these as normal public task APIs.
 */
extern void task_internal_set_current(task_t* task);

/*
 * Low level i386 context switch.
 */
extern void arch_context_switch(uintptr_t* old_stack_pointer, uintptr_t new_stack_pointer);

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
    SCHED_POLICY_BACKGROUND
};

static bool policy_valid(sched_policy_t policy)
{
    return policy >= 0 && policy < SCHED_POLICY_COUNT;
}

static scheduler_policy_slot_t* get_slot(sched_policy_t policy)
{
    if (!policy_valid(policy))
        return NULL;

    return &policy_slots[policy];
}

void scheduler_initialize(void)
{
    for (size_t i = 0; i < SCHED_POLICY_COUNT; ++i)
    {
        policy_slots[i].algorithm = NULL;
        policy_slots[i].algorithm_state = NULL;
    }

    idle_task = NULL;
    reschedule_pending = false;
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
    scheduler_bind_algorithm(SCHED_POLICY_REALTIME,    &scheduler_round_robin_algorithm);
    scheduler_bind_algorithm(SCHED_POLICY_INTERACTIVE, &scheduler_round_robin_algorithm);
    scheduler_bind_algorithm(SCHED_POLICY_NORMAL,      &scheduler_round_robin_algorithm);
    scheduler_bind_algorithm(SCHED_POLICY_BACKGROUND,  &scheduler_round_robin_algorithm);
}

bool scheduler_bind_algorithm(sched_policy_t policy, const scheduler_algorithm_t* algorithm)
{
    if (!scheduler_initialized)
        return false;
    
    if (!policy_valid(policy))
        return false;

    if (algorithm == NULL)
        return false;

    if (algorithm->create == NULL  ||
        algorithm->enqueue == NULL ||
        algorithm->dequeue == NULL ||
        algorithm->pick_next == NULL)
    {
        return false;
    }

    scheduler_policy_slot_t* slot = get_slot(policy);

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

    void* state = algorithm->create();

    if (state == NULL)
        return false;

    slot->algorithm = algorithm;
    slot->algorithm_state = state;

    return true;
}

void scheduler_make_ready(task_t* task)
{
    if (task == NULL)
        return;

    if (!policy_valid(task->policy))
        return ;
        
    scheduler_policy_slot_t* slot = get_slot(task->policy);

    if (slot == NULL || slot->algorithm == NULL)
        return;

    /*
     * Prevent accidental duplicate insertion.
     */
    if (task->state == TASK_READY)
        return;

    task->state = TASK_READY;
    task->sched_previous = NULL;
    task->sched_next = NULL;

    slot->algorithm->enqueue(slot->algorithm_state, task);
}

void scheduler_remove(task_t* task)
{
    if (task == NULL)
        return;

    if (task->state != TASK_READY)
        return;

    scheduler_policy_slot_t* slot = get_slot(task->policy);

    if (slot == NULL || slot->algorithm == NULL)
        return;

    slot->algorithm->dequeue(slot->algorithm_state, task);

    task->state = TASK_NEW;
}

static task_t* scheduler_pick_next(void)
{
    for (size_t i = 0; i < sizeof(policy_order) / sizeof(policy_order)[0]; ++i)
    {
        sched_policy_t policy = policy_order[i];

        scheduler_policy_slot_t* slot = get_slot(policy);

        if (slot == NULL || slot->algorithm == NULL)
            continue;

        task_t* task = slot->algorithm->pick_next(slot->algorithm_state);

        if (task != NULL)
            return task;
    }

    return idle_task;
}

static void scheduler_notify_switch_in(task_t* task)
{
    if (task == NULL)
        return;

    /*
     * Idle isn't in normal run queues.
     */
    if (task == idle_task)
        return;

    scheduler_policy_slot_t* slot = get_slot(task->policy);

    if (slot == NULL || slot->algorithm == NULL)
        return;

    if (slot->algorithm->on_switch_in != NULL)
        slot->algorithm->on_switch_in(slot->algorithm_state, task);
}

void scheduler_schedule(void)
{
    task_t* current = task_current();

    /*
     * If the currently executing task is still runnable,
     * return it to its scheduler queue.
     *
     * BLOCKED/SLEEPING/ZOMBIE tasks are not requeued.
     */
    if (current != NULL && current != idle_task && current->state == TASK_RUNNING)
    {
        scheduler_policy_slot_t* slot = get_slot(current->policy);

        if (slot != NULL && slot->algorithm != NULL)
        {
            current->state = TASK_READY;
            current->sched_previous = NULL;
            current->sched_next = NULL;

            slot->algorithm->enqueue(slot->algorithm_state, current);
        }
    }

    task_t* next = scheduler_pick_next();

    if (next == NULL)
    {
        /*
         * This should only happen before the idle task exists.
         */
        if (current != NULL && current->state == TASK_RUNNING)
            return;

        for (;;)
            __asm__ volatile ("hlt");
    }

    /*
     * We selected ourselves.
     *
     * No actual CPU context switch is required.
     */
    if (next == current)
    {
        next->state = TASK_RUNNING;

        scheduler_notify_switch_in(next);

        reschedule_pending = false;

        return;
    }

    next->state = TASK_RUNNING;

    scheduler_notify_switch_in(next);

    /*
     * Must update this BEFORE switching stacks.
     *
     * A newly-created task enters its bootstrap function and
     * immediately calls task_current().
     */
    task_internal_set_current(next);

    reschedule_pending = false;

    if (current == NULL)
    {
        /*
         * In our current design this should never happen because the
         * bootstrap kernel task is registered during task_initialize().
         */
        for (;;)
            __asm__ volatile ("hlt");
    }

    arch_context_switch(&current->stack_pointer, next->stack_pointer);
}

void scheduler_yield(void)
{
    reschedule_pending = false;

    scheduler_schedule();
}

void scheduler_block_current(task_state_t block_state)
{
    if (block_state != TASK_BLOCKED && block_state != TASK_SLEEPING)
        return;
    
    task_t* current = task_current();

    if (current == NULL)
        return;

    current->state = block_state;

    scheduler_schedule();
}

void scheduler_wake(task_t* task)
{
    if (task == NULL)
        return;

    if (task->state != TASK_BLOCKED && task->state != TASK_SLEEPING)
        return;

    scheduler_make_ready(task);

    /*
     * Later the active policy can decide whether this newly awakened
     * task should immediately preempt the current task.
     *
     * For now simply request a reschedule.
     */
    reschedule_pending = true;
}

bool scheduler_set_policy(task_t* task, sched_policy_t policy)
{
    if (task == NULL)
        return false;

    if (!policy_valid(policy))
        return false;

    if (task->policy == policy)
        return true;

    bool was_ready = task->state == TASK_READY;

    if (was_ready)
    {
        scheduler_policy_slot_t* old_slot = get_slot(task->policy);

        if (old_slot == NULL || old_slot->algorithm == NULL)
            return false;

        old_slot->algorithm->dequeue(old_slot->algorithm_state, task);

        task->state = TASK_NEW;
    }

    task->policy = policy;

    if (was_ready) scheduler_make_ready(task);

    /*
     * The new policy may outrank the running task.
     *
     * Don't force-switch here yet. Request a safe reschedule.
     */
    reschedule_pending = true;

    return true;
}

void scheduler_tick(void)
{
    task_t* current = task_current();

    if (current == NULL)
        return;

    if (current == idle_task)
        return;

    ++current->runtime_ticks;

    scheduler_policy_slot_t* slot = get_slot(current->policy);

    if (slot == NULL || slot->algorithm == NULL || slot->algorithm->tick == NULL)
        return;

    if (slot->algorithm->tick(slot->algorithm_state, current))
        reschedule_pending = true;
}

bool scheduler_preemption_pending(void)
{
    return reschedule_pending;
}

void scheduler_handle_safe_preemption_point(void)
{
    if (!reschedule_pending)
        return;

    scheduler_schedule();
}

void scheduler_set_idle_task(task_t* task)
{
    idle_task = task;
}