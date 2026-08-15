#ifndef KERNEL_CPU_H
#define KERNEL_CPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/smp.h>

struct task;
typedef struct task task_t;

typedef struct cpu_local
{
    size_t              index;
    uint8_t             apic_id;
    bool                online;
    task_t              *current_task;
    task_t              *idle_task;
    task_t              *previous_task;
    bool                reschedule_pending;
    uint32_t            scheduler_switch_flags;
    bool                scheduler_switch_lock_held;
    volatile uint32_t   scheduler_ticks;
    volatile uint32_t   idle_ticks;
    uint32_t            scheduler_preempt_restore_flags;
} cpu_local_t;

void cpu_local_initialize(void);

cpu_local_t *cpu_current(void);
cpu_local_t *cpu_get(size_t index);
size_t cpu_current_index(void);

void cpu_account_scheduler_tick(bool idle);
uint32_t cpu_utilization_percent(size_t index);
uint32_t cpu_average_utilization_percent(void);

#endif
