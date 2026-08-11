#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/cpu.h>
#include <kernel/smp.h>
#include <kernel/spinlock.h>
#include <kernel/system_info.h>
#include <kernel/task.h>
#include <kernel/test.h>
#include <kernel/timer.h>

#define SMP_STRESS_TASKS       1024u
#define SMP_STRESS_YIELDS      1024u
#define SMP_STRESS_BARRIER_YIELDS 4096u

static spinlock_t stress_lock = SPINLOCK_INITIALIZER;
static size_t workers_finished;
static size_t worker_yields_completed;
static size_t workers_per_cpu[SMP_MAX_CPUS];
static bool cpu_observed[SMP_MAX_CPUS];
static size_t observed_cpus;
static size_t expected_cpus;
static bool stress_failed;

static void stress_worker(void *argument)
{
    (void)argument;

    /*
     * Do not let the BSP finish this batch before an AP receives a timer
     * interrupt and takes work from the shared run queue.  This turns the
     * test into an actual AP scheduling test rather than a race against the
     * BSP's first time slice.
     */
    cpu_local_t *cpu = cpu_current();

    uint32_t flags = spin_lock_irqsave(&stress_lock);

    if (cpu == NULL || cpu->index >= SMP_MAX_CPUS)
    {
        stress_failed = true;
    }
    else if (!cpu_observed[cpu->index])
    {
        cpu_observed[cpu->index] = true;
        ++observed_cpus;
    }

    spin_unlock_irqrestore(&stress_lock, flags);

    for (size_t attempt = 0; attempt < SMP_STRESS_BARRIER_YIELDS; ++attempt)
    {
        flags = spin_lock_irqsave(&stress_lock);
        bool released = observed_cpus >= expected_cpus;
        spin_unlock_irqrestore(&stress_lock, flags);

        if (released)
            break;

        task_yield();

        if (attempt + 1u == SMP_STRESS_BARRIER_YIELDS)
        {
            flags = spin_lock_irqsave(&stress_lock);
            stress_failed = true;
            spin_unlock_irqrestore(&stress_lock, flags);
        }
    }

    for (size_t i = 0; i < SMP_STRESS_YIELDS; ++i)
    {
        task_yield();

        if ((i & 15u) == 15u)
        {
            flags = spin_lock_irqsave(&stress_lock);
            worker_yields_completed += 16u;
            spin_unlock_irqrestore(&stress_lock, flags);
        }
    }

    cpu = cpu_current();
    flags = spin_lock_irqsave(&stress_lock);

    if (cpu == NULL || cpu->index >= SMP_MAX_CPUS)
    {
        stress_failed = true;
    }
    else
    {
        ++workers_per_cpu[cpu->index];
    }

    ++workers_finished;

    spin_unlock_irqrestore(&stress_lock, flags);
}

void smp_multitasking_stress_test(void)
{
    size_t online = smp_online_cpu_count();

    printf("\n[SMP STRESS] starting: online CPUs=%u tasks=%u yields/task=%u\n",
           (unsigned)online,
           (unsigned)SMP_STRESS_TASKS,
           (unsigned)SMP_STRESS_YIELDS);

    if (online == 0)
    {
        printf("[SMP STRESS] FAIL: no online CPU\n");
        return;
    }

    uint32_t flags = spin_lock_irqsave(&stress_lock);
    workers_finished = 0;
    worker_yields_completed = 0;
    stress_failed = false;
    observed_cpus = 0;
    expected_cpus = online;

    for (size_t i = 0; i < SMP_MAX_CPUS; ++i)
    {
        workers_per_cpu[i] = 0;
        cpu_observed[i] = false;
    }

    spin_unlock_irqrestore(&stress_lock, flags);

    for (size_t i = 0; i < SMP_STRESS_TASKS; ++i)
    {
        if (task_create_kernel(stress_worker, NULL) == NULL)
        {
            printf("[SMP STRESS] FAIL: task creation at %u\n", (unsigned)i);
            return;
        }
    }

    size_t total_yields = SMP_STRESS_TASKS * SMP_STRESS_YIELDS;
    size_t report_interval = total_yields / 4u;
    size_t next_report = report_interval;
    uint64_t last_live_report_tick = timer_ticks();
    uint32_t report_frequency = timer_frequency();

    if (report_frequency == 0)
        report_frequency = 1;

    for (;;)
    {
        flags = spin_lock_irqsave(&stress_lock);
        bool complete = workers_finished == SMP_STRESS_TASKS;
        size_t completed_yields = worker_yields_completed;
        spin_unlock_irqrestore(&stress_lock, flags);

        if (complete)
            break;

        uint64_t now = timer_ticks();
        bool milestone_reached = completed_yields >= next_report;
        bool time_to_report = now - last_live_report_tick >= report_frequency;

        if (milestone_reached || time_to_report)
        {
            printf("[SMP STRESS] progress=%u%% ",
                   (unsigned)((completed_yields * 100u) / total_yields));
            system_info_print_live();

            if (milestone_reached)
                next_report += report_interval;

            last_live_report_tick = now;
        }

        task_yield();
    }

    flags = spin_lock_irqsave(&stress_lock);
    size_t participating_cpus = observed_cpus;

    for (size_t i = 0; i < smp_cpu_count(); ++i)
    {
        const smp_cpu_t *cpu = smp_get_cpu(i);

        printf("[SMP STRESS] CPU %u observed=%s completed=%u resched_ipi=%u\n",
               (unsigned)i,
               cpu_observed[i] ? "yes" : "no",
               (unsigned)workers_per_cpu[i],
               cpu == NULL ? 0u : (unsigned)cpu->reschedule_ipi_count);
    }

    bool passed = !stress_failed &&
                  workers_finished == SMP_STRESS_TASKS &&
                  (online == 1 || participating_cpus >= 2);

    spin_unlock_irqrestore(&stress_lock, flags);

    printf("[SMP STRESS] %s\n", passed ? "PASS" : "FAIL");
}
