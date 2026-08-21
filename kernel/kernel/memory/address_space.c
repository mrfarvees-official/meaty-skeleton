#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/address_space.h>
#include <kernel/heap.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>
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
     * Immutable during this address space's lifetime.
     */
    uintptr_t page_directory;

    /*
     * Number of live owners/users.
     *
     * Protected by lock.
     */
    size_t reference_count;

    /*
     * Bitmap describing which user-thread stack virtual
     * slots are reserved.
     *
     * Bit N:
     *
     *     0 = slot N is free
     *     1 = slot N is reserved
     *
     * Protected by lock.
     *
     * U12.3A tracks virtual ownership only.
     */
    uint32_t user_stack_slot_bitmap;

    /*
     * Canonical kernel address space is static and immortal.
     */
    bool kernel_space;

    /*
     * Protects:
     *
     *     reference_count
     *     user_stack_slot_bitmap
     */
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
 * Dedicated temporary mapping used while zeroing newly allocated
 * userspace stack frames.
 *
 * Kernel heap ends at 0xE0000000, so keep this safely above it.
 *
 * This address belongs only to address-space stack construction.
 */
#define ADDRESS_SPACE_STACK_SCRATCH_ADDRESS ((uintptr_t)0xE1000000u)

/*
 * Serializes use of ADDRESS_SPACE_STACK_SCRATCH_ADDRESS.
 *
 * Multiple CPUs may create userspace threads simultaneously.
 */
static spinlock_t address_space_stack_mapping_lock = SPINLOCK_INITIALIZER;

/*
 * --------------------------------------------------------------------------
 * PHYSICALLY-BACKED USER STACKS
 * --------------------------------------------------------------------------
 */

static void address_space_user_stack_from_slot(
    const address_space_user_stack_slot_t *slot,
    address_space_user_stack_t *stack)
{
    if (slot == NULL ||
        stack == NULL)
    {
        return;
    }

    stack->slot_index =
        slot->index;

    stack->stack_bottom =
        slot->stack_bottom;

    stack->stack_top =
        slot->stack_top;

    stack->guard_bottom =
        slot->guard_bottom;

    stack->guard_top =
        slot->guard_top;

    stack->mapped_page_count =
        0;
}


bool address_space_user_stack_register_existing(
    address_space_t *space,
    size_t slot_index,
    address_space_user_stack_t *out_stack)
{
    if (space == NULL ||
        out_stack == NULL)
    {
        return false;
    }

    memset(
        out_stack,
        0,
        sizeof(*out_stack));

    address_space_user_stack_slot_t slot;

    if (!address_space_user_stack_slot_reserve_index(
            space,
            slot_index,
            &slot))
    {
        return false;
    }

    address_space_user_stack_from_slot(
        &slot,
        out_stack);

    /*
     * Existing stacks are already mapped by another subsystem.
     *
     * The ELF loader currently creates the complete 1 MiB main
     * thread stack.
     */
    out_stack->mapped_page_count =
        ADDRESS_SPACE_USER_STACK_SIZE /
        PAGE_SIZE;

    return true;
}


/*
 * Allocate one physical frame, zero it through a temporary supervisor
 * mapping, then map it into the requested userspace page directory.
 */
