#include <stddef.h>
#include <stdint.h>

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

enum
{
    GDT_ENTRY_NULL,
    GDT_ENTRY_KERNEL_CODE,
    GDT_ENTRY_KERNEL_DATA,
    GDT_ENTRY_COUNT
};

static struct gdt_entry gdt[GDT_ENTRY_COUNT];
static struct gdt_pointer gdtr;

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

    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base = (uint32_t)(uintptr_t)gdt;

    gdt_load();
}

void gdt_load(void)
{
    gdt_flush(&gdtr);
}
