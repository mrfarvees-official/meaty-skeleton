#ifndef KERNEL_ADDRESS_SPACE_H
#define KERNEL_ADDRESS_SPACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * --------------------------------------------------------------------------
 * USER THREAD STACK LAYOUT
 * --------------------------------------------------------------------------
 *
 * Userspace ends at:
 *
 *     0xC0000000
 *
 * Each thread receives one fixed 1 MiB stack slot.
 *
 * Beneath every stack is one unmapped 4 KiB guard page.
 *
 * Layout:
 *
 *     0xC0000000
 *          |
 *          | slot 0 stack: 1 MiB
 *          |
 *     0xBFF00000
 *          |
 *          | guard page
 *          |
 *     0xBFEFF000
 *          |
 *          | slot 1 stack: 1 MiB
 *          |
 *          ...
 *
 * The guard page is only a RESERVED virtual region in U12.3A.
 *
 * Actual physical stack pages are introduced in U12.3B.
 */

#define ADDRESS_SPACE_USER_STACK_SIZE \
    (1024u * 1024u)

#define ADDRESS_SPACE_USER_STACK_GUARD_SIZE \
    4096u

/*
 * Initial implementation supports 32 simultaneous stack slots
 * per userspace address space.
 *
 * This is intentionally a simple uint32_t bitmap for now.
 *
 * It can later become a larger dynamic bitmap without changing
 * the task/thread ownership model.
 */
#define ADDRESS_SPACE_USER_STACK_SLOT_COUNT \
    32u

#define ADDRESS_SPACE_USER_STACK_REGION_TOP \
    ((uintptr_t)0xC0000000u)

/*
 * Description of one reserved user-stack virtual slot.
 *
 * Ranges use the normal half-open convention:
 *
 *     stack:
 *         [stack_bottom, stack_top)
 *
 *     guard:
 *         [guard_bottom, guard_top)
 *
 * and:
 *
 *     guard_top == stack_bottom
 */
typedef struct address_space_user_stack_slot
{
    size_t index;

    uintptr_t stack_bottom;
    uintptr_t stack_top;

    uintptr_t guard_bottom;
    uintptr_t guard_top;

} address_space_user_stack_slot_t;

/*
 * A physically-backed userspace thread stack.
 *
 * The slot describes the reserved virtual range.
 *
 * All pages in:
 *
 *     [stack_bottom, stack_top)
 *
 * are mapped PAGE_USER | PAGE_WRITABLE.
 *
 * The guard range:
 *
 *     [guard_bottom, guard_top)
 *
 * remains deliberately unmapped.
 */
typedef struct address_space_user_stack
{
    size_t slot_index;

    uintptr_t stack_bottom;
    uintptr_t stack_top;

    uintptr_t guard_bottom;
    uintptr_t guard_top;

    size_t mapped_page_count;

} address_space_user_stack_t;

/*
 * --------------------------------------------------------------------------
 * ADDRESS SPACE
 * --------------------------------------------------------------------------
 */

typedef struct address_space address_space_t;

/*
 * Initialize the address-space subsystem.
 *
 * Must be called after paging_initialize().
 */
bool address_space_initialize(void);

/*
 * Return the immortal canonical kernel address space.
 */
address_space_t *address_space_kernel(void);

/*
 * Adopt an already-created userspace page directory.
 *
 * On success:
 *
 *     returned address_space_t owns page_directory
 *     reference_count == 1
 *
 * On failure:
 *
 *     ownership remains with caller
 */
address_space_t *address_space_adopt_user(
    uintptr_t page_directory);

/*
 * --------------------------------------------------------------------------
 * REFERENCE MANAGEMENT
 * --------------------------------------------------------------------------
 */

bool address_space_retain(
    address_space_t *space);

bool address_space_release(
    address_space_t *space);

/*
 * --------------------------------------------------------------------------
 * BASIC ACCESSORS
 * --------------------------------------------------------------------------
 */

uintptr_t address_space_page_directory(
    const address_space_t *space);

bool address_space_is_kernel(
    const address_space_t *space);

size_t address_space_reference_count(
    address_space_t *space);

/*
 * --------------------------------------------------------------------------
 * USER STACK SLOT MANAGEMENT
 * --------------------------------------------------------------------------
 *
 * U12.3A manages only virtual-range ownership.
 *
 * These functions DO NOT:
 *
 *     allocate physical frames
 *     create page mappings
 *     unmap pages
 *     modify CR3
 *
 * U12.3B will build physical stack allocation on top of this layer.
 */

/*
 * Reserve the first free user-stack slot.
 *
 * On success:
 *
 *     - the slot becomes unavailable to other threads
 *     - out_slot describes its virtual stack and guard ranges
 *
 * Kernel address spaces cannot allocate user-stack slots.
 */
bool address_space_user_stack_slot_reserve(
    address_space_t *space,
    address_space_user_stack_slot_t *out_slot);

/*
 * Reserve one specific slot.
 *
 * This is important for the existing ELF main-thread stack:
 *
 *     slot 0
 *     0xBFF00000 - 0xC0000000
 *
 * Later spawn.c can explicitly register that already-created stack
 * instead of allowing a worker thread to reuse its virtual range.
 */
bool address_space_user_stack_slot_reserve_index(
    address_space_t *space,
    size_t index,
    address_space_user_stack_slot_t *out_slot);

/*
 * Release one previously-reserved slot.
 *
 * U12.3A only releases allocator metadata.
 *
 * Physical stack mappings do not exist yet.
 */
bool address_space_user_stack_slot_release(
    address_space_t *space,
    size_t index);

/*
 * Diagnostic helper.
 *
 * Returns the number of currently reserved stack slots.
 */
size_t address_space_user_stack_slot_reserved_count(
    address_space_t *space);

/*
 * Register a stack which has already been physically mapped.
 *
 * This is used for the ELF main thread because elf.c already creates
 * its initial 1 MiB userspace stack.
 *
 * No physical frames are allocated here.
 */
bool address_space_user_stack_register_existing(
    address_space_t *space,
    size_t slot_index,
    address_space_user_stack_t *out_stack);

/*
 * Allocate and map a new userspace thread stack.
 *
 * The function:
 *
 *     - reserves one free stack slot
 *     - allocates physical frames
 *     - zeroes every frame
 *     - maps every stack page PAGE_USER | PAGE_WRITABLE
 *     - leaves the guard page unmapped
 *
 * For U12.3B this must be called while the canonical kernel
 * address space is active.
 */
bool address_space_user_stack_create(
    address_space_t *space,
    address_space_user_stack_t *out_stack);

#endif