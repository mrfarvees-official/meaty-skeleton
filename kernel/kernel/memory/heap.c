#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/heap.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>

#define KERNEL_HEAP_START       ((uintptr_t)0xD0000000u)
#define KERNEL_HEAP_LIMIT       ((uintptr_t)0xE0000000u)

#define HEAP_INITIAL_PAGES      4u
#define HEAP_MINIMUM_EXPANSION  4u

#define HEAP_ALIGNMENT          16u
#define HEAP_BLOCK_MAGIC        0x48454150u /* "HEAP" */

/*
 * A split must leave enough room for another block header and a useful
 * aligned allocation.
 */
#define HEAP_MINIMUM_PAYLOAD    HEAP_ALIGNMENT

static heap_block_t* heap_first_block;
static heap_block_t* heap_last_block;

/*
 * First virtual address after the currently mapped heap.
 */
static uintptr_t heap_mapped_end;

static bool heap_initialized;

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

static bool heap_pointer_is_inside(const void* pointer)
{
    uintptr_t address = (uintptr_t)pointer;

    return address >= KERNEL_HEAP_START && address < heap_mapped_end;
}

static heap_block_t* heap_block_from_pointer(void* pointer)
{
    if (pointer == NULL)
        return NULL;

    if (!heap_pointer_is_inside(pointer))
        return NULL;

    uintptr_t pointer_address = (uintptr_t)pointer;

    if (pointer_address < KERNEL_HEAP_START + sizeof(heap_block_t))
        return NULL;

    heap_block_t* block = (heap_block_t*)(pointer_address - sizeof(heap_block_t));

    if (!heap_pointer_is_inside(block))
        return NULL;

    if (block->magic != HEAP_BLOCK_MAGIC)
        return NULL;

    return block;
}

