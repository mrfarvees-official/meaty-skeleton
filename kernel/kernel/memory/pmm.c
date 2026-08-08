#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/pmm.h>
#include <kernel/multiboot.h>
#include <kernel/spinlock.h>

#define MAX_PHYSICAL_MEMORY (1024u * 1024u * 1024u)

#define FRAME_COUNT \
    (MAX_PHYSICAL_MEMORY / PAGE_SIZE)

#define BITMAP_SIZE \
    (FRAME_COUNT / 8u)

static uint8_t frame_bitmap[BITMAP_SIZE];

static uint64_t total_usable_memory = 0;

static spinlock_t pmm_lock =
    SPINLOCK_INITIALIZER;

extern uint8_t _kernel_start;
extern uint8_t _kernel_end;


static uintptr_t align_down(uintptr_t value)
{
    return value &
        ~(uintptr_t)(PAGE_SIZE - 1u);
}


static uintptr_t align_up(uintptr_t value)
{
    return
        (value + PAGE_SIZE - 1u) &
        ~(uintptr_t)(PAGE_SIZE - 1u);
}


static void frame_mark_used(size_t frame)
{
    frame_bitmap[frame / 8u] |=
        (uint8_t)(1u << (frame % 8u));
}


static void frame_mark_free(size_t frame)
{
    frame_bitmap[frame / 8u] &=
        (uint8_t)~(1u << (frame % 8u));
}


static bool frame_is_used(size_t frame)
{
    return
        (frame_bitmap[frame / 8u] &
         (uint8_t)(1u << (frame % 8u))) != 0;
}


/*
 * Internal version.
 *
 * Caller is responsible for synchronization.
 */
static void pmm_reserve_range_unlocked(
    uintptr_t base,
    size_t length)
{
    if (length == 0)
        return;

    if (base >= MAX_PHYSICAL_MEMORY)
        return;

    uintptr_t start =
        align_down(base);

    uintptr_t end;

    if (length > MAX_PHYSICAL_MEMORY ||
        base > MAX_PHYSICAL_MEMORY - length)
    {
        end = MAX_PHYSICAL_MEMORY;
    }
    else
    {
        end = align_up(base + length);

        if (end > MAX_PHYSICAL_MEMORY)
            end = MAX_PHYSICAL_MEMORY;
    }

    for (uintptr_t address = start;
         address < end;
         address += PAGE_SIZE)
    {
        size_t frame =
            (size_t)(address / PAGE_SIZE);

        frame_mark_used(frame);
    }
}


/*
 * Internal version.
 *
 * Caller is responsible for synchronization.
 */
static void pmm_release_range_unlocked(
    uintptr_t base,
    size_t length)
{
    if (length == 0)
        return;

    if (base >= MAX_PHYSICAL_MEMORY)
        return;

    uintptr_t start =
        align_up(base);

    uintptr_t end;

    if (length > MAX_PHYSICAL_MEMORY ||
        base > MAX_PHYSICAL_MEMORY - length)
    {
        end = MAX_PHYSICAL_MEMORY;
    }
    else
    {
        end = align_down(base + length);

        if (end > MAX_PHYSICAL_MEMORY)
            end = MAX_PHYSICAL_MEMORY;
    }

    if (start >= end)
        return;

    for (uintptr_t address = start;
         address < end;
         address += PAGE_SIZE)
    {
        size_t frame =
            (size_t)(address / PAGE_SIZE);

        frame_mark_free(frame);
    }
}


void pmm_reserve_range(
    uintptr_t base,
    size_t length)
{
    uint32_t flags =
        spin_lock_irqsave(&pmm_lock);

    pmm_reserve_range_unlocked(
        base,
        length
    );

    spin_unlock_irqrestore(
        &pmm_lock,
        flags
    );
}


void pmm_release_range(
    uintptr_t base,
    size_t length)
{
    uint32_t flags =
        spin_lock_irqsave(&pmm_lock);

    pmm_release_range_unlocked(
        base,
        length
    );

    spin_unlock_irqrestore(
        &pmm_lock,
        flags
    );
}


