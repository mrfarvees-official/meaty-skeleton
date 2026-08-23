#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/acpi.h>
#include <kernel/cpu.h>
#include <kernel/heap.h>
#include <kernel/paging.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <kernel/task.h>

#include "../../arch/i386/gdt.h"
#include "../../arch/i386/idt.h"
#include "../../arch/i386/interrupts.h"

#define MADT_ENTRY_PROCESSOR_LOCAL_APIC 0u

#define MADT_LOCAL_APIC_ENABLED (1u << 0)

#define LAPIC_ID_REGISTER 0x020u
#define LAPIC_EOI_REGISTER 0x0B0u
#define LAPIC_SVR_REGISTER 0x0F0u
#define LAPIC_ICR_LOW_REGISTER 0x300u
#define LAPIC_ICR_HIGH_REGISTER 0x310u
#define LAPIC_LVT_TIMER_REGISTER 0x320u
#define LAPIC_TIMER_INITIAL_REGISTER 0x380u
#define LAPIC_TIMER_DIVIDE_REGISTER 0x3E0u

#define LAPIC_ICR_DELIVERY_PENDING (1u << 12)
#define LAPIC_ICR_INIT_ASSERT 0x0000C500u
#define LAPIC_ICR_INIT_DEASSERT 0x00008500u
#define LAPIC_ICR_STARTUP 0x00000600u
#define LAPIC_SVR_ENABLE (1u << 8)
#define LAPIC_TIMER_PERIODIC (1u << 17)
#define LAPIC_TIMER_VECTOR 0xF0u
#define LAPIC_RESCHEDULE_VECTOR 0xF1u

#define AP_TRAMPOLINE_PHYSICAL 0x00008000u
#define AP_TRAMPOLINE_VECTOR (AP_TRAMPOLINE_PHYSICAL >> 12)
#define AP_BOOT_STACK_SIZE (16u * 1024u)
#define AP_START_TIMEOUT 4000000u

/*
 * ==========================================================================
 * MADT STRUCTURES
 * ==========================================================================
 */

typedef struct acpi_madt
{
    acpi_sdt_header_t header;

    /*
     * Physical MMIO address of Local APIC.
     *
     * Usually 0xFEE00000 on traditional x86.
     */
    uint32_t local_apic_address;

    uint32_t flags;

    /*
     * Variable-length MADT entries follow immediately.
     */

} __attribute__((packed)) acpi_madt_t;

typedef struct madt_entry_header
{
    uint8_t type;
    uint8_t length;

} __attribute__((packed)) madt_entry_header_t;

typedef struct madt_local_apic
{
    madt_entry_header_t header;

    uint8_t acpi_processor_id;
    uint8_t apic_id;

    uint32_t flags;

} __attribute__((packed)) madt_local_apic_t;

/*
 * ==========================================================================
 * DISCOVERED CPU STATE
 * ==========================================================================
 */

static smp_cpu_t cpus[SMP_MAX_CPUS];

static size_t cpu_count = 0;

static uintptr_t local_apic_physical_address = 0;

/* One BSP-controlled launch slot is sufficient because APs are started serially. */
static volatile uint32_t ap_ready[SMP_MAX_CPUS];

extern uint8_t ap_trampoline_start;
extern uint8_t ap_trampoline_end;
extern uint32_t ap_trampoline_stack;
extern uint32_t ap_trampoline_cr3;
extern uint32_t ap_trampoline_entry;

static volatile uint32_t *lapic = NULL;

static void smp_ap_entry(void) __attribute__((noreturn));

static void lapic_write(uint32_t offset, uint32_t value)
{
    lapic[offset / sizeof(*lapic)] = value;
    (void)lapic[LAPIC_ID_REGISTER / sizeof(*lapic)];
}

static uint32_t lapic_read(uint32_t offset)
{
    return lapic[offset / sizeof(*lapic)];
}

static void spin_delay(uint32_t iterations)
{
    while (iterations-- != 0)
        __asm__ volatile("pause");
}

