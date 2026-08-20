#ifndef KERNEL_ADDRESS_SPACE_H
#define KERNEL_ADDRESS_SPACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * --------------------------------------------------------------------------
 * ADDRESS SPACE
 * --------------------------------------------------------------------------
 *
 * An address space represents one virtual-memory environment.
 *
 * Multiple tasks/threads will eventually be able to reference the same
 * address_space_t.
 *
 * Example:
 *
 *     address_space_t
 *         |
 *         +---- task TID 5
 *         +---- task TID 6
 *         +---- task TID 7
 *
 * All three tasks may therefore execute using the same CR3 while retaining
 * their own scheduler state and kernel stacks.
 *
 * The underlying page directory is destroyed only after the final reference
 * to a userspace address space is released.
 */
typedef struct address_space address_space_t;

/*
 * Initialize the address-space subsystem.
 *
 * This records the canonical kernel address space.
 *
 * Must be called after paging_initialize().
 */
bool address_space_initialize(void);

/*
 * Return the immortal shared kernel address space.
 *
 * Returns NULL if address_space_initialize() has not succeeded.
 */
address_space_t *address_space_kernel(void);

/*
 * Adopt an already-created userspace page directory.
 *
 * On success:
 *
 *     - the returned address_space_t owns page_directory
 *     - initial reference count is 1
 *     - caller must eventually call address_space_release()
 *
 * On failure:
 *
 *     - ownership of page_directory remains with the caller
 *
 * The directory must have been created by the userspace paging machinery
 * and must not be the canonical kernel directory.
 */
address_space_t *address_space_adopt_user(
                    uintptr_t page_directory);

/*
 * Add one reference.
 *
 * Kernel address space is immortal, so retain/release operations on it
 * succeed without changing its lifetime.
 */
bool address_space_retain(
                    address_space_t *space);

/*
 * Release one reference.
 *
 * When the final reference to a userspace address space disappears,
 * its page directory and address_space_t object are destroyed.
 *
 * Final userspace release currently must happen while the canonical
 * kernel CR3 is active.
 */
bool address_space_release(
                    address_space_t *space);

/*
 * Return the physical page-directory frame suitable for CR3.
 *
 * Returns 0 for NULL/invalid input.
 */
uintptr_t address_space_page_directory(
                    const address_space_t *space);

/*
 * True only for the canonical kernel address space.
 */
bool address_space_is_kernel(
                    const address_space_t *space);

/*
 * Diagnostic helper.
 *
 * Returns 0 for NULL.
 *
 * The kernel address space is immortal; its count is not intended for
 * lifetime management.
 */
size_t address_space_reference_count(
                    address_space_t *space);

#endif