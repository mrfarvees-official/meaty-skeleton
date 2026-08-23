#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/heap.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>
#include <kernel/spinlock.h>

#define KERNEL_HEAP_START ((uintptr_t)0xD0000000u)
#define KERNEL_HEAP_LIMIT ((uintptr_t)0xE0000000u)

#define HEAP_INITIAL_PAGES 4u
#define HEAP_MINIMUM_EXPANSION 4u

#define HEAP_ALIGNMENT 16u
#define HEAP_BLOCK_MAGIC 0x48454150u /* "HEAP" */

/*
 * A split must leave enough room for another block header and a useful
 * aligned allocation.
 */
#define HEAP_MINIMUM_PAYLOAD HEAP_ALIGNMENT

/*
 * MEMORY LOCK ORDER:
 *
 *     heap_lock
 *         ->
 *     paging_lock
 *         ->
 *     pmm_lock
 *
 * The heap may call paging and PMM.
 *
 * Paging and PMM must never call kmalloc(), kcalloc(), krealloc(),
 * or kfree() while holding their locks.
 *
 * Never yield, sleep, or explicitly schedule while heap_lock is held.
 */

/*
 * Heap metadata.
 */
static heap_block_t *heap_first_block;
static heap_block_t *heap_last_block;

/*
 * First virtual address after the currently mapped heap.
 */
static uintptr_t heap_mapped_end;

static bool heap_initialized;

/*
 * Protects:
 *
 *     heap_first_block
 *     heap_last_block
 *     heap_mapped_end
 *     heap block metadata
 *
 * Since this is currently a single-core preemptive kernel, the
 * IRQ-saving spinlock also prevents timer-driven preemption while
 * heap metadata is being modified.
 */
static spinlock_t heap_lock = SPINLOCK_INITIALIZER;

/*
 * --------------------------------------------------------------------------
 * BASIC HELPERS
 * --------------------------------------------------------------------------
 */

static size_t align_up_size(size_t value, size_t alignment)
{
    size_t mask = alignment - 1u;

    if (value > SIZE_MAX - mask)
        return 0;

    return (value + mask) & ~mask;
}

static size_t pages_for_size(size_t size)
{
    if (size > SIZE_MAX - (PAGE_SIZE - 1u))
        return 0;

    return (size + PAGE_SIZE - 1u) / PAGE_SIZE;
}

/*
 * Caller must hold heap_lock when the scheduler is active.
 */
static bool heap_pointer_is_inside(const void *pointer)
{
    uintptr_t address = (uintptr_t)pointer;

    return address >= KERNEL_HEAP_START &&
           address < heap_mapped_end;
}

/*
 * Caller must hold heap_lock.
 */
static heap_block_t *heap_block_from_pointer(void *pointer)
{
    if (pointer == NULL)
        return NULL;

    if (!heap_pointer_is_inside(pointer))
        return NULL;

    uintptr_t pointer_address =
        (uintptr_t)pointer;

    if (pointer_address <
        KERNEL_HEAP_START + sizeof(heap_block_t))
    {
        return NULL;
    }

    heap_block_t *block =
        (heap_block_t *)(pointer_address -
                         sizeof(heap_block_t));

    if (!heap_pointer_is_inside(block))
        return NULL;

    if (block->magic != HEAP_BLOCK_MAGIC)
        return NULL;

    return block;
}

/*
 * --------------------------------------------------------------------------
 * PAGE MAPPING
 * --------------------------------------------------------------------------
 *
 * Caller must hold heap_lock.
 *
 * These functions may call paging and PMM, giving the dependency:
 *
 *     heap_lock -> paging_lock -> pmm_lock
 */

static bool heap_map_pages(
    uintptr_t start,
    size_t page_count)
{
    size_t mapped_pages = 0;

    for (; mapped_pages < page_count; ++mapped_pages)
    {
        uintptr_t virtual_address =
            start +
            mapped_pages * PAGE_SIZE;

        /*
         * pmm_allocate_frame() takes pmm_lock internally.
         *
         * heap_lock -> pmm_lock
         */
        uintptr_t physical_address =
            pmm_allocate_frame();

        if (physical_address == 0)
            break;

        /*
         * paging_map_page() takes paging_lock internally.
         *
         * It may also take pmm_lock if it needs to create
         * another page table.
         *
         * heap_lock -> paging_lock -> pmm_lock
         */
        if (!paging_map_page(
                virtual_address,
                physical_address,
                PAGE_WRITABLE))
        {
            pmm_free_frame(physical_address);
            break;
        }
    }

    if (mapped_pages == page_count)
        return true;

    /*
     * Roll back pages mapped by this operation.
     */
    for (size_t i = 0;
         i < mapped_pages;
         ++i)
    {
        uintptr_t virtual_address =
            start + i * PAGE_SIZE;

        /*
         * paging_unmap_page(..., true) releases both
         * the mapping and its physical frame.
         */
        paging_unmap_page(
            virtual_address,
            true);
    }

    return false;
}