static bool address_space_map_zeroed_user_stack_page(
    uintptr_t directory,
    uintptr_t virtual_address)
{
    if (directory == 0)
        return false;

    if ((virtual_address &
         (PAGE_SIZE - 1u)) != 0)
    {
        return false;
    }

    uintptr_t frame =
        pmm_allocate_frame();

    if (frame == 0)
        return false;

    /*
     * Temporarily expose the physical frame to the kernel so it can
     * be zeroed before userspace receives access.
     */
    if (!paging_map_page(
            ADDRESS_SPACE_STACK_SCRATCH_ADDRESS,
            frame,
            PAGE_WRITABLE))
    {
        pmm_free_frame(
            frame);

        return false;
    }

    memset(
        (void *)
            ADDRESS_SPACE_STACK_SCRATCH_ADDRESS,
        0,
        PAGE_SIZE);

    /*
     * Remove only the temporary mapping.
     *
     * The frame itself must remain allocated because it is about to
     * become the backing store for the userspace stack page.
     */
    if (!paging_unmap_page(
            ADDRESS_SPACE_STACK_SCRATCH_ADDRESS,
            false))
    {
        /*
         * Losing control of the scratch mapping is an architectural
         * invariant failure. Continuing could alias later frames.
         */
        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    /*
     * Install the zeroed frame into the inactive user directory.
     */
    if (!paging_map_page_in_directory(
            directory,
            virtual_address,
            frame,
            PAGE_USER |
                PAGE_WRITABLE))
    {
        /*
         * Mapping failed, therefore no userspace mapping owns this
         * frame yet.
         */
        pmm_free_frame(
            frame);

        return false;
    }

    return true;
}


bool address_space_user_stack_create(
    address_space_t *space,
    address_space_user_stack_t *out_stack)
{
    if (space == NULL ||
        out_stack == NULL)
    {
        return false;
    }

    if (address_space_is_kernel(
            space))
    {
        return false;
    }

    /*
     * U12.3B constructs stack mappings from the canonical kernel
     * address space.
     *
     * paging_map_page_in_directory() currently requires this.
     */
    uintptr_t kernel_directory =
        paging_kernel_directory();

    if (kernel_directory == 0 ||
        paging_current_directory() !=
            kernel_directory)
    {
        return false;
    }

    uintptr_t directory =
        address_space_page_directory(
            space);

    if (directory == 0 ||
        directory ==
            kernel_directory)
    {
        return false;
    }

    memset(
        out_stack,
        0,
        sizeof(*out_stack));

    address_space_user_stack_slot_t slot;

    /*
     * Reserve the virtual range first.
     *
     * The address-space lock inside the slot allocator prevents two
     * CPUs from selecting the same user stack slot.
     */
    if (!address_space_user_stack_slot_reserve(
            space,
            &slot))
    {
        return false;
    }

    address_space_user_stack_from_slot(
        &slot,
        out_stack);

    /*
     * Protect the temporary scratch VA while frames are being zeroed.
     *
     * Keep this lock across construction of the complete stack so
     * another CPU cannot attempt to use the same scratch mapping.
     */
    uint32_t mapping_flags =
        spin_lock_irqsave(
            &address_space_stack_mapping_lock);

    size_t mapped_pages =
        0;

    for (uintptr_t virtual_page =
             slot.stack_bottom;
         virtual_page <
             slot.stack_top;
         virtual_page +=
             PAGE_SIZE)
    {
        if (!address_space_map_zeroed_user_stack_page(
                directory,
                virtual_page))
        {
            /*
             * U12.3B bring-up rule:
             *
             * paging.c does not yet expose
             * paging_unmap_page_in_directory().
             *
             * Therefore a partially constructed stack cannot yet be
             * safely rolled back page-by-page.
             *
             * Release the allocator lock and report failure.
             *
             * The containing address space must then be destroyed by
             * the caller, which releases all PAGE_USER mappings and
             * their frames through paging_destroy_user_directory().
             */
            spin_unlock_irqrestore(
                &address_space_stack_mapping_lock,
                mapping_flags);

            out_stack->mapped_page_count =
                mapped_pages;

            return false;
        }

        ++mapped_pages;
    }

    spin_unlock_irqrestore(
        &address_space_stack_mapping_lock,
        mapping_flags);

    out_stack->mapped_page_count =
        mapped_pages;

    /*
     * Exactly one MiB / 4096 = 256 pages must exist.
     */
    if (mapped_pages !=
        ADDRESS_SPACE_USER_STACK_SIZE /
            PAGE_SIZE)
    {
        return false;
    }

    /*
     * IMPORTANT:
     *
     * Nothing maps:
     *
     *     [slot.guard_bottom, slot.guard_top)
     *
     * The guard page remains absent by construction.
     */

    return true;
}

/*
 * --------------------------------------------------------------------------
 * USER STACK SLOT HELPERS
 * --------------------------------------------------------------------------
 */

/*
 * Describe the deterministic virtual range belonging to one
 * stack-slot index.
 *
 * Slot layout:
 *
 *     slot 0
 *         stack:
 *             0xBFF00000 - 0xC0000000
 *
 *         guard:
 *             0xBFEFF000 - 0xBFF00000
 *
 *     slot 1
 *         stack begins immediately below slot 0's guard page.
 *
 * No physical mappings are created here.
 */
static bool address_space_describe_user_stack_slot(
    size_t index,
    address_space_user_stack_slot_t *out_slot)
{
    if (out_slot == NULL)
        return false;

    if (index >=
        ADDRESS_SPACE_USER_STACK_SLOT_COUNT)
    {
        return false;
    }

    /*
     * Stack and guard sizes must remain page aligned.
     */
    if ((ADDRESS_SPACE_USER_STACK_SIZE &
         (PAGE_SIZE - 1u)) != 0 ||
        (ADDRESS_SPACE_USER_STACK_GUARD_SIZE &
         (PAGE_SIZE - 1u)) != 0)
    {
        return false;
    }

    uintptr_t slot_span =
        (uintptr_t)
            ADDRESS_SPACE_USER_STACK_SIZE +
        (uintptr_t)
            ADDRESS_SPACE_USER_STACK_GUARD_SIZE;

    /*
     * index is bounded to 0..31, so this multiplication
     * cannot approach uintptr_t overflow with the current
     * layout.
     */
    uintptr_t offset =
        (uintptr_t)index *
        slot_span;

    if (offset >=
        ADDRESS_SPACE_USER_STACK_REGION_TOP)
    {
        return false;
    }

    uintptr_t stack_top =
        ADDRESS_SPACE_USER_STACK_REGION_TOP -
        offset;

    if (stack_top <
        (uintptr_t)
            ADDRESS_SPACE_USER_STACK_SIZE)
    {
        return false;
    }

    uintptr_t stack_bottom =
        stack_top -
        (uintptr_t)
            ADDRESS_SPACE_USER_STACK_SIZE;

    if (stack_bottom <
        (uintptr_t)
            ADDRESS_SPACE_USER_STACK_GUARD_SIZE)
    {
        return false;
    }

    uintptr_t guard_top =
        stack_bottom;

    uintptr_t guard_bottom =
        guard_top -
        (uintptr_t)
            ADDRESS_SPACE_USER_STACK_GUARD_SIZE;

    out_slot->index =
        index;

    out_slot->stack_bottom =
        stack_bottom;

    out_slot->stack_top =
        stack_top;

    out_slot->guard_bottom =
        guard_bottom;

    out_slot->guard_top =
        guard_top;

    return true;
}

/*
 * Return the bitmap bit belonging to one already-validated
 * stack slot.
 */
static uint32_t address_space_user_stack_slot_mask(
    size_t index)
{
    return ((uint32_t)1u << (uint32_t)index);
}

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

/*
 * --------------------------------------------------------------------------
 * USER STACK SLOT MANAGEMENT
 * --------------------------------------------------------------------------
 */

bool address_space_user_stack_slot_reserve_index(
    address_space_t *space,
    size_t index,
    address_space_user_stack_slot_t *out_slot)
{
    if (space == NULL ||
        out_slot == NULL)
    {
        return false;
    }

    /*
     * The canonical kernel address space never contains
     * user-thread stack slots.
     */
    if (space->kernel_space)
        return false;

    if (index >=
        ADDRESS_SPACE_USER_STACK_SLOT_COUNT)
    {
        return false;
    }

    address_space_user_stack_slot_t description;

    if (!address_space_describe_user_stack_slot(
            index,
            &description))
    {
        return false;
    }

    uint32_t mask =
        address_space_user_stack_slot_mask(
            index);

    uint32_t flags =
        spin_lock_irqsave(
            &space->lock);

    /*
     * reference_count == 0 means destruction has begun.
     */
    if (space->reference_count == 0)
    {
        spin_unlock_irqrestore(
            &space->lock,
            flags);

        return false;
    }

    /*
     * A stack slot can have only one owner at a time.
     */
    if ((space->user_stack_slot_bitmap &
         mask) != 0)
    {
        spin_unlock_irqrestore(
            &space->lock,
            flags);

        return false;
    }

    space->user_stack_slot_bitmap |=
        mask;

    /*
     * Publish the descriptor only after reservation succeeds.
     */
    *out_slot =
        description;

    spin_unlock_irqrestore(
        &space->lock,
        flags);

    return true;
}


bool address_space_user_stack_slot_reserve(
    address_space_t *space,
    address_space_user_stack_slot_t *out_slot)
{
    if (space == NULL ||
        out_slot == NULL)
    {
        return false;
    }

    if (space->kernel_space)
        return false;

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

    for (size_t index = 0;
         index <
             ADDRESS_SPACE_USER_STACK_SLOT_COUNT;
         ++index)
    {
        uint32_t mask =
            address_space_user_stack_slot_mask(
                index);

        if ((space->user_stack_slot_bitmap &
             mask) != 0)
        {
            continue;
        }

        address_space_user_stack_slot_t description;

        if (!address_space_describe_user_stack_slot(
                index,
                &description))
        {
            spin_unlock_irqrestore(
                &space->lock,
                flags);

            return false;
        }

        /*
         * Reserve while still holding the address-space lock.
         *
         * Another CPU allocating a thread stack in the same
         * address space therefore cannot choose this slot.
         */
        space->user_stack_slot_bitmap |=
            mask;

        *out_slot =
            description;

        spin_unlock_irqrestore(
            &space->lock,
            flags);

        return true;
    }

    /*
     * All 32 slots are occupied.
     */
    spin_unlock_irqrestore(
        &space->lock,
        flags);

    return false;
}


bool address_space_user_stack_slot_release(
    address_space_t *space,
    size_t index)
{
    if (space == NULL)
        return false;

    if (space->kernel_space)
        return false;

    if (index >=
        ADDRESS_SPACE_USER_STACK_SLOT_COUNT)
    {
        return false;
    }

    uint32_t mask =
        address_space_user_stack_slot_mask(
            index);

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
     * Releasing an already-free slot is an ownership error.
     */
    if ((space->user_stack_slot_bitmap &
         mask) == 0)
    {
        spin_unlock_irqrestore(
            &space->lock,
            flags);

        return false;
    }

    space->user_stack_slot_bitmap &=
        ~mask;

    spin_unlock_irqrestore(
        &space->lock,
        flags);

    return true;
}


size_t address_space_user_stack_slot_reserved_count(
    address_space_t *space)
{
    if (space == NULL)
        return 0;

    if (space->kernel_space)
        return 0;

    uint32_t flags =
        spin_lock_irqsave(
            &space->lock);

    if (space->reference_count == 0)
    {
        spin_unlock_irqrestore(
            &space->lock,
            flags);

        return 0;
    }

    uint32_t bitmap =
        space->user_stack_slot_bitmap;

    size_t count =
        0;

    /*
     * Avoid compiler/runtime dependencies on popcount.
     *
     * Thirty-two iterations are trivial for this diagnostic
     * and bring-up path.
     */
    for (size_t index = 0;
         index <
             ADDRESS_SPACE_USER_STACK_SLOT_COUNT;
         ++index)
    {
        uint32_t mask =
            address_space_user_stack_slot_mask(
                index);

        if ((bitmap & mask) != 0)
            ++count;
    }

    spin_unlock_irqrestore(
        &space->lock,
        flags);

    return count;
}