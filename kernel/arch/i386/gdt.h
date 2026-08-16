#ifndef ARCH_I386_GDT_H
#define ARCH_I386_GDT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Ring 0 selectors.
 */
#define GDT_KERNEL_CODE_SELECTOR 0x08
#define GDT_KERNEL_DATA_SELECTOR 0x10

/*
 * Ring 3 descriptor selectors without RPL bits.
 */
#define GDT_USER_CODE_DESCRIPTOR 0x18
#define GDT_USER_DATA_DESCRIPTOR 0x20

/*
 * Ring 3 selectors with Requested Privilege Level 3.
 */
#define GDT_USER_CODE_SELECTOR \
    (GDT_USER_CODE_DESCRIPTOR | 0x03) /* 0x1B */

#define GDT_USER_DATA_SELECTOR \
    (GDT_USER_DATA_DESCRIPTOR | 0x03) /* 0x23 */

/*
 * First per-CPU 32-bit TSS descriptor.
 *
 * CPU n uses:
 *
 *     GDT_TSS_BASE_SELECTOR + n * 8
 */
#define GDT_TSS_BASE_SELECTOR 0x28

void gdt_initialize(void);
void gdt_load(void);

/*
 * Load the TSS belonging to one kernel CPU into TR.
 */
bool gdt_load_tss(size_t cpu_index);

/*
 * Set the ring-0 stack used by one CPU when entering the kernel
 * from CPL3.
 */
bool gdt_set_kernel_stack(
    size_t cpu_index,
    uintptr_t stack_pointer);

/*
 * Implemented in gdt_flush.S.
 *
 * Loads GDTR and reloads the 32-bit protected-mode segment registers.
 */
void gdt_flush(const void *gdtr);

#endif
