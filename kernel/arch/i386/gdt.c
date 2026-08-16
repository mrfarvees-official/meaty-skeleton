#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/cpu.h>
#include <kernel/smp.h>

#include "gdt.h"

struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t flags_limit;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_pointer
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss32
{
    uint32_t previous_task;

    uint32_t esp0;
    uint32_t ss0;

    uint32_t esp1;
    uint32_t ss1;

    uint32_t esp2;
    uint32_t ss2;

    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;

    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;

    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;

    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;

    uint32_t ldt;

    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

enum
{
    GDT_ENTRY_NULL,
    GDT_ENTRY_KERNEL_CODE,
    GDT_ENTRY_KERNEL_DATA,
    GDT_ENTRY_USER_CODE,
    GDT_ENTRY_USER_DATA,

    /*
     * One TSS descriptor per possible CPU.
     */
    GDT_ENTRY_TSS_BASE,

    GDT_ENTRY_COUNT =
        GDT_ENTRY_TSS_BASE +
        SMP_MAX_CPUS
};

static struct gdt_entry gdt[GDT_ENTRY_COUNT];
static struct gdt_pointer gdtr;

static struct tss32 tss_per_cpu[SMP_MAX_CPUS];

static void gdt_set_entry(
    size_t index,
    uint32_t base,
    uint32_t limit,
    uint8_t access,
    uint8_t flags)
{
    gdt[index].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[index].base_low = (uint16_t)(base & 0xFFFF);
    gdt[index].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[index].access = access;

    /*
     * High nibble: descriptor flags.
     * Low nibble: bits 16-19 of the segment limit.
     */
    gdt[index].flags_limit = (uint8_t)((flags & 0xF0) | ((limit >> 16) & 0x0F));

    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFF);
}

void gdt_initialize(void)
{
    /*
     * Entry 0: required null descriptor.
     */
    gdt_set_entry(
        GDT_ENTRY_NULL,
        0,
        0,
        0,
        0);

    /*
     * Entry 1, selector 0x08:
     * 32-bit ring-0 code segment.
     *
     * Access 0x9A:
     *   P   = 1
     *   DPL = 0
     *   S   = 1
     *   E   = 1
     *   DC  = 0
     *   RW  = 1
     *   A   = 0
     *
     * Flags 0xC0:
     *   G   = 1
     *   D/B = 1
     *   L   = 0
     *   AVL = 0
     */
    gdt_set_entry(
        GDT_ENTRY_KERNEL_CODE,
        0,
        0xFFFFF,
        0x9A,
        0xC0);

    /*
     * Entry 2, selector 0x10:
     * 32-bit ring-0 data and stack segment.
     *
     * Access 0x92:
     *   P   = 1
     *   DPL = 0
     *   S   = 1
     *   E   = 0
     *   DC  = 0
     *   RW  = 1
     *   A   = 0
     */
    gdt_set_entry(
        GDT_ENTRY_KERNEL_DATA,
        0,
        0xFFFFF,
        0x92,
        0xC0);

    /*
     * Entry 3, selector 0x18 / user selector 0x1B:
     * 32-bit ring-3 code segment.
     */
    gdt_set_entry(
        GDT_ENTRY_USER_CODE,
        0,
        0xFFFFF,
        0xFA,
        0xC0);

    /*
     * Entry 4, selector 0x20 / user selector 0x23:
     * 32-bit ring-3 data and stack segment.
     */
    gdt_set_entry(
        GDT_ENTRY_USER_DATA,
        0,
        0xFFFFF,
        0xF2,
        0xC0);

    /*
     * Entries 5 onward:
     *
     * One available 32-bit TSS descriptor for every possible CPU.
     *
     * A CPU must never share a loaded hardware TSS descriptor with
     * another CPU.
     */
    for (size_t i = 0; i < SMP_MAX_CPUS; ++i)
    {
        struct tss32 *cpu_tss =
            &tss_per_cpu[i];

        memset(
            cpu_tss,
            0,
            sizeof(*cpu_tss));

        cpu_tss->ss0 =
            GDT_KERNEL_DATA_SELECTOR;

        /*
         * Put the I/O bitmap beyond the descriptor limit.
         *
         * User mode therefore has no direct I/O-port permission.
         */
        cpu_tss->iomap_base =
            sizeof(*cpu_tss);

        gdt_set_entry(
            GDT_ENTRY_TSS_BASE + i,
            (uint32_t)(uintptr_t)cpu_tss,
            sizeof(*cpu_tss) - 1u,
            0x89,
            0x00);
    }

    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base = (uint32_t)(uintptr_t)gdt;

    gdt_load();
}

void gdt_load(void)
{
    gdt_flush(&gdtr);
}

bool gdt_load_tss(size_t cpu_index)
{
    if (cpu_index >= SMP_MAX_CPUS)
        return false;

    uint16_t selector =
        (uint16_t)(GDT_TSS_BASE_SELECTOR +
                   cpu_index * sizeof(struct gdt_entry));

    __asm__ volatile(
        "ltr %0"
        :
        : "r"(selector)
        : "memory");

    return true;
}

bool gdt_set_kernel_stack(
    size_t cpu_index,
    uintptr_t stack_pointer)
{
    if (cpu_index >= SMP_MAX_CPUS)
        return false;

    struct tss32 *cpu_tss =
        &tss_per_cpu[cpu_index];

    cpu_tss->ss0 =
        GDT_KERNEL_DATA_SELECTOR;

    cpu_tss->esp0 =
        (uint32_t)stack_pointer;

    return true;
}

bool gdt_set_current_kernel_stack(
    uintptr_t stack_pointer)
{
    cpu_local_t *cpu =
        cpu_current();

    if (cpu == NULL)
        return false;

    return gdt_set_kernel_stack(
        cpu->index,
        stack_pointer);
}

bool gdt_get_kernel_stack(
    size_t cpu_index,
    uintptr_t *stack_pointer)
{
    if (stack_pointer == NULL)
        return false;

    if (cpu_index >= SMP_MAX_CPUS)
        return false;

    *stack_pointer =
        (uintptr_t)tss_per_cpu[cpu_index].esp0;

    return true;
}