static bool heap_map_pages(uintptr_t start, size_t page_count)
{
    size_t mapped_pages = 0;

    for (; mapped_pages < page_count; ++mapped_pages)
    {
        uintptr_t virtual_address = start + mapped_pages * PAGE_SIZE;

        uintptr_t physical_address = pmm_allocate_frame();

        if (physical_address == 0)
            break;

        if (!paging_map_page(virtual_address, physical_address, PAGE_WRITABLE))
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
    for (size_t i = 0; i < mapped_pages; ++i)
    {
        uintptr_t virtual_address = start + i * PAGE_SIZE;

        paging_unmap_page(virtual_address, true);
    }

    return false;
}

static bool heap_expand(size_t minimum_payload)
{
    /*
     * New space must contain a block header plus the requested payload.
     */
    if (minimum_payload > SIZE_MAX - sizeof(heap_block_t))
        return false;

    size_t required_bytes = sizeof(heap_block_t) + minimum_payload;

    size_t page_count = pages_for_size(required_bytes);

    if (page_count == 0)
        return false;

    if (page_count < HEAP_MINIMUM_EXPANSION)
        page_count = HEAP_MINIMUM_EXPANSION;

    if (page_count > SIZE_MAX / PAGE_SIZE)
        return false;

    size_t expansion_size = page_count * PAGE_SIZE;

    if (heap_mapped_end > KERNEL_HEAP_LIMIT || expansion_size > KERNEL_HEAP_LIMIT - heap_mapped_end)
        return 0;

    uintptr_t expansion_start = heap_mapped_end;

    if (!heap_map_pages(expansion_start, page_count))
        return false;

    /*
     * Clear new pages. This is also important if the PMM later begins
     * recycling frames containing old kernel data.
     */
    memset((void*)expansion_start, 0, expansion_size);

    heap_mapped_end += expansion_size;

    /*
     * If the last block is free, extend it directly.
     */
    if (heap_last_block != NULL && heap_last_block->free)
    {
        heap_last_block->size += expansion_size;
        return true;
    }

    /*
     * Otherwise create a new free block in the new region.
     */
    heap_block_t* block = (heap_block_t*)expansion_start;

    block->size = expansion_size - sizeof(heap_block_t);

    block->previous = heap_last_block;
    block->next = NULL;
    block->magic = HEAP_BLOCK_MAGIC;
    block->free = true;

    if (heap_last_block != NULL)
        heap_last_block->next = block;
    else 
        heap_first_block = block;

    heap_last_block = block;

    return true;
}

static heap_block_t* heap_find_free_block(size_t requested_size)
{
    /*
     * First-fit search.
     */
    for (heap_block_t* block = heap_first_block; block != NULL; block = block->next)
    {
        if (block->free && block->size >= requested_size)
            return block;
    }

    return NULL;
}

static void heap_split_block(heap_block_t* block, size_t requested_size)
{
    /*
     * Do not split unless the remainder can contain another header and
     * at least one useful allocation.
     */
    size_t minimum_remainder = sizeof(heap_block_t) + HEAP_MINIMUM_PAYLOAD;

    if (block->size < requested_size)
        return;

    size_t remaining = block->size - requested_size;

    if (remaining < minimum_remainder)
        return;

    uintptr_t new_block_address = (uintptr_t)(block + 1) + requested_size;

    heap_block_t* new_block = (heap_block_t*)new_block_address;

    new_block->size = remaining - sizeof(heap_block_t);

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

static void heap_merge_with_next(heap_block_t* block)
{
    heap_block_t* next = block->next;

    if (next == NULL || !next->free)
        return;

    /*
     * Since blocks are created by splitting contiguous heap memory,
     * adjacent list entries should also be adjacent in memory.
     */
    uintptr_t expected_next = (uintptr_t)(block + 1) + block->size;

    if ((uintptr_t)next != expected_next)
        return;

    block->size += sizeof(heap_block_t) + next->size;

    block->next = next->next;

    if (block->next != NULL)
        block->next->previous = block;
    else
        heap_last_block = block;
    
    /*
     * Help expose stale-pointer and double-free bugs.
     */
    next->magic = 0;
    next->previous = NULL;
    next->next = NULL;
    next->size = 0;
    next->free = false;
}

void heap_initialize(void)
{
    if (heap_initialized)
        return;

    heap_first_block = NULL;
    heap_last_block = NULL;
    heap_mapped_end = KERNEL_HEAP_START;

    size_t initial_size = HEAP_INITIAL_PAGES * PAGE_SIZE;

    if (!heap_map_pages(KERNEL_HEAP_START, HEAP_INITIAL_PAGES))
    {
        /*
         * Replace this with your panic routine.
         */
        return;
    }

    memset((void*)KERNEL_HEAP_START, 0, initial_size);

    heap_mapped_end = KERNEL_HEAP_START + initial_size;

    heap_first_block = (heap_block_t*)KERNEL_HEAP_START;

    heap_first_block->size = initial_size - sizeof(heap_block_t);

    heap_first_block->previous = NULL;
    heap_first_block->next = NULL;
    heap_first_block->magic = HEAP_BLOCK_MAGIC;
    heap_first_block->free = true;

    heap_last_block = heap_first_block;
    heap_initialized = true;
}

void* kmalloc(size_t size)
{
    if (!heap_initialized)
        return NULL;

    /*
     * malloc(0) behavior is implementation-defined. Returning NULL is
     * simplest for a kernel allocator.
     */
    if (size == 0)
        return NULL;

    size_t aligned_size = align_up_size(size, HEAP_ALIGNMENT);

    if (aligned_size == 0)
        return NULL;
    
    heap_block_t* block = heap_find_free_block(aligned_size);

    if (block == NULL)
    {
        if (!heap_expand(aligned_size))
            return NULL;

        block = heap_find_free_block(aligned_size);

        if (block == NULL)
            return NULL;
    }

    heap_split_block(block, aligned_size);

    block->free = false;

    return (void*)(block + 1);
}

void* kcalloc(size_t count, size_t size)
{
    if (count == 0 || size == 0)
        return NULL;

    if (count > SIZE_MAX / size)
        return NULL;

    size_t total_size = count * size;

    void* pointer = kmalloc(total_size);

    if (pointer == NULL)
        return NULL;

    memset(pointer, 0, total_size);

    return pointer;
}

void kfree(void* pointer)
{
    if (pointer == NULL)
        return;

    heap_block_t* block = heap_block_from_pointer(pointer);

    if (block == NULL)
    {
        /*
         * Invalid pointer. Replace this with a panic or diagnostic
         * message in a debug build.
         */
        return;
    }

    if (block->free)
    {
        /*
         * Double free. Replace with a panic in a debug build.
         */
        return;
    }

    block->free = true;

    /*
     * Merge forward first.
     */
    heap_merge_with_next(block);

    /*
     * Then merge into the previous block if it is free.
     */
    if (block->previous != NULL && block->previous->free)
    {
        block = block->previous;
        heap_merge_with_next(block);
    }
}

void* krealloc(void* pointer, size_t new_size)
{
    if (pointer == NULL)
        return NULL;

    if (new_size == 0)
    {
        kfree(pointer);
        return NULL;
    }

    heap_block_t* block = heap_block_from_pointer(pointer);

    if (block == NULL || block->free)
        return NULL;

    size_t aligned_size = align_up_size(new_size, HEAP_ALIGNMENT);

    if (aligned_size == 0)
        return NULL;

    /*
     * Existing block is already large enough.
     */
    if (block->size >= aligned_size)
    {
        heap_split_block(block, aligned_size);
        return pointer;
    }

    /*
     * Try to grow into the following free block without moving.
     */
    if (block->next != NULL && block->next->free)
    {
        size_t combined_size = block->size + sizeof(heap_block_t) + block->next->size;

        if (combined_size >= aligned_size)
        {
            heap_merge_with_next(block);
            heap_split_block(block, aligned_size);
            block->free = false;

            return pointer;
        }
    }

    /*
     * Allocate a new block and move the data.
     */
    void* replacement = kmalloc(new_size);

    if (replacement == NULL)
        return NULL;

    size_t copy_size = block->size < new_size ? block->size : new_size;

    memcpy(replacement, pointer, copy_size);
    kfree(pointer);

    return replacement;
}