/*
 * Caller must hold heap_lock.
 */
static bool heap_expand(size_t minimum_payload)
{
    /*
     * New space must contain a block header plus requested payload.
     */
    if (minimum_payload >
        SIZE_MAX - sizeof(heap_block_t))
    {
        return false;
    }

    size_t required_bytes =
        sizeof(heap_block_t) +
        minimum_payload;

    size_t page_count =
        pages_for_size(required_bytes);

    if (page_count == 0)
        return false;

    if (page_count < HEAP_MINIMUM_EXPANSION)
        page_count = HEAP_MINIMUM_EXPANSION;

    if (page_count > SIZE_MAX / PAGE_SIZE)
        return false;

    size_t expansion_size =
        page_count * PAGE_SIZE;

    if (heap_mapped_end > KERNEL_HEAP_LIMIT)
        return false;

    if (expansion_size >
        KERNEL_HEAP_LIMIT - heap_mapped_end)
    {
        return false;
    }

    uintptr_t expansion_start =
        heap_mapped_end;

    if (!heap_map_pages(
            expansion_start,
            page_count))
    {
        return false;
    }

    /*
     * Clear newly mapped heap memory.
     */
    memset(
        (void *)expansion_start,
        0,
        expansion_size);

    heap_mapped_end += expansion_size;

    /*
     * If the final block was already free, the new virtual memory
     * directly follows that block. Extend it rather than creating
     * another header.
     */
    if (heap_last_block != NULL &&
        heap_last_block->free)
    {
        heap_last_block->size +=
            expansion_size;

        return true;
    }

    /*
     * Otherwise create a new free block.
     */
    heap_block_t *block =
        (heap_block_t *)expansion_start;

    block->size =
        expansion_size -
        sizeof(heap_block_t);

    block->previous =
        heap_last_block;

    block->next = NULL;

    block->magic =
        HEAP_BLOCK_MAGIC;

    block->free = true;

    if (heap_last_block != NULL)
        heap_last_block->next = block;
    else
        heap_first_block = block;

    heap_last_block = block;

    return true;
}

/*
 * --------------------------------------------------------------------------
 * BLOCK LIST HELPERS
 * --------------------------------------------------------------------------
 *
 * All functions below assume heap_lock is already held.
 */

static heap_block_t *heap_find_free_block(
    size_t requested_size)
{
    /*
     * First-fit allocation.
     */
    for (heap_block_t *block = heap_first_block;
         block != NULL;
         block = block->next)
    {
        if (block->free &&
            block->size >= requested_size)
        {
            return block;
        }
    }

    return NULL;
}

static void heap_split_block(
    heap_block_t *block,
    size_t requested_size)
{
    size_t minimum_remainder =
        sizeof(heap_block_t) +
        HEAP_MINIMUM_PAYLOAD;

    if (block->size < requested_size)
        return;

    size_t remaining =
        block->size -
        requested_size;

    if (remaining < minimum_remainder)
        return;

    uintptr_t new_block_address =
        (uintptr_t)(block + 1) +
        requested_size;

    heap_block_t *new_block =
        (heap_block_t *)new_block_address;

    new_block->size =
        remaining -
        sizeof(heap_block_t);

    new_block->previous = block;
    new_block->next = block->next;
    new_block->magic = HEAP_BLOCK_MAGIC;
    new_block->free = true;

    if (new_block->next != NULL)
        new_block->next->previous = new_block;
    else
        heap_last_block = new_block;

    block->size = requested_size;
    block->next = new_block;
}

