#ifndef KERNEL_SMP_H
#define KERNEL_SMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Keep this small for our first SMP stage.
 *
 * Later we can make CPU storage dynamic if needed.
 */
#define SMP_MAX_CPUS 64u

typedef struct smp_cpu
{
    /*
     * Sequential kernel CPU number:
     *
     *     0
     *     1
     *     2
     *     ...
     */
    size_t              index;

    /*
     * ACPI Processor UID from MADT Local APIC entry.
     */
    uint32_t            processor_id;

    /*
     * Local APIC ID used to address this logical processor.
     */
    uint32_t            apic_id;

    /*
     * Firmware says processor may be used.
     */
    bool                enabled;

    /*
     * Current processor executing the kernel.
     *
     * At this stage only the BSP is online.
     */
    bool                bootstrap_processor;

    /*
     * False for every AP in this stage.
     *
     * We are detecting CPUs, not starting them yet.
     */
    bool                online;

    /* Number of scheduler wakeup IPIs handled by this CPU. */
    volatile uint32_t   reschedule_ipi_count;
} smp_cpu_t;

/*
 * Discover CPUs from ACPI MADT.
 */
bool smp_detect_cpus(void);

/*
 * Start every enabled application processor described by the MADT.
 * This must be called by the BSP after paging, ACPI and CPU-local state
 * are ready, but before normal kernel work is allowed to begin.
 */
bool smp_start_aps(void);

/*
 * Number of enabled processors discovered.
 */
size_t smp_cpu_count(void);

/*
 * Return CPU information by sequential kernel index.
 */
const smp_cpu_t* smp_get_cpu(size_t index);

/*
 * Local APIC MMIO physical address advertised by MADT.
 */
uintptr_t smp_lapic_address(void);

/* Number of processors that have completed kernel-side initialization. */
size_t smp_online_cpu_count(void);

/* Notify idle peer CPUs that shared scheduler work is available. */
void smp_request_reschedule(void);

#endif
