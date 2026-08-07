#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/paging.h>
#include <kernel/pmm.h>

#define PAGE_DIRECTORY_ENTRIES 1024u
#define PAGE_TABLE_ENTRIES     1024u

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
#define RECURSIVE_TABLES_BASE     0xFFC00000u
#define RECURSIVE_DIRECTORY_BASE  0xFFFFF000u

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

static uint32_t* recursive_page_directory(void)
{
    return (uint32_t*)RECURSIVE_DIRECTORY_BASE;
}

static uint32_t* recursive_page_table(size_t directory_index)
{
    return (uint32_t*)(
        RECURSIVE_TABLES_BASE +
        directory_index * PAGE_SIZE
    );
}

static void load_page_directory(uintptr_t physical_address)
{
    __asm__ volatile (
        "movl %0, %%cr3"
        :
        : "r"((uint32_t)physical_address)
        : "memory"
    );
}

static void reload_page_directory(void)
{
    uint32_t cr3;

    __asm__ volatile (
        "movl %%cr3, %0"
        : "=r"(cr3)
    );

    __asm__ volatile (
        "movl %0, %%cr3"
        :
        : "r"(cr3)
        : "memory"
    );
}

static void enable_paging(void)
{
    uint32_t cr0;

    __asm__ volatile (
        "movl %%cr0, %0"
        : "=r"(cr0)
    );

    cr0 |= (1u << 31);

    __asm__ volatile (
        "movl %0, %%cr0"
        :
        : "r"(cr0)
        : "memory"
    );
}

static void invalidate_page(uintptr_t virtual_address)
{
    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"(virtual_address)
        : "memory"
    );
}

void paging_initialize(void)
{
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

    /*
     * Never allow normal mappings inside the recursive paging area.
     */
    if (directory_index == RECURSIVE_DIRECTORY_INDEX)
        return false;

    uint32_t* directory = recursive_page_directory();

    /*
     * Allocate a page table if this directory entry does not have one.
     */
    if ((directory[directory_index] & PAGE_PRESENT) == 0)
    {
        uintptr_t table_frame = pmm_allocate_frame();

        if (table_frame == 0)
            return false;

        uint32_t directory_flags =
            PAGE_PRESENT |
            PAGE_WRITABLE;

        /*
         * Both the PDE and PTE must have PAGE_USER for user access.
         */
        if ((flags & PAGE_USER) != 0)
            directory_flags |= PAGE_USER;

        directory[directory_index] =
            (uint32_t)(table_frame & PAGE_FRAME) |
            directory_flags;

        /*
         * The page table becomes accessible through recursive paging
         * immediately after its PDE is installed.
         */
        uint32_t* page_table =
            recursive_page_table(directory_index);

        /*
         * Invalidate the recursive virtual address because it was
         * previously unmapped.
         */
        invalidate_page((uintptr_t)page_table);

        memset(page_table, 0, PAGE_SIZE);
    }
    else
    {
        /*
         * Upgrade the directory entry when adding a user-accessible
         * mapping to an existing page table.
         */
        if ((flags & PAGE_USER) != 0)
            directory[directory_index] |= PAGE_USER;

        /*
         * Keep the PDE writable. The PTE still controls whether the
         * actual mapped page is writable.
         */
        directory[directory_index] |= PAGE_WRITABLE;
    }

    uint32_t* page_table =
        recursive_page_table(directory_index);

    /*
     * Refuse to silently overwrite an existing mapping.
     */
    if ((page_table[table_index] & PAGE_PRESENT) != 0)
        return false;

    page_table[table_index] =
        (uint32_t)(physical_address & PAGE_FRAME) |
        PAGE_PRESENT |
        (flags & PAGE_MAP_FLAGS);

    invalidate_page(virtual_address);

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

    uint32_t* directory = recursive_page_directory();

    if ((directory[directory_index] & PAGE_PRESENT) == 0)
        return false;

    uint32_t* page_table =
        recursive_page_table(directory_index);

    uint32_t page_entry = page_table[table_index];

    if ((page_entry & PAGE_PRESENT) == 0)
        return false;

    uintptr_t physical_address =
        (uintptr_t)(page_entry & PAGE_FRAME);

    page_table[table_index] = 0;

    invalidate_page(virtual_address);

    if (release_frame)
        pmm_free_frame(physical_address);

    /*
     * This implementation intentionally keeps empty page tables.
     *
     * You can reclaim empty page tables later, but do not free the
     * bootstrap first_page_table through the PMM.
     */
    return true;
}

bool paging_get_physical_address(
    uintptr_t virtual_address,
    uintptr_t* physical_address)
{
    if (physical_address == NULL)
        return false;

    size_t directory_index =
        page_directory_index(virtual_address);

    size_t table_index =
        page_table_index(virtual_address);

    uintptr_t page_offset =
        virtual_address & (PAGE_SIZE - 1u);

    uint32_t* directory = recursive_page_directory();

    if ((directory[directory_index] & PAGE_PRESENT) == 0)
        return false;

    uint32_t* page_table =
        recursive_page_table(directory_index);

    uint32_t page_entry =
        page_table[table_index];

    if ((page_entry & PAGE_PRESENT) == 0)
        return false;

    *physical_address =
        (uintptr_t)(page_entry & PAGE_FRAME) +
        page_offset;

    return true;
}

bool paging_is_mapped(uintptr_t virtual_address)
{
    size_t directory_index =
        page_directory_index(virtual_address);

    size_t table_index =
        page_table_index(virtual_address);

    uint32_t* directory = recursive_page_directory();

    if ((directory[directory_index] & PAGE_PRESENT) == 0)
        return false;

    uint32_t* page_table =
        recursive_page_table(directory_index);

    return (page_table[table_index] & PAGE_PRESENT) != 0;
}