static bool lapic_wait_icr_idle(void)
{
    for (uint32_t i = 0; i < AP_START_TIMEOUT; ++i)
    {
        if ((lapic_read(LAPIC_ICR_LOW_REGISTER) & LAPIC_ICR_DELIVERY_PENDING) == 0)
            return true;

        __asm__ volatile("pause");
    }

    return false;
}

static void lapic_send_ipi(uint8_t apic_id, uint32_t command)
{
    lapic_write(LAPIC_ICR_HIGH_REGISTER, (uint32_t)apic_id << 24);
    lapic_write(LAPIC_ICR_LOW_REGISTER, command);
}

static void lapic_enable_timer(void)
{
    lapic_write(LAPIC_SVR_REGISTER, LAPIC_SVR_ENABLE | 0xFFu);
    lapic_write(LAPIC_TIMER_DIVIDE_REGISTER, 0x3u); /* divide by 16 */
    lapic_write(
        LAPIC_LVT_TIMER_REGISTER,
        LAPIC_TIMER_PERIODIC | LAPIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INITIAL_REGISTER, 100000u);
}

static void lapic_timer_handler(struct interrupt_frame *frame)
{
    (void)frame;

    lapic_write(LAPIC_EOI_REGISTER, 0);
    scheduler_tick();
}

static void lapic_reschedule_handler(struct interrupt_frame *frame)
{
    (void)frame;

    lapic_write(LAPIC_EOI_REGISTER, 0);

    cpu_local_t *cpu = cpu_current();

    if (cpu != NULL)
    {
        cpu->reschedule_pending = true;
        ++cpus[cpu->index].reschedule_ipi_count;
    }
}

/*
 * ==========================================================================
 * CPUID
 * ==========================================================================
 *
 * We need the APIC ID of the CPU currently executing the kernel so that
 * we can mark the bootstrap processor.
 *
 * CPUID leaf 1 returns the initial APIC ID in EBX[31:24].
 */

static uint8_t current_cpu_initial_apic_id(void)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    eax = 1u;

    __asm__ volatile(
        "cpuid"
        : "+a"(eax),
          "=b"(ebx),
          "=c"(ecx),
          "=d"(edx));

    return (uint8_t)((ebx >> 24) &
                     0xFFu);
}

/*
 * ==========================================================================
 * CPU DETECTION
 * ==========================================================================
 */

bool smp_detect_cpus(void)
{
    cpu_count = 0;

    local_apic_physical_address = 0;
    for (size_t i = 0; i < SMP_MAX_CPUS; ++i)
        cpus[i] = (smp_cpu_t){0};

    const acpi_sdt_header_t *header =
        acpi_find_table(
            "APIC");

    if (header == NULL)
        return false;

    if (header->length <
        sizeof(acpi_madt_t))
    {
        return false;
    }

    const acpi_madt_t *madt =
        (const acpi_madt_t *)header;

    local_apic_physical_address =
        (uintptr_t)
            madt->local_apic_address;

    uint8_t bsp_apic_id =
        current_cpu_initial_apic_id();

    const uint8_t *current =
        (const uint8_t *)madt +
        sizeof(acpi_madt_t);

    const uint8_t *end =
        (const uint8_t *)madt +
        madt->header.length;

    /*
     * MADT entries are variable-length.
     *
     * Every entry begins with:
     *
     *     type
     *     length
     */
    while (current +
               sizeof(madt_entry_header_t) <=
           end)
    {
        const madt_entry_header_t *entry =
            (const madt_entry_header_t *)
                current;

        /*
         * A zero or undersized entry would otherwise cause an infinite
         * loop or invalid memory access.
         */
        if (entry->length <
            sizeof(madt_entry_header_t))
        {
            return false;
        }

        if (current +
                entry->length >
            end)
        {
            return false;
        }

        if (entry->type ==
            MADT_ENTRY_PROCESSOR_LOCAL_APIC)
        {
            if (entry->length >=
                sizeof(madt_local_apic_t))
            {
                const madt_local_apic_t *lapic =
                    (const madt_local_apic_t *)
                        current;

                bool enabled =
                    (lapic->flags &
                     MADT_LOCAL_APIC_ENABLED) != 0;

                /*
                 * Ignore disabled processors.
                 */
                if (enabled)
                {
                    /*
                     * Keep enumeration bounded.
                     */
                    if (cpu_count >=
                        SMP_MAX_CPUS)
                    {
                        return false;
                    }

                    smp_cpu_t *cpu =
                        &cpus[cpu_count];

                    cpu->index =
                        cpu_count;

                    cpu->processor_id =
                        lapic->acpi_processor_id;

                    cpu->apic_id =
                        lapic->apic_id;

                    cpu->enabled =
                        true;

                    cpu->bootstrap_processor =
                        lapic->apic_id ==
                        bsp_apic_id;

                    /*
                     * Only BSP is actually executing kernel code.
                     */
                    cpu->online =
                        cpu->bootstrap_processor;

                    ++cpu_count;
                }
            }
        }

        current +=
            entry->length;
    }

    /*
     * A usable MADT should describe at least the BSP.
     */
    if (cpu_count == 0)
        return false;

    /*
     * Sanity check: our currently executing BSP must have appeared in
     * the MADT.
     */
    bool found_bsp =
        false;

    for (size_t i = 0;
         i < cpu_count;
         ++i)
    {
        if (cpus[i].bootstrap_processor)
        {
            found_bsp = true;
            break;
        }
    }

    return found_bsp;
}