void pmm_initialize(
    uint32_t multiboot_info_address)
{
    const struct multiboot_info *mbi =
        (const struct multiboot_info *)
        (uintptr_t)multiboot_info_address;

    spinlock_initialize(&pmm_lock);

    /*
     * Start with every frame reserved.
     */
    memset(
        frame_bitmap,
        0xFF,
        sizeof(frame_bitmap)
    );

    total_usable_memory = 0;

    if ((mbi->flags &
         MULTIBOOT_INFO_MEMORY_MAP) == 0)
    {
        return;
    }

    uintptr_t current =
        mbi->mmap_addr;

    uintptr_t end =
        mbi->mmap_addr +
        mbi->mmap_lengh;

    while (current < end)
    {
        const struct multiboot_mmap_entry *entry =
            (const struct multiboot_mmap_entry *)
            current;

        if (entry->type ==
            MULTIBOOT_MEMORY_AVAILABLE)
        {
            uint64_t region_base =
                entry->address;

            uint64_t region_end =
                entry->address +
                entry->length;

            if (region_base <
                MAX_PHYSICAL_MEMORY)
            {
                if (region_end >
                    MAX_PHYSICAL_MEMORY)
                {
                    region_end =
                        MAX_PHYSICAL_MEMORY;
                }

                if (region_end >
                    region_base)
                {
                    total_usable_memory +=
                        region_end -
                        region_base;

                    /*
                     * No lock needed during boot.
                     *
                     * Scheduler is not running yet.
                     */
                    pmm_release_range_unlocked(
                        (uintptr_t)region_base,
                        (size_t)(
                            region_end -
                            region_base
                        )
                    );
                }
            }
        }

        current +=
            entry->size +
            sizeof(entry->size);
    }

    /*
     * Reserve first MiB.
     */
    pmm_reserve_range_unlocked(
        0,
        0x100000
    );

    /*
     * Reserve kernel image.
     */
    pmm_reserve_range_unlocked(
        (uintptr_t)&_kernel_start,
        (uintptr_t)&_kernel_end -
        (uintptr_t)&_kernel_start
    );

    /*
     * Reserve Multiboot information.
     */
    pmm_reserve_range_unlocked(
        multiboot_info_address,
        sizeof(struct multiboot_info)
    );

    /*
     * Reserve Multiboot memory map.
     */
    pmm_reserve_range_unlocked(
        mbi->mmap_addr,
        mbi->mmap_lengh
    );
}


uintptr_t pmm_allocate_frame(void)
{
    uint32_t flags =
        spin_lock_irqsave(&pmm_lock);

    for (size_t frame = 1;
         frame < FRAME_COUNT;
         ++frame)
    {
        if (!frame_is_used(frame))
        {
            frame_mark_used(frame);

            uintptr_t address =
                (uintptr_t)frame *
                PAGE_SIZE;

            spin_unlock_irqrestore(
                &pmm_lock,
                flags
            );

            return address;
        }
    }

    spin_unlock_irqrestore(
        &pmm_lock,
        flags
    );

    return 0;
}


void pmm_free_frame(uintptr_t address)
{
    if ((address &
         (PAGE_SIZE - 1u)) != 0)
    {
        return;
    }

    if (address == 0 ||
        address >= MAX_PHYSICAL_MEMORY)
    {
        return;
    }

    uint32_t flags =
        spin_lock_irqsave(&pmm_lock);

    frame_mark_free(
        (size_t)(address / PAGE_SIZE)
    );

    spin_unlock_irqrestore(
        &pmm_lock,
        flags
    );
}


size_t pmm_get_free_frame_count(void)
{
    uint32_t flags =
        spin_lock_irqsave(&pmm_lock);

    size_t free_frames = 0;

    for (size_t frame = 0;
         frame < FRAME_COUNT;
         ++frame)
    {
        if (!frame_is_used(frame))
            ++free_frames;
    }

    spin_unlock_irqrestore(
        &pmm_lock,
        flags
    );

    return free_frames;
}


uint64_t pmm_get_free_memory(void)
{
    return
        (uint64_t)pmm_get_free_frame_count() *
        PAGE_SIZE;
}


uint64_t pmm_get_usable_memory(void)
{
    return total_usable_memory;
}