#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/address_space.h>
#include <kernel/heap.h>
#include <kernel/paging.h>
#include <kernel/spinlock.h>

#include "../arch/i386/interrupts.h"

/*
 * --------------------------------------------------------------------------
 * ADDRESS-SPACE OBJECT
 * --------------------------------------------------------------------------
 */

struct address_space
{
    /*
     * Physical page-directory frame loaded into CR3.
     *
     * This value is immutable during the lifetime of the address space.
     */
    uintptr_t page_directory;

    /*
     * Number of live owners/users of this address space.
     *
     * Protected by lock.
     */
    size_t reference_count;

    /*
     * The canonical kernel address space is static and immortal.
     *
     * Userspace address spaces are heap allocated and destroyed when
     * reference_count reaches zero.
     */
    bool kernel_space;

    spinlock_t lock;
};

/*
 * --------------------------------------------------------------------------
 * GLOBAL KERNEL ADDRESS SPACE
 * --------------------------------------------------------------------------
 */

static address_space_t kernel_address_space;

static bool address_space_initialized =
    false;

static spinlock_t address_space_initialization_lock =
    SPINLOCK_INITIALIZER;

/*
 * --------------------------------------------------------------------------
 * INITIALIZATION
 * --------------------------------------------------------------------------
 */

bool address_space_initialize(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &address_space_initialization_lock);

    /*
     * Initialization is idempotent.
     */
    if (address_space_initialized)
    {
        spin_unlock_irqrestore(
            &address_space_initialization_lock,
            flags);

        return true;
    }

    uintptr_t kernel_directory =
        paging_kernel_directory();

    if (kernel_directory == 0 ||
        (kernel_directory &
         (PAGE_SIZE - 1u)) != 0)
    {
        spin_unlock_irqrestore(
            &address_space_initialization_lock,
            flags);

        return false;
    }

    memset(
        &kernel_address_space,
        0,
        sizeof(kernel_address_space));

    kernel_address_space.page_directory =
        kernel_directory;

    /*
     * The kernel address space never reaches zero and is never destroyed.
     *
     * Keep one permanent reference as a useful diagnostic value.
     */
    kernel_address_space.reference_count =
        1u;

    kernel_address_space.kernel_space =
        true;

    spinlock_initialize(
        &kernel_address_space.lock);

    address_space_initialized =
        true;

    spin_unlock_irqrestore(
        &address_space_initialization_lock,
        flags);

    return true;
}

/*
 * --------------------------------------------------------------------------
 * KERNEL ADDRESS SPACE
 * --------------------------------------------------------------------------
 */

address_space_t *address_space_kernel(void)
{
    if (!address_space_initialized)
        return NULL;

    return &kernel_address_space;
}

/*
 * --------------------------------------------------------------------------
 * USER ADDRESS-SPACE CREATION
 * --------------------------------------------------------------------------
 */

address_space_t *address_space_adopt_user(
    uintptr_t page_directory)
{
    if (!address_space_initialized)
        return NULL;

    uintptr_t kernel_directory =
        paging_kernel_directory();

    if (kernel_directory == 0)
        return NULL;

    /*
     * For now address-space ownership transitions occur from the canonical
     * kernel address space.
     */
    if (paging_current_directory() !=
        kernel_directory)
    {
        return NULL;
    }

    /*
     * User address spaces must be distinct from the canonical kernel CR3
     * and page aligned.
     */
    if (page_directory == 0 ||
        page_directory ==
            kernel_directory ||
        (page_directory &
         (PAGE_SIZE - 1u)) != 0)
    {
        return NULL;
    }

    address_space_t *space =
        kmalloc(
            sizeof(*space));

    if (space == NULL)
        return NULL;

    memset(
        space,
        0,
        sizeof(*space));

    space->page_directory =
        page_directory;

    space->reference_count =
        1u;

    space->kernel_space =
        false;

    spinlock_initialize(
        &space->lock);

    return space;
}

/*
 * --------------------------------------------------------------------------
 * REFERENCE MANAGEMENT
 * --------------------------------------------------------------------------
 */

bool address_space_retain(
    address_space_t *space)
{
    if (space == NULL)
        return false;

    /*
     * The kernel address space is immortal.
     */
    if (space->kernel_space)
        return true;

    uint32_t flags =
        spin_lock_irqsave(
            &space->lock);

    /*
     * reference_count == 0 means destruction has already begun.
     *
     * SIZE_MAX protects against wraparound.
     */
    if (space->reference_count == 0 ||
        space->reference_count ==
            SIZE_MAX)
    {
        spin_unlock_irqrestore(
            &space->lock,
            flags);

        return false;
    }

    ++space->reference_count;

    spin_unlock_irqrestore(
        &space->lock,
        flags);

    return true;
}

bool address_space_release(
    address_space_t *space)
{
    if (space == NULL)
        return false;

    /*
     * The canonical kernel address space is never destroyed.
     */
    if (space->kernel_space)
        return true;

    uint32_t flags =
        spin_lock_irqsave(
            &space->lock);

    if (space->reference_count == 0)
    {
        spin_unlock_irqrestore(
            &space->lock,
            flags);

        return false;
    }

    /*
     * More than one task/object still references this address space.
     *
     * Only drop our reference.
     */
    if (space->reference_count > 1u)
    {
        --space->reference_count;

        spin_unlock_irqrestore(
            &space->lock,
            flags);

        return true;
    }

    /*
     * We are releasing the FINAL reference.
     *
     * The current paging implementation destroys user directories only
     * from the canonical kernel address space.
     *
     * Keep reference_count == 1 if this invariant is not satisfied so
     * the object remains valid rather than becoming an unreachable
     * zero-reference object.
     */
    uintptr_t kernel_directory =
        paging_kernel_directory();

    if (kernel_directory == 0 ||
        paging_current_directory() !=
            kernel_directory ||
        space->page_directory == 0 ||
        space->page_directory ==
            kernel_directory)
    {
        spin_unlock_irqrestore(
            &space->lock,
            flags);

        return false;
    }

    uintptr_t directory =
        space->page_directory;

    /*
     * Mark the object dead while the lock and local interrupt-disable
     * state still protect this final transition.
     */
    space->reference_count =
        0;

    space->page_directory =
        0;

    /*
     * Release the object lock but intentionally leave local interrupts
     * disabled until the directory and wrapper are completely destroyed.
     *
     * This prevents this CPU from being scheduled into another address
     * space halfway through the final-release operation.
     */
    spin_unlock(
        &space->lock);

    paging_destroy_user_directory(
        directory);

    kfree(
        space);

    interrupt_restore(
        flags);

    return true;
}

/*
 * --------------------------------------------------------------------------
 * ACCESSORS
 * --------------------------------------------------------------------------
 */

uintptr_t address_space_page_directory(
    const address_space_t *space)
{
    if (space == NULL)
        return 0;

    return space->page_directory;
}

bool address_space_is_kernel(
    const address_space_t *space)
{
    if (space == NULL)
        return false;

    return space->kernel_space;
}

size_t address_space_reference_count(
    address_space_t *space)
{
    if (space == NULL)
        return 0;

    if (space->kernel_space)
        return space->reference_count;

    uint32_t flags =
        spin_lock_irqsave(
            &space->lock);

    size_t count =
        space->reference_count;

    spin_unlock_irqrestore(
        &space->lock,
        flags);

    return count;
}