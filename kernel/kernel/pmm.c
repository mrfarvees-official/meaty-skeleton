#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/pmm.h>
#include <kernel/multiboot.h>

#define MAX_PHYSICAL_MEMORY (1024u * 1024u * 1024u)
#define FRAME_COUNT         (MAX_PHYSICAL_MEMORY / PAGE_SIZE)
#define BITMAP_SIZE         (FRAME_COUNT / 8u)

static uint8_t frame_bitmap[BITMAP_SIZE];
static uint64_t total_usable_memory = 0;

extern uint8_t _kernel_start;
extern uint8_t _kernel_end;

static uintptr_t align_down(uintptr_t value)
{
    return value & ~(uintptr_t)(PAGE_SIZE - 1);
}

static uintptr_t align_up(uintptr_t value)
{
    return (value + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
}

static void frame_mark_used(size_t frame)
{
    frame_bitmap[frame / 8] |= (uint8_t)(1u << (frame % 8));
}

static void frame_mark_free(size_t frame)
{
    frame_bitmap[frame / 8] &= (uint8_t)~(1u << (frame % 8));
}

static bool frame_is_used(size_t frame)
{
    return (frame_bitmap[frame / 8] & (uint8_t)(1u << (frame % 8))) != 0;
}

void pmm_reserve_range(uintptr_t base, size_t length)
{
    uintptr_t start = align_down(base);
    uintptr_t end;

    if (length > MAX_PHYSICAL_MEMORY || base > MAX_PHYSICAL_MEMORY - length) 
    {
        end = MAX_PHYSICAL_MEMORY;
    }
    else 
    {
        end = align_up(base + length);
    }

    for (uintptr_t address = start; address < end; address += PAGE_SIZE)
    {
        frame_mark_used(address / PAGE_SIZE);
    }
}

void pmm_release_range(uintptr_t base, size_t length)
{
    uintptr_t start = align_up(base);
    uintptr_t end = align_down(base + length);

    if (end > MAX_PHYSICAL_MEMORY)
        end = MAX_PHYSICAL_MEMORY;

    for (uintptr_t address = start; address < end; address += PAGE_SIZE) 
    {
        frame_mark_free(address / PAGE_SIZE);
    }
}

void pmm_initialize(uint32_t multiboot_info_address)
{
    const struct multiboot_info* mbi = (const struct multiboot_info*)(uintptr_t)multiboot_info_address;
    
    memset(frame_bitmap, 0xFF, sizeof(frame_bitmap));

    total_usable_memory = 0;

    if ((mbi->flags & MULTIBOOT_INFO_MEMORY_MAP) == 0)
        return;

    uintptr_t current = mbi->mmap_addr;
    uintptr_t end = mbi->mmap_addr + mbi->mmap_lengh;

    while (current < end) 
    {
        const struct multiboot_mmap_entry* entry = (const struct multiboot_mmap_entry*)current;

        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) 
        {
            uint64_t region_base = entry->address;
            uint64_t region_end = entry->address + entry->length;

            if (region_base < MAX_PHYSICAL_MEMORY)
            {
                if (region_end > MAX_PHYSICAL_MEMORY) 
                    region_end = MAX_PHYSICAL_MEMORY;

                uint64_t usable_size = region_end - region_base;

                total_usable_memory += usable_size;

                pmm_release_range((uintptr_t)region_base, (size_t)(region_end - region_base));
            }
        }

        current += entry->size + sizeof(entry->size);
    }

    pmm_reserve_range(0, 0x100000);

    pmm_reserve_range((uintptr_t)&_kernel_start, (uintptr_t)&_kernel_end - (uintptr_t)&_kernel_start);

    pmm_reserve_range(multiboot_info_address, sizeof(struct multiboot_info));

    pmm_reserve_range(mbi->mmap_addr, mbi->mmap_lengh);
}

uintptr_t pmm_allocate_frame(void)
{
    for (size_t frame = 1; frame < FRAME_COUNT; ++frame)
    {
        if (!frame_is_used(frame))
        {
            frame_mark_used(frame);
            return frame * PAGE_SIZE;
        }
    }

    return 0;
}

void pmm_free_frame(uintptr_t address)
{
    if ((address & (PAGE_SIZE - 1)) != 0) 
        return;

    if (address == 0 || address >= MAX_PHYSICAL_MEMORY)
        return;

    frame_mark_free(address / PAGE_SIZE);
}

size_t pmm_get_free_frame_count(void)
{
    size_t free_frames = 0;

    for (size_t frame = 0; frame < FRAME_COUNT; ++frame)
    {
        if (!frame_is_used(frame))
            ++free_frames;
    }

    return free_frames;
}

uint64_t pmm_get_free_memory(void)
{
    return (uint64_t)pmm_get_free_frame_count() * PAGE_SIZE;
}

uint64_t pmm_get_usable_memory(void)
{
    return total_usable_memory;
}
