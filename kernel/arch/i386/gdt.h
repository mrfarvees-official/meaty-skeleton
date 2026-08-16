#ifndef ARCH_I386_GDT_H
#define ARCH_I386_GDT_H

#include <stdint.h>

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
 * Optional 32-bit TSS selector.
 *
 * Remove this definition until a 32-bit TSS descriptor is actually
 * added to the GDT.
 */
#define GDT_TSS_SELECTOR 0x28

void gdt_initialize(void);
void gdt_load(void);

/*
 * Set the ring-0 stack used when the CPU enters the kernel from CPL3.
 *
 * U1a uses this only for the BSP test.  Later U1 work must make the
 * backing TSS per-CPU before userspace is allowed to run on APs.
 */
void gdt_set_kernel_stack(uintptr_t stack_pointer);

/*
 * Implemented in gdt_flush.S.
 *
 * Loads GDTR and reloads the 32-bit protected-mode segment registers.
 */
void gdt_flush(const void* gdtr);

#endif