/*
 * ==========================================================================
 * ACCESSORS
 * ==========================================================================
 */

size_t smp_cpu_count(void)
{
    return cpu_count;
}

const smp_cpu_t *smp_get_cpu(
    size_t index)
{
    if (index >= cpu_count)
        return NULL;

    return &cpus[index];
}

uintptr_t smp_lapic_address(void)
{
    return local_apic_physical_address;
}

size_t smp_online_cpu_count(void)
{
    size_t count = 0;

    for (size_t i = 0; i < cpu_count; ++i)
    {
        if (cpus[i].online)
            ++count;
    }

    return count;
}

static void smp_ap_entry(void)
{
    /*
     * The temporary trampoline GDT got us here.
     * Switch to the kernel-owned descriptor tables.
     */
    gdt_load();
    idt_load();

    cpu_local_t *cpu =
        cpu_current();

    if (cpu == NULL ||
        cpu->index >= cpu_count)
    {
        goto halt;
    }

    /*
     * Every CPU owns a distinct hardware TSS descriptor.
     */
    if (!gdt_load_tss(
            cpu->index))
    {
        goto halt;
    }

    /*
     * Give the AP its CPU-local bootstrap/idle task structures.
     *
     * IMPORTANT:
     *
     * For the current scheduler milestone APs do NOT participate
     * in normal task scheduling.
     *
     * The existing scheduler uses global run queues but carries
     * context-switch handoff state inside cpu_local_t. Allowing a
     * task to resume on a different CPU is therefore unsafe.
     *
     * Keep AP task structures initialized so SMP infrastructure
     * remains valid, but do not enable scheduler preemption and do
     * not call task_yield() from the AP bootstrap context.
     */
    if (!task_initialize_cpu())
    {
        goto halt;
    }

    /*
     * AP is alive and initialized.
     */
    cpu->online =
        true;

    cpus[cpu->index].online =
        true;

    /*
     * Tell the BSP startup code that this AP reached its stable
     * parked state.
     */
    __asm__ volatile(
        ""
        :
        :
        : "memory");

    ap_ready[cpu->index] =
        1u;

    /*
     * ------------------------------------------------------------------
     * TEMPORARY SMP POLICY
     * ------------------------------------------------------------------
     *
     * Park APs until the scheduler has per-CPU run queues or explicit
     * task affinity.
     *
     * Do NOT:
     *
     *     scheduler_enable_preemption();
     *     task_yield();
     *
     * here.
     *
     * Those operations allow global scheduler tasks to migrate onto
     * this CPU, while scheduler_finish_switch() relies on CPU-local
     * handoff state.
     *
     * Interrupts remain disabled on this AP, so it stays completely
     * outside scheduler execution.
     */
    for (;;)
    {
        __asm__ volatile(
            "cli; hlt");
    }

halt:
    for (;;)
    {
        __asm__ volatile(
            "cli; hlt");
    }
}

