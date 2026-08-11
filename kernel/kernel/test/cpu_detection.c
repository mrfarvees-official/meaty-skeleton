#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/test.h>
#include <kernel/acpi.h>
#include <kernel/smp.h>


void cpu_detection_test(void)
{
    printf(
        "\n[SMP TEST] CPU detection starting\n"
    );


    /*
     * ACPI should already have been initialized before this test, but
     * verify the state.
     */
    const acpi_rsdp_t *rsdp =
        acpi_get_rsdp();


    if (rsdp == NULL)
    {
        printf(
            "[SMP TEST] FAIL: RSDP unavailable\n"
        );

        return;
    }


    printf(
        "[SMP TEST] RSDP found at 0x%x\n",
        (unsigned)(uintptr_t)rsdp
    );


    printf(
        "[SMP TEST] RSDT at 0x%x\n",
        (unsigned)rsdp->rsdt_address
    );


    printf(
        "[SMP TEST] LAPIC physical address: 0x%x\n",
        (unsigned)smp_lapic_address()
    );


    size_t count =
        smp_cpu_count();


    printf(
        "[SMP TEST] enabled CPUs detected: %u\n",
        (unsigned)count
    );


    if (count == 0)
    {
        printf(
            "[SMP TEST] FAIL: no CPUs detected\n"
        );

        return;
    }


    size_t bsp_count = 0;
    size_t online_count = 0;


    for (size_t i = 0;
         i < count;
         ++i)
    {
        const smp_cpu_t *cpu =
            smp_get_cpu(i);


        if (cpu == NULL)
        {
            printf(
                "[SMP TEST] FAIL: CPU %u missing\n",
                (unsigned)i
            );

            return;
        }


        printf(
            "[SMP TEST] CPU %u: "
            "ACPI_ID=%u "
            "APIC_ID=%u "
            "BSP=%s "
            "ONLINE=%s "
            "RESCHED_IPI=%u\n",

            (unsigned)cpu->index,

            (unsigned)cpu->processor_id,

            (unsigned)cpu->apic_id,

            cpu->bootstrap_processor
                ? "yes"
                : "no",

            cpu->online
                ? "yes"
                : "no",

            (unsigned)cpu->reschedule_ipi_count
        );


        if (cpu->bootstrap_processor)
            ++bsp_count;


        if (cpu->online)
            ++online_count;
    }


    printf(
        "[SMP TEST] BSP count: %u\n",
        (unsigned)bsp_count
    );


    printf(
        "[SMP TEST] online CPUs: %u\n",
        (unsigned)online_count
    );


    if (bsp_count != 1u)
    {
        printf(
            "[SMP TEST] FAIL: expected exactly one BSP\n"
        );

        return;
    }


    if (online_count != count)
    {
        printf(
            "[SMP TEST] FAIL: not every detected CPU is online\n"
        );

        return;
    }


    printf(
        "[SMP TEST] PASS\n"
    );
}
