#include <stddef.h>
#include <stdint.h>

#include <kernel/cpu.h>
#include <kernel/smp.h>

static cpu_local_t cpu_locals[SMP_MAX_CPUS];

static uint8_t current_apic_id(void)
{
    uint32_t eax = 1;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    __asm__ volatile(
        "cpuid"
        : "+a"(eax),
          "=b"(ebx),
          "=c"(ecx),
          "=d"(edx));

    return (uint8_t)(ebx >> 24);
}

void cpu_local_initialize(void)
{
    size_t count = smp_cpu_count();

    for (size_t i = 0; i < count; ++i)
    {
        const smp_cpu_t *smp_cpu = smp_get_cpu(i);

        cpu_locals[i].index = i;
        cpu_locals[i].apic_id = smp_cpu->apic_id;
        cpu_locals[i].online = smp_cpu->online;
        cpu_locals[i].current_task = NULL;
        cpu_locals[i].idle_task = NULL;
        cpu_locals[i].previous_task = NULL;
        cpu_locals[i].reschedule_pending = false;
        cpu_locals[i].preemption_enabled = false;
        cpu_locals[i].scheduler_switch_flags = 0;
        cpu_locals[i].scheduler_switch_lock_held = false;
        cpu_locals[i].scheduler_ticks = 0;
        cpu_locals[i].idle_ticks = 0;
        cpu_locals[i].scheduler_preempt_restore_flags = 0;
    }
}

cpu_local_t *cpu_get(size_t index)
{
    if (index >= smp_cpu_count())
        return NULL;

    return &cpu_locals[index];
}

cpu_local_t *cpu_current(void)
{
    uint8_t apic_id = current_apic_id();
    size_t count = smp_cpu_count();

    for (size_t i = 0; i < count; ++i)
    {
        if (cpu_locals[i].apic_id == apic_id)
            return &cpu_locals[i];
    }

    return NULL;
}

size_t cpu_current_index(void)
{
    cpu_local_t *cpu = cpu_current();

    if (cpu == NULL)
        return SIZE_MAX;

    return cpu->index;
}

void cpu_account_scheduler_tick(bool idle)
{
    cpu_local_t *cpu = cpu_current();

    if (cpu == NULL)
        return;

    ++cpu->scheduler_ticks;

    if (idle)
        ++cpu->idle_ticks;
}

uint32_t cpu_utilization_percent(size_t index)
{
    cpu_local_t *cpu = cpu_get(index);

    if (cpu == NULL || cpu->scheduler_ticks == 0)
        return 0;

    uint32_t total = cpu->scheduler_ticks;
    uint32_t idle = cpu->idle_ticks;

    if (idle > total)
        idle = total;

    return ((total - idle) * 100u) / total;
}

uint32_t cpu_average_utilization_percent(void)
{
    size_t online = 0;
    uint32_t total = 0;

    for (size_t i = 0; i < smp_cpu_count(); ++i)
    {
        cpu_local_t *cpu = cpu_get(i);

        if (cpu == NULL || !cpu->online)
            continue;

        total += cpu_utilization_percent(i);
        ++online;
    }

    return online == 0 ? 0 : total / (uint32_t)online;
}