bool smp_start_aps(void)
{
    if (cpu_count == 0 || local_apic_physical_address == 0)
        return false;

    if (!paging_identity_map_range(
            local_apic_physical_address,
            PAGE_SIZE,
            PAGE_WRITABLE))
    {
        return false;
    }

    lapic = (volatile uint32_t *)local_apic_physical_address;

    /* The BSP must enable its own LAPIC before issuing INIT/SIPI IPIs. */
    lapic_write(LAPIC_SVR_REGISTER, LAPIC_SVR_ENABLE | 0xFFu);

    if (interrupt_register_handler(LAPIC_TIMER_VECTOR, lapic_timer_handler) != 0 ||
        interrupt_register_handler(LAPIC_RESCHEDULE_VECTOR,
                                   lapic_reschedule_handler) != 0)
        return false;

    size_t trampoline_size =
        (size_t)(&ap_trampoline_end - &ap_trampoline_start);

    if (trampoline_size == 0 || trampoline_size > PAGE_SIZE)
        return false;

    volatile uint8_t *trampoline_destination =
        (volatile uint8_t *)AP_TRAMPOLINE_PHYSICAL;
    const uint8_t *trampoline_source = &ap_trampoline_start;

    for (size_t i = 0; i < trampoline_size; ++i)
        trampoline_destination[i] = trampoline_source[i];

    uintptr_t cr3;
    __asm__ volatile("movl %%cr3, %0" : "=r"(cr3));

    for (size_t i = 0; i < cpu_count; ++i)
    {
        smp_cpu_t *cpu = &cpus[i];

        if (!cpu->enabled || cpu->bootstrap_processor)
            continue;

        void *stack = kmalloc(AP_BOOT_STACK_SIZE);

        if (stack == NULL)
            return false;

        ap_ready[i] = 0;

        *(volatile uint32_t *)(AP_TRAMPOLINE_PHYSICAL +
                               ((uintptr_t)&ap_trampoline_stack - (uintptr_t)&ap_trampoline_start)) =
            (uint32_t)((uintptr_t)stack + AP_BOOT_STACK_SIZE);
        *(volatile uint32_t *)(AP_TRAMPOLINE_PHYSICAL +
                               ((uintptr_t)&ap_trampoline_cr3 - (uintptr_t)&ap_trampoline_start)) =
            (uint32_t)cr3;
        *(volatile uint32_t *)(AP_TRAMPOLINE_PHYSICAL +
                               ((uintptr_t)&ap_trampoline_entry - (uintptr_t)&ap_trampoline_start)) =
            (uint32_t)(uintptr_t)smp_ap_entry;

        __asm__ volatile("" ::: "memory");

        if (!lapic_wait_icr_idle())
            return false;

        lapic_send_ipi((uint8_t)cpu->apic_id, LAPIC_ICR_INIT_ASSERT);
        spin_delay(100000u);
        lapic_send_ipi((uint8_t)cpu->apic_id, LAPIC_ICR_INIT_DEASSERT);
        spin_delay(100000u);

        for (size_t sipi = 0; sipi < 2u; ++sipi)
        {
            if (!lapic_wait_icr_idle())
                return false;

            lapic_send_ipi(
                (uint8_t)cpu->apic_id,
                LAPIC_ICR_STARTUP | AP_TRAMPOLINE_VECTOR);
            spin_delay(200000u);
        }

        bool started = false;

        for (uint32_t wait = 0; wait < AP_START_TIMEOUT; ++wait)
        {
            if (ap_ready[i] != 0u)
            {
                started = true;
                break;
            }

            __asm__ volatile("pause");
        }

        if (!started)
            return false;
    }

    return true;
}

void smp_request_reschedule(void)
{
    if (lapic == NULL || smp_online_cpu_count() < 2u)
        return;

    cpu_local_t *local = cpu_current();

    for (size_t i = 0; i < cpu_count; ++i)
    {
        const smp_cpu_t *target = &cpus[i];

        if (!target->online ||
            (local != NULL && target->apic_id == local->apic_id))
        {
            continue;
        }

        if (!lapic_wait_icr_idle())
            return;

        lapic_send_ipi(
            (uint8_t)target->apic_id,
            LAPIC_RESCHEDULE_VECTOR);
    }
}
