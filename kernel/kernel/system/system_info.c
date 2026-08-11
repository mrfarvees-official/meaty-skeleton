#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/cpu.h>
#include <kernel/process.h>
#include <kernel/smp.h>
#include <kernel/system_info.h>
#include <kernel/task.h>
#include <kernel/timer.h>

static void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
}

static uint32_t cache_size_kib(uint32_t ebx, uint32_t ecx)
{
    uint64_t line_size = (uint64_t)(ebx & 0xFFFu) + 1u;
    uint64_t partitions = (uint64_t)((ebx >> 12) & 0x3FFu) + 1u;
    uint64_t ways = (uint64_t)((ebx >> 22) & 0x3FFu) + 1u;
    uint64_t sets = (uint64_t)ecx + 1u;

    return (uint32_t)((line_size * partitions * ways * sets) / 1024u);
}

void system_info_print(void)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_basic = eax;

    cpuid(0x80000000u, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_extended = eax;

    char brand[49] = "Unknown CPU";

    if (max_extended >= 0x80000004u)
    {
        uint32_t *brand_words = (uint32_t *)brand;

        for (uint32_t leaf = 0; leaf < 3u; ++leaf)
        {
            cpuid(0x80000002u + leaf, 0, &eax, &ebx, &ecx, &edx);
            brand_words[leaf * 4u + 0u] = eax;
            brand_words[leaf * 4u + 1u] = ebx;
            brand_words[leaf * 4u + 2u] = ecx;
            brand_words[leaf * 4u + 3u] = edx;
        }

        brand[48] = '\0';
    }

    size_t logical_cpus = smp_online_cpu_count();
    uint32_t threads_per_core = 1;
    uint32_t logical_per_package = logical_cpus == 0 ? 1u : (uint32_t)logical_cpus;

    if (max_basic >= 0x0Bu)
    {
        for (uint32_t subleaf = 0; subleaf < 8u; ++subleaf)
        {
            cpuid(0x0Bu, subleaf, &eax, &ebx, &ecx, &edx);
            uint32_t level_type = (ecx >> 8) & 0xFFu;

            if (level_type == 0 || ebx == 0)
                break;

            if (level_type == 1)
                threads_per_core = ebx & 0xFFFFu;
            else if (level_type == 2)
                logical_per_package = ebx & 0xFFFFu;
        }
    }

    if (threads_per_core == 0)
        threads_per_core = 1;
    if (logical_per_package == 0)
        logical_per_package = 1;

    uint32_t cores_per_package = logical_per_package / threads_per_core;
    if (cores_per_package == 0)
        cores_per_package = 1;

    uint32_t sockets = ((uint32_t)logical_cpus + logical_per_package - 1u) /
                       logical_per_package;

    uint32_t l1_data_kib = 0;
    uint32_t l1_instruction_kib = 0;
    uint32_t l2_kib = 0;
    uint32_t l3_kib = 0;

    if (max_basic >= 4u)
    {
        for (uint32_t subleaf = 0; subleaf < 16u; ++subleaf)
        {
            cpuid(4, subleaf, &eax, &ebx, &ecx, &edx);
            uint32_t cache_type = eax & 0x1Fu;
            uint32_t level = (eax >> 5) & 0x7u;

            if (cache_type == 0)
                break;

            uint32_t size_kib = cache_size_kib(ebx, ecx);

            if (level == 1u && cache_type == 1u)
                l1_data_kib += size_kib;
            else if (level == 1u && cache_type == 2u)
                l1_instruction_kib += size_kib;
            else if (level == 2u)
                l2_kib += size_kib;
            else if (level == 3u)
                l3_kib += size_kib;
        }
    }

    uint64_t uptime_ms = timer_uptime_ms();
    uint64_t uptime_seconds = uptime_ms / 1000u;

    printf("\n[SYSTEM INFO]\n");
    printf("CPU: %s\n", brand);
    printf("Sockets: %u  Cores/package: %u  Threads/core: %u  Online logical CPUs: %u\n",
           (unsigned)sockets, (unsigned)cores_per_package,
           (unsigned)threads_per_core, (unsigned)logical_cpus);

    if (max_basic >= 0x16u)
    {
        cpuid(0x16u, 0, &eax, &ebx, &ecx, &edx);
        printf("Frequency: base=%u MHz  max/boost=%u MHz\n",
               (unsigned)eax, (unsigned)ebx);
    }
    else
    {
        printf("Frequency: unavailable (CPUID leaf 0x16 unsupported)\n");
    }

    printf("Cache: L1D=%u KiB L1I=%u KiB L2=%u KiB L3=%u KiB\n",
           (unsigned)l1_data_kib, (unsigned)l1_instruction_kib,
           (unsigned)l2_kib, (unsigned)l3_kib);
    printf("Uptime: %llu:%02llu:%02llu\n",
           (unsigned long long)(uptime_seconds / 3600u),
           (unsigned long long)((uptime_seconds / 60u) % 60u),
           (unsigned long long)(uptime_seconds % 60u));

    for (size_t i = 0; i < smp_cpu_count(); ++i)
    {
        const smp_cpu_t *cpu = smp_get_cpu(i);

        if (cpu != NULL && cpu->online)
            printf("CPU %u utilization: %u%%\n",
                   (unsigned)i, (unsigned)cpu_utilization_percent(i));
    }

    printf("Average CPU utilization: %u%%\n",
           (unsigned)cpu_average_utilization_percent());
    printf("Kernel threads/tasks: %u  Processes: %u  Handles: %u\n",
           (unsigned)task_live_count(),
           (unsigned)process_live_count(),
           (unsigned)process_handle_live_count());
}

void system_info_print_live(void)
{
    uint64_t uptime_seconds = timer_uptime_ms() / 1000u;

    printf("[LIVE] uptime=%llu:%02llu:%02llu threads=%u processes=%u handles=%u avg_cpu=%u%%",
           (unsigned long long)(uptime_seconds / 3600u),
           (unsigned long long)((uptime_seconds / 60u) % 60u),
           (unsigned long long)(uptime_seconds % 60u),
           (unsigned)task_live_count(),
           (unsigned)process_live_count(),
           (unsigned)process_handle_live_count(),
           (unsigned)cpu_average_utilization_percent());

    for (size_t i = 0; i < smp_cpu_count(); ++i)
    {
        const smp_cpu_t *cpu = smp_get_cpu(i);

        if (cpu != NULL && cpu->online)
            printf(" cpu%u=%u%%", (unsigned)i,
                   (unsigned)cpu_utilization_percent(i));
    }

    printf("\n");
}
