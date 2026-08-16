#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/spinlock.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>

#define PAGE_DIRECTORY_ENTRIES 1024u
#define PAGE_TABLE_ENTRIES 1024u

/*
 * The final page-directory entry points back to the page directory.
 *
 * This produces:
 *
 * 0xFFC00000 - 0xFFFFEFFF:
 *     All page tables
 *
 * 0xFFFFF000 - 0xFFFFFFFF:
 *     The page directory itself
 */
#define RECURSIVE_DIRECTORY_INDEX 1023u
#define RECURSIVE_TABLES_BASE 0xFFC00000u
#define RECURSIVE_DIRECTORY_BASE 0xFFFFF000u

/*
 * Temporary kernel-only mapping used to initialize page-directory
 * frames that are not otherwise virtually mapped.
 *
 * Keep this outside the kernel heap and below the recursive region.
 */
#define PAGE_DIRECTORY_SCRATCH_ADDRESS 0xE0000000u

#define PAGE_DIRECTORY_SCRATCH_INDEX \
    ((size_t)(PAGE_DIRECTORY_SCRATCH_ADDRESS >> 22))

/*
 * Bootstrap paging structures.
 *
 * These must be located in the identity-mapped portion of memory before
 * paging is enabled.
 */
static uint32_t page_directory[PAGE_DIRECTORY_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

static uint32_t first_page_table[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

static spinlock_t paging_lock = SPINLOCK_INITIALIZER;

/*
 * Physical frame of the bootstrap/kernel address space.
 *
 * Unlike paging_current_directory(), this value does not change when
 * a task switches to another CR3.
 */
static uintptr_t kernel_directory_physical;

static bool is_page_aligned(uintptr_t address)
{
    return (address & (PAGE_SIZE - 1u)) == 0;
}

static size_t page_directory_index(uintptr_t virtual_address)
{
    return (size_t)((virtual_address >> 22) & 0x3FFu);
}

static size_t page_table_index(uintptr_t virtual_address)
{
    return (size_t)((virtual_address >> 12) & 0x3FFu);
}

static uint32_t *recursive_page_directory(void)
{
    return (uint32_t *)RECURSIVE_DIRECTORY_BASE;
}

static uint32_t *recursive_page_table(size_t directory_index)
{
    return (uint32_t *)(RECURSIVE_TABLES_BASE +
                        directory_index * PAGE_SIZE);
}

static void load_page_directory(
    uintptr_t physical_address)
{
    __asm__ volatile(
        "movl %0, %%cr3"
        :
        : "r"((uint32_t)physical_address)
        : "memory");
}

uintptr_t paging_current_directory(void)
{
    uint32_t cr3;

    __asm__ volatile(
        "movl %%cr3, %0"
        : "=r"(cr3));

    return (uintptr_t)(cr3 & PAGE_FRAME);
}

uintptr_t paging_kernel_directory(void)
{
    return kernel_directory_physical;
}

bool paging_switch_directory(
    uintptr_t directory_physical)
{
    if (!is_page_aligned(
            directory_physical))
    {
        return false;
    }

    if (directory_physical == 0)
        return false;

    load_page_directory(
        directory_physical);

    return true;
}

static void reload_page_directory(void)
{
    uint32_t cr3;

    __asm__ volatile(
        "movl %%cr3, %0"
        : "=r"(cr3));

    __asm__ volatile(
        "movl %0, %%cr3"
        :
        : "r"(cr3)
        : "memory");
}

static void enable_paging(void)
{
    uint32_t cr0;

    __asm__ volatile(
        "movl %%cr0, %0"
        : "=r"(cr0));

    cr0 |= (1u << 31);

    __asm__ volatile(
        "movl %0, %%cr0"
        :
        : "r"(cr0)
        : "memory");
}

static void invalidate_page(uintptr_t virtual_address)
{
    __asm__ volatile(
        "invlpg (%0)"
        :
        : "r"(virtual_address)
        : "memory");
}

void paging_initialize(void)
{
    spinlock_initialize(&paging_lock);

    memset(page_directory, 0, sizeof(page_directory));
    memset(first_page_table, 0, sizeof(first_page_table));

    /*
     * Identity-map the first 4 MiB:
     *
     * virtual 0x00000000 -> physical 0x00000000
     * virtual 0x00001000 -> physical 0x00001000
     * ...
     * virtual 0x003FF000 -> physical 0x003FF000
     *
     * This should contain the kernel, stack, GDT, IDT and bootstrap
     * paging structures in a normal low-memory meaty-skeleton setup.
     */
    for (size_t i = 0; i < PAGE_TABLE_ENTRIES; ++i)
    {
        uintptr_t physical_address = i * PAGE_SIZE;

        first_page_table[i] =
            (uint32_t)(physical_address & PAGE_FRAME) |
            PAGE_PRESENT |
            PAGE_WRITABLE;
    }

    uintptr_t first_table_physical =
        (uintptr_t)first_page_table;

    uintptr_t directory_physical =
        (uintptr_t)page_directory;

    kernel_directory_physical =
        directory_physical;

    /*
     * Directory entry 0 controls virtual addresses 0-4 MiB.
     */
    page_directory[0] =
        (uint32_t)(first_table_physical & PAGE_FRAME) |
        PAGE_PRESENT |
        PAGE_WRITABLE;

    /*
     * Recursive mapping.
     *
     * The final directory entry points back to the directory itself.
     */
    page_directory[RECURSIVE_DIRECTORY_INDEX] =
        (uint32_t)(directory_physical & PAGE_FRAME) |
        PAGE_PRESENT |
        PAGE_WRITABLE;

    /*
     * Interrupt 14 must already have a valid page-fault handler before
     * enabling paging.
     */
    load_page_directory(directory_physical);
    enable_paging();

    /*
     * Ensure the processor uses the completed paging structures.
     */
    reload_page_directory();
}

bool paging_create_user_directory(
    uintptr_t *directory_physical)
{
    if (directory_physical == NULL)
        return false;

    uintptr_t frame =
        pmm_allocate_frame();

    if (frame == 0)
        return false;

    /*
     * Temporarily map the physical directory frame into the current
     * address space so it can be initialized.
     */
    if (!paging_map_page(
            PAGE_DIRECTORY_SCRATCH_ADDRESS,
            frame,
            PAGE_WRITABLE))
    {
        pmm_free_frame(frame);
        return false;
    }

    uint32_t *new_directory =
        (uint32_t *)
            PAGE_DIRECTORY_SCRATCH_ADDRESS;

    memset(
        new_directory,
        0,
        PAGE_SIZE);

    uint32_t *current_directory =
        recursive_page_directory();

    /*
     * Share all existing supervisor mappings.
     *
     * PAGE_USER PDEs represent user-visible regions and are
     * intentionally omitted, giving the new address space an empty
     * userspace namespace.
     *
     * Also omit the temporary scratch PDE itself.
     */
    for (size_t i = 0;
         i < PAGE_DIRECTORY_ENTRIES;
         ++i)
    {
        if (i ==
            RECURSIVE_DIRECTORY_INDEX)
        {
            continue;
        }

        if (i ==
            PAGE_DIRECTORY_SCRATCH_INDEX)
        {
            continue;
        }

        uint32_t entry =
            current_directory[i];

        if ((entry & PAGE_PRESENT) == 0)
            continue;

        if ((entry & PAGE_USER) != 0)
            continue;

        new_directory[i] =
            entry;
    }

    /*
     * Recursive mapping must point at this directory's own physical
     * frame, not the current address space's directory.
     */
    new_directory[RECURSIVE_DIRECTORY_INDEX] =
        (uint32_t)(frame & PAGE_FRAME) |
        PAGE_PRESENT |
        PAGE_WRITABLE;

    /*
     * Remove only the temporary virtual alias.
     *
     * Do not free the directory frame.
     */
    if (!paging_unmap_page(
            PAGE_DIRECTORY_SCRATCH_ADDRESS,
            false))
    {
        pmm_free_frame(frame);
        return false;
    }

    *directory_physical =
        frame;

    return true;
}

void paging_destroy_user_directory(
    uintptr_t directory_physical)
{
    if (directory_physical == 0)
        return;

    if (!is_page_aligned(
            directory_physical))
    {
        return;
    }

    /*
     * U3a creates no private page tables in these directories yet.
     *
     * Shared supervisor page tables belong to the kernel address
     * space and must not be freed here.
     */
    pmm_free_frame(
        directory_physical);
}

bool paging_map_page(
    uintptr_t virtual_address,
    uintptr_t physical_address,
    uint32_t flags)
{
    if (!is_page_aligned(virtual_address))
        return false;

    if (!is_page_aligned(physical_address))
        return false;

    size_t directory_index =
        page_directory_index(virtual_address);

    size_t table_index =
        page_table_index(virtual_address);

    if (directory_index == RECURSIVE_DIRECTORY_INDEX)
        return false;

    uint32_t irq_flags =
        spin_lock_irqsave(&paging_lock);

    uint32_t *directory =
        recursive_page_directory();

    if ((directory[directory_index] & PAGE_PRESENT) == 0)
    {
        /*
         * paging_lock -> pmm_lock
         *
         * This ordering is intentional.
         */
        uintptr_t table_frame =
            pmm_allocate_frame();

        if (table_frame == 0)
        {
            spin_unlock_irqrestore(
                &paging_lock,
                irq_flags);

            return false;
        }

        uint32_t directory_flags =
            PAGE_PRESENT |
            PAGE_WRITABLE;

        if ((flags & PAGE_USER) != 0)
            directory_flags |= PAGE_USER;

        directory[directory_index] =
            (uint32_t)(table_frame & PAGE_FRAME) |
            directory_flags;

        uint32_t *page_table =
            recursive_page_table(directory_index);

        /*
         * The recursive mapping corresponding to this PDE was
         * previously not present.
         */
        invalidate_page((uintptr_t)page_table);

        memset(page_table, 0, PAGE_SIZE);
    }
    else
    {
        if ((flags & PAGE_USER) != 0)
            directory[directory_index] |= PAGE_USER;

        directory[directory_index] |= PAGE_WRITABLE;
    }

    uint32_t *page_table =
        recursive_page_table(directory_index);

    /*
     * Never overwrite an existing mapping.
     */
    if ((page_table[table_index] & PAGE_PRESENT) != 0)
    {
        spin_unlock_irqrestore(
            &paging_lock,
            irq_flags);

        return false;
    }

    page_table[table_index] =
        (uint32_t)(physical_address & PAGE_FRAME) |
        PAGE_PRESENT |
        (flags & PAGE_MAP_FLAGS);

    invalidate_page(virtual_address);

    spin_unlock_irqrestore(
        &paging_lock,
        irq_flags);

    return true;
}

bool paging_unmap_page(
    uintptr_t virtual_address,
    bool release_frame)
{
    if (!is_page_aligned(virtual_address))
        return false;

    size_t directory_index =
        page_directory_index(virtual_address);

    size_t table_index =
        page_table_index(virtual_address);

    if (directory_index == RECURSIVE_DIRECTORY_INDEX)
        return false;

    uint32_t irq_flags =
        spin_lock_irqsave(&paging_lock);

    uint32_t *directory =
        recursive_page_directory();

    if ((directory[directory_index] & PAGE_PRESENT) == 0)
    {
        spin_unlock_irqrestore(
            &paging_lock,
            irq_flags);

        return false;
    }

    uint32_t *page_table =
        recursive_page_table(directory_index);

    uint32_t page_entry =
        page_table[table_index];

    if ((page_entry & PAGE_PRESENT) == 0)
    {
        spin_unlock_irqrestore(
            &paging_lock,
            irq_flags);

        return false;
    }

    uintptr_t physical_address =
        (uintptr_t)(page_entry & PAGE_FRAME);

    page_table[table_index] = 0;

    invalidate_page(virtual_address);

    /*
     * paging_lock -> pmm_lock
     */
    if (release_frame)
        pmm_free_frame(physical_address);

    spin_unlock_irqrestore(
        &paging_lock,
        irq_flags);

    return true;
}
bool paging_get_physical_address(
    uintptr_t virtual_address,
    uintptr_t *physical_address)
{
    if (physical_address == NULL)
        return false;

    /*
     * Do not allow callers to inspect the recursive paging area
     * through this normal API.
     */
    size_t directory_index =
        page_directory_index(virtual_address);

    if (directory_index == RECURSIVE_DIRECTORY_INDEX)
        return false;

    uint32_t irq_flags =
        spin_lock_irqsave(&paging_lock);

    size_t table_index =
        page_table_index(virtual_address);

    uintptr_t page_offset =
        virtual_address & (PAGE_SIZE - 1u);

    uint32_t *directory =
        recursive_page_directory();

    if ((directory[directory_index] & PAGE_PRESENT) == 0)
    {
        spin_unlock_irqrestore(
            &paging_lock,
            irq_flags);

        return false;
    }

    uint32_t *page_table =
        recursive_page_table(directory_index);

    uint32_t page_entry =
        page_table[table_index];

    if ((page_entry & PAGE_PRESENT) == 0)
    {
        spin_unlock_irqrestore(
            &paging_lock,
            irq_flags);

        return false;
    }

    *physical_address =
        (uintptr_t)(page_entry & PAGE_FRAME) +
        page_offset;

    spin_unlock_irqrestore(
        &paging_lock,
        irq_flags);

    return true;
}

bool paging_is_mapped(uintptr_t virtual_address)
{
    size_t directory_index =
        page_directory_index(virtual_address);

    if (directory_index == RECURSIVE_DIRECTORY_INDEX)
        return false;

    size_t table_index =
        page_table_index(virtual_address);

    uint32_t irq_flags =
        spin_lock_irqsave(&paging_lock);

    uint32_t *directory =
        recursive_page_directory();

    if ((directory[directory_index] & PAGE_PRESENT) == 0)
    {
        spin_unlock_irqrestore(
            &paging_lock,
            irq_flags);

        return false;
    }

    uint32_t *page_table =
        recursive_page_table(directory_index);

    bool mapped =
        (page_table[table_index] & PAGE_PRESENT) != 0;

    spin_unlock_irqrestore(
        &paging_lock,
        irq_flags);

    return mapped;
}

bool paging_get_effective_flags(
    uintptr_t virtual_address,
    uint32_t *flags)
{
    if (flags == NULL)
        return false;

    size_t directory_index =
        page_directory_index(virtual_address);

    if (directory_index ==
        RECURSIVE_DIRECTORY_INDEX)
    {
        return false;
    }

    size_t table_index =
        page_table_index(virtual_address);

    uint32_t irq_flags =
        spin_lock_irqsave(&paging_lock);

    uint32_t *directory =
        recursive_page_directory();

    uint32_t directory_entry =
        directory[directory_index];

    if ((directory_entry & PAGE_PRESENT) == 0)
    {
        spin_unlock_irqrestore(
            &paging_lock,
            irq_flags);

        return false;
    }

    uint32_t *page_table =
        recursive_page_table(directory_index);

    uint32_t page_entry =
        page_table[table_index];

    if ((page_entry & PAGE_PRESENT) == 0)
    {
        spin_unlock_irqrestore(
            &paging_lock,
            irq_flags);

        return false;
    }

    uint32_t effective =
        PAGE_PRESENT;

    /*
     * User access requires U/S permission at both paging levels.
     */
    if ((directory_entry & PAGE_USER) != 0 &&
        (page_entry & PAGE_USER) != 0)
    {
        effective |= PAGE_USER;
    }

    /*
     * Write access similarly requires R/W at both levels.
     */
    if ((directory_entry & PAGE_WRITABLE) != 0 &&
        (page_entry & PAGE_WRITABLE) != 0)
    {
        effective |= PAGE_WRITABLE;
    }

    *flags = effective;

    spin_unlock_irqrestore(
        &paging_lock,
        irq_flags);

    return true;
}

bool paging_identity_map_range(
    uintptr_t physical_address,
    size_t length,
    uint32_t flags)
{
    if (length == 0)
        return true;

    if (physical_address >
        UINTPTR_MAX - (length - 1u))
    {
        return false;
    }

    uintptr_t start =
        physical_address &
        ~(uintptr_t)(PAGE_SIZE - 1u);

    uintptr_t end =
        (physical_address + length - 1u) &
        ~(uintptr_t)(PAGE_SIZE - 1u);

    for (uintptr_t page = start;; page += PAGE_SIZE)
    {
        if (!paging_is_mapped(page))
        {
            /*
             * Identity map:
             *
             * virtual == physical
             */
            if (!paging_map_page(
                    page,
                    page,
                    flags))
            {
                return false;
            }
        }

        if (page == end)
            break;

        if (page > UINTPTR_MAX - PAGE_SIZE)
            return false;
    }

    return true;
}