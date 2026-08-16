#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
    GDT_ENTRY_TSS,
    GDT_ENTRY_COUNT
};

static struct gdt_entry gdt[GDT_ENTRY_COUNT];
static struct gdt_pointer gdtr;
static struct tss32 tss;

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
     * Entry 5, selector 0x28:
     * Available 32-bit TSS.
     */
    memset(&tss, 0, sizeof(tss));

    tss.ss0 = GDT_KERNEL_DATA_SELECTOR;

    /*
     * Place the I/O bitmap beyond the TSS limit.  CPL3 therefore
     * receives no direct I/O-port access.
     */
    tss.iomap_base = sizeof(tss);

    gdt_set_entry(
        GDT_ENTRY_TSS,
        (uint32_t)(uintptr_t)&tss,
        sizeof(tss) - 1u,
        0x89,
        0x00);

    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base = (uint32_t)(uintptr_t)gdt;

    gdt_load();

    /*
     * Load the task register.
     *
     * U1a intentionally uses this TSS only on the BSP.
     */
    __asm__ volatile(
        "ltr %0"
        :
        : "r"((uint16_t)GDT_TSS_SELECTOR)
        : "memory");
}

void gdt_load(void)
{
    gdt_flush(&gdtr);
}

void gdt_set_kernel_stack(uintptr_t stack_pointer)
{
    tss.ss0 = GDT_KERNEL_DATA_SELECTOR;
    tss.esp0 = (uint32_t)stack_pointer;
}