static void heap_merge_with_next(
    heap_block_t *block)
{
    heap_block_t *next =
        block->next;

    if (next == NULL ||
        !next->free)
    {
        return;
    }

    /*
     * Adjacent list entries should also be contiguous in memory.
     */
    uintptr_t expected_next =
        (uintptr_t)(block + 1) +
        block->size;

    if ((uintptr_t)next != expected_next)
        return;

    block->size +=
        sizeof(heap_block_t) +
        next->size;

    block->next =
        next->next;

    if (block->next != NULL)
    {
        block->next->previous = block;
    }
    else
    {
        heap_last_block = block;
    }

    /*
     * Poison removed block metadata to expose stale-header usage.
     */
    next->magic = 0;
    next->previous = NULL;
    next->next = NULL;
    next->size = 0;
    next->free = false;
}

/*
 * --------------------------------------------------------------------------
 * PRIVATE UNLOCKED ALLOCATION OPERATIONS
 * --------------------------------------------------------------------------
 *
 * These functions MUST only be called while heap_lock is already held.
 *
 * They deliberately do not acquire heap_lock themselves.
 *
 * This prevents recursive-lock deadlocks in krealloc().
 */

static void *heap_allocate_unlocked(size_t size)
{
    if (!heap_initialized)
        return NULL;

    if (size == 0)
        return NULL;

    size_t aligned_size =
        align_up_size(
            size,
            HEAP_ALIGNMENT);

    if (aligned_size == 0)
        return NULL;

    heap_block_t *block =
        heap_find_free_block(
            aligned_size);

    if (block == NULL)
    {
        if (!heap_expand(aligned_size))
            return NULL;

        block =
            heap_find_free_block(
                aligned_size);

        if (block == NULL)
            return NULL;
    }

    heap_split_block(
        block,
        aligned_size);

    block->free = false;

    return (void *)(block + 1);
}

static void heap_free_unlocked(void *pointer)
{
    if (pointer == NULL)
        return;

    heap_block_t *block =
        heap_block_from_pointer(pointer);

    if (block == NULL)
    {
        /*
         * Invalid pointer.
         *
         * A debug kernel could panic here later.
         */
        return;
    }

    if (block->free)
    {
        /*
         * Double free.
         *
         * A debug kernel could panic here later.
         */
        return;
    }

    block->free = true;

    /*
     * Merge forward first.
     */
    heap_merge_with_next(block);

    /*
     * Then merge into previous block when possible.
     */
    if (block->previous != NULL &&
        block->previous->free)
    {
        block = block->previous;

        heap_merge_with_next(block);
    }
}

static void *heap_reallocate_unlocked(
    void *pointer,
    size_t new_size)
{
    if (pointer == NULL)
    {
        /*
         * Kernel realloc semantics:
         *
         * realloc(NULL, size) behaves like malloc(size).
         */
        return heap_allocate_unlocked(
            new_size);
    }

    if (new_size == 0)
    {
        /*
         * realloc(ptr, 0) frees ptr.
         */
        heap_free_unlocked(pointer);

        return NULL;
    }

    heap_block_t *block =
        heap_block_from_pointer(pointer);

    if (block == NULL ||
        block->free)
    {
        return NULL;
    }

    size_t aligned_size =
        align_up_size(
            new_size,
            HEAP_ALIGNMENT);

    if (aligned_size == 0)
        return NULL;

    /*
     * Existing block is already large enough.
     */
    if (block->size >= aligned_size)
    {
        heap_split_block(
            block,
            aligned_size);

        return pointer;
    }

    /*
     * Try to grow into a following free block without moving.
     */
    if (block->next != NULL &&
        block->next->free)
    {
        size_t combined_size =
            block->size +
            sizeof(heap_block_t) +
            block->next->size;

        if (combined_size >= aligned_size)
        {
            heap_merge_with_next(block);

            heap_split_block(
                block,
                aligned_size);

            block->free = false;

            return pointer;
        }
    }

    /*
     * Allocate another block WITHOUT trying to reacquire heap_lock.
     */
    void *replacement =
        heap_allocate_unlocked(
            new_size);

    if (replacement == NULL)
        return NULL;

    size_t copy_size =
        block->size < new_size
            ? block->size
            : new_size;

    memcpy(
        replacement,
        pointer,
        copy_size);

    /*
     * Free old block WITHOUT reacquiring heap_lock.
     */
    heap_free_unlocked(pointer);

    return replacement;
}

/*
 * --------------------------------------------------------------------------
 * INITIALIZATION
 * --------------------------------------------------------------------------
 */

