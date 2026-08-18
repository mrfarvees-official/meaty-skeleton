#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>
#include <kernel/spinlock.h>

#include <kernel/task.h>
#include <kernel/algorithm.h>

void scheduler_initialize(void);
void scheduler_finish_switch(void);

/*
 * Bind an algorithm to a policy.
 *
 * Example:
 *
 * NORMAL      -> round robin
 * INTERACTIVE -> priority
 * REALTIME    -> EDF
 *
 * Later.
 */
bool scheduler_bind_algorithm(sched_policy_t policy, const scheduler_algorithm_t* algorithm);

/*
 * Runnable task management.
 */
void scheduler_make_ready(task_t* task);
void scheduler_remove(task_t* task);

/*
 * Scheduling operations.
 */
void scheduler_schedule(void);
void scheduler_yield(void);

/*
 * Blocking / waking.
 *
 * These become the foundation for disk/network/audio/mutex/etc.
 */
void scheduler_block_current(task_state_t blocked_state);
void scheduler_block_current_wait(task_state_t block_state, spinlock_t *wait_lock, uint32_t wait_flags);
void scheduler_wake(task_t* task);

/*
 * Change scheduling requirements.
 */
bool scheduler_set_policy(task_t* task, sched_policy_t policy);

/*
 * Timer hook.
 *
 * IMPORTANT:
 *
 * At this stage this records the request for preemption.
 * It does NOT context-switch directly from the PIT interrupt.
 */
void scheduler_tick(void);

bool scheduler_preemption_pending(void);

void scheduler_enable_preemption(void);
void scheduler_disable_preemption(void);
bool scheduler_preemption_enabled(void);

void scheduler_handle_safe_preemption_point(
    uint32_t restore_flags);

/*
 * Idle task.
 */
void scheduler_set_idle_task(task_t* task);

/*
 * Provided by round_robin.c
 */
extern const scheduler_algorithm_t scheduler_round_robin_algorithm;

#endif