void heap_initialize(void)
{
    /*
     * Heap initialization happens during boot before normal concurrent
     * kernel tasks are active.
     */
    if (heap_initialized)
        return;

    spinlock_initialize(
        &heap_lock);

    heap_first_block =
        NULL;

    heap_last_block =
        NULL;

    heap_mapped_end =
        KERNEL_HEAP_START;

    /*
     * ------------------------------------------------------------
     * Establish stable kernel-heap PDEs
     * ------------------------------------------------------------
     *
     * The kernel heap occupies:
     *
     *     0xD0000000 .. 0xDFFFFFFF
     *
     * User page directories inherit the kernel's supervisor PDEs when
     * they are created.
     *
     * The heap itself grows lazily. Without pre-creating these page
     * tables, crossing a later 4 MiB boundary would create a brand-new
     * kernel PDE after existing user address spaces had already been
     * created.
     *
     * Those older address spaces would then lack the new kernel PDE.
     *
     * Pre-create one supervisor-only page table for every 4 MiB heap
     * region now. Only page-table frames are allocated here; actual
     * heap data pages remain demand-mapped by heap_map_pages().
     */

    const uintptr_t page_directory_span =
        (uintptr_t)PAGE_SIZE *
        1024u;

    for (uintptr_t address =
             KERNEL_HEAP_START;
         address <
         KERNEL_HEAP_LIMIT;
         address +=
         page_directory_span)
    {
        if (!paging_ensure_kernel_page_table(
                address))
        {
            /*
             * Heap initialization cannot safely continue if its
             * supervisor page-table topology could later diverge
             * between kernel and userspace address spaces.
             *
             * heap_initialize() currently has a void API, so preserve
             * its existing failure convention and leave the heap
             * uninitialized.
             */
            return;
        }
    }

    /*
     * ------------------------------------------------------------
     * Map initial heap data pages
     * ------------------------------------------------------------
     *
     * These PTEs are now inserted beneath page tables whose PDEs are
     * stable for the lifetime of the kernel.
     */

    size_t initial_size =
        HEAP_INITIAL_PAGES *
        PAGE_SIZE;

    if (!heap_map_pages(
            KERNEL_HEAP_START,
            HEAP_INITIAL_PAGES))
    {
        /*
         * Replace with the kernel panic routine later.
         */
        return;
    }

    memset(
        (void *)KERNEL_HEAP_START,
        0,
        initial_size);

    heap_mapped_end =
        KERNEL_HEAP_START +
        initial_size;

    heap_first_block =
        (heap_block_t *)
            KERNEL_HEAP_START;

    heap_first_block->size =
        initial_size -
        sizeof(heap_block_t);

    heap_first_block->previous =
        NULL;

    heap_first_block->next =
        NULL;

    heap_first_block->magic =
        HEAP_BLOCK_MAGIC;

    heap_first_block->free =
        true;

    heap_last_block =
        heap_first_block;

    heap_initialized =
        true;
}

/*
 * --------------------------------------------------------------------------
 * PUBLIC SYNCHRONIZED API
 * --------------------------------------------------------------------------
 */

void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;

    uint32_t flags =
        spin_lock_irqsave(
            &heap_lock);

    void *pointer =
        heap_allocate_unlocked(
            size);

    spin_unlock_irqrestore(
        &heap_lock,
        flags);

    return pointer;
}

void *kcalloc(
    size_t count,
    size_t size)
{
    if (count == 0 ||
        size == 0)
    {
        return NULL;
    }

    if (count > SIZE_MAX / size)
        return NULL;

    size_t total_size =
        count * size;

    /*
     * kmalloc() owns heap synchronization.
     */
    void *pointer =
        kmalloc(total_size);

    if (pointer == NULL)
        return NULL;

    /*
     * The newly allocated block belongs exclusively to this caller,
     * so heap_lock does not need to remain held during memset().
     */
    memset(
        pointer,
        0,
        total_size);

    return pointer;
}

void *krealloc(
    void *pointer,
    size_t new_size)
{
    /*
     * Everything inside heap_reallocate_unlocked() operates under
     * this single acquisition.
     *
     * It must never call the public kmalloc()/kfree() functions.
     */
    uint32_t flags =
        spin_lock_irqsave(
            &heap_lock);

    void *result =
        heap_reallocate_unlocked(
            pointer,
            new_size);

    spin_unlock_irqrestore(
        &heap_lock,
        flags);

    return result;
}

void kfree(void *pointer)
{
    if (pointer == NULL)
        return;

    uint32_t flags =
        spin_lock_irqsave(
            &heap_lock);

    heap_free_unlocked(pointer);

    spin_unlock_irqrestore(
        &heap_lock,
        flags);
}