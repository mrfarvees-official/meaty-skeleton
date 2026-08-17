#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <kernel/elf.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>

#define ELF_CLASS_32                1u
#define ELF_DATA_LSB                1u
#define ELF_VERSION_CURRENT         1u

#define ELF_TYPE_EXECUTABLE         2u
#define ELF_MACHINE_I386            3u

#define ELF_PROGRAM_LOAD            1u

#define ELF_PROGRAM_FLAG_EXECUTE    0x1u
#define ELF_PROGRAM_FLAG_WRITE      0x2u

/*
 * Keep userspace below 3 GiB for this initial loader.
 */
#define ELF_USER_LIMIT              0xC0000000u

/*
 * Temporary supervisor alias used only while populating one newly
 * allocated physical user page.
 */
#define ELF_PAGE_SCRATCH_ADDRESS    0xE0000000u

typedef struct __attribute__((packed))
{
    uint8_t identification[16];

    uint16_t type;
    uint16_t machine;

    uint32_t version;
    uint32_t entry;
    uint32_t program_header_offset;
    uint32_t section_header_offset;
    uint32_t flags;

    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} elf32_header_t;

typedef struct __attribute__((packed))
{
    uint32_t type;
    uint32_t offset;
    uint32_t virtual_address;
    uint32_t physical_address;
    uint32_t file_size;
    uint32_t memory_size;
    uint32_t flags;
    uint32_t alignment;
} elf32_program_header_t;

static bool file_range_valid(
    size_t offset,
    size_t length,
    size_t file_size)
{
    if (offset > file_size)
        return false;

    return length <= file_size - offset;
}

static uintptr_t page_align_down(
    uintptr_t address)
{
    return address &
        ~(uintptr_t)(PAGE_SIZE - 1u);
}

static bool populate_segment_page(
    uintptr_t directory,
    uintptr_t virtual_page,
    const uint8_t *file_data,
    const elf32_program_header_t *segment)
{
    uintptr_t frame =
        pmm_allocate_frame();

    if (frame == 0)
    {
        printf(
            "ELF: PMM frame allocation failed\n");

        return false;
    }

    /*
     * Temporarily expose the physical frame in the kernel address
     * space so its contents can be initialized.
     */
    if (!paging_map_page(
            ELF_PAGE_SCRATCH_ADDRESS,
            frame,
            PAGE_WRITABLE))
    {
        printf(
            "ELF: scratch mapping failed\n");

        pmm_free_frame(
            frame);

        return false;
    }

    memset(
        (void *)ELF_PAGE_SCRATCH_ADDRESS,
        0,
        PAGE_SIZE);

    uint64_t page_start =
        (uint64_t)virtual_page;

    uint64_t page_end =
        page_start +
        PAGE_SIZE;

    uint64_t file_virtual_start =
        (uint64_t)
            segment->virtual_address;

    uint64_t file_virtual_end =
        file_virtual_start +
        segment->file_size;

    uint64_t copy_start =
        page_start >
                file_virtual_start
            ? page_start
            : file_virtual_start;

    uint64_t copy_end =
        page_end <
                file_virtual_end
            ? page_end
            : file_virtual_end;

    if (copy_start < copy_end)
    {
        size_t destination_offset =
            (size_t)(
                copy_start -
                page_start);

        size_t source_offset =
            (size_t)segment->offset +
            (size_t)(
                copy_start -
                file_virtual_start);

        size_t copy_length =
            (size_t)(
                copy_end -
                copy_start);

        memcpy(
            (void *)(
                ELF_PAGE_SCRATCH_ADDRESS +
                destination_offset),
            file_data +
                source_offset,
            copy_length);
    }

    if (!paging_unmap_page(
            ELF_PAGE_SCRATCH_ADDRESS,
            false))
    {
        printf(
            "ELF: scratch unmap failed\n");

        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    uint32_t mapping_flags =
        PAGE_USER;

    if ((segment->flags &
         ELF_PROGRAM_FLAG_WRITE) != 0)
    {
        mapping_flags |=
            PAGE_WRITABLE;
    }

    if (!paging_map_page_in_directory(
            directory,
            virtual_page,
            frame,
            mapping_flags))
    {
        printf(
            "ELF: inactive-directory mapping failed\n");

        pmm_free_frame(
            frame);

        return false;
    }

    return true;
}

static bool load_segment(
    uintptr_t directory,
    const uint8_t *file_data,
    size_t file_size,
    const elf32_program_header_t *segment)
{
    if (segment->memory_size == 0)
        return true;

    if (segment->file_size >
        segment->memory_size)
    {
        printf(
            "ELF: filesz exceeds memsz\n");

        return false;
    }

    if (!file_range_valid(
            (size_t)segment->offset,
            (size_t)segment->file_size,
            file_size))
    {
        printf(
            "ELF: segment file range invalid offset=0x%lx size=0x%lx file=0x%lx\n",
            (unsigned long)segment->offset,
            (unsigned long)segment->file_size,
            (unsigned long)file_size);

        return false;
    }

    uint64_t memory_start =
        (uint64_t)
            segment->virtual_address;

    uint64_t memory_end =
        memory_start +
        segment->memory_size;

    if (memory_start == 0 ||
        memory_end <= memory_start ||
        memory_end > ELF_USER_LIMIT)
    {
        printf(
            "ELF: invalid memory range start=0x%lx end=0x%lx\n",
            (unsigned long)memory_start,
            (unsigned long)memory_end);

        return false;
    }

    if (segment->alignment > 1u)
    {
        uint32_t alignment =
            segment->alignment;

        if ((alignment &
             (alignment - 1u)) != 0)
        {
            printf(
                "ELF: p_align is not power of two\n");

            return false;
        }

        if ((segment->offset &
             (alignment - 1u)) !=
            (segment->virtual_address &
             (alignment - 1u)))
        {
            printf(
                "ELF: p_offset/p_vaddr alignment mismatch\n");

            return false;
        }
    }

    uintptr_t first_page =
        page_align_down(
            (uintptr_t)memory_start);

    uint64_t final_page_end =
        (memory_end +
         (PAGE_SIZE - 1u)) &
        ~(uint64_t)(PAGE_SIZE - 1u);

    for (uint64_t page =
            first_page;
         page < final_page_end;
         page += PAGE_SIZE)
    {
        if (!populate_segment_page(
                directory,
                (uintptr_t)page,
                file_data,
                segment))
        {
            printf(
                "ELF: populate page 0x%lx failed\n",
                (unsigned long)page);

            return false;
        }
    }

    return true;
}

static bool map_user_stack(
    uintptr_t directory,
    uintptr_t stack_address,
    uintptr_t stack_top)
{
    if ((stack_address &
         (PAGE_SIZE - 1u)) != 0)
    {
        return false;
    }

    if (stack_address == 0 ||
        stack_address >= ELF_USER_LIMIT)
    {
        return false;
    }

    if (stack_address >
        UINTPTR_MAX - PAGE_SIZE)
    {
        return false;
    }

    if (stack_top !=
        stack_address + PAGE_SIZE)
    {
        return false;
    }

    if (stack_top > ELF_USER_LIMIT)
        return false;

    uintptr_t frame =
        pmm_allocate_frame();

    if (frame == 0)
        return false;

    /*
     * Give userspace a deterministic zeroed initial stack.
     */
    if (!paging_map_page(
            ELF_PAGE_SCRATCH_ADDRESS,
            frame,
            PAGE_WRITABLE))
    {
        pmm_free_frame(
            frame);

        return false;
    }

    memset(
        (void *)ELF_PAGE_SCRATCH_ADDRESS,
        0,
        PAGE_SIZE);

    if (!paging_unmap_page(
            ELF_PAGE_SCRATCH_ADDRESS,
            false))
    {
        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    if (!paging_map_page_in_directory(
            directory,
            stack_address,
            frame,
            PAGE_USER |
                PAGE_WRITABLE))
    {
        pmm_free_frame(
            frame);

        return false;
    }

    return true;
}

bool elf_load_user_image(
    user_image_t *image,
    const void *file_data,
    size_t file_size,
    uintptr_t stack_address,
    uintptr_t stack_top)
{
    if (image == NULL ||
        file_data == NULL)
    {
        printf(
            "ELF: invalid arguments\n");

        return false;
    }

    image->page_directory = 0;
    image->entry = 0;
    image->stack_top = 0;

    if (file_size <
        sizeof(elf32_header_t))
    {
        printf(
            "ELF: file too small\n");

        return false;
    }

    if (paging_current_directory() !=
        paging_kernel_directory())
    {
        printf(
            "ELF: kernel CR3 not active\n");

        return false;
    }

    const uint8_t *bytes =
        (const uint8_t *)file_data;

    const elf32_header_t *header =
        (const elf32_header_t *)
            file_data;

    if (header->identification[0] != 0x7Fu ||
        header->identification[1] != 'E' ||
        header->identification[2] != 'L' ||
        header->identification[3] != 'F')
    {
        printf(
            "ELF: bad magic\n");

        return false;
    }

    if (header->identification[4] !=
            ELF_CLASS_32 ||
        header->identification[5] !=
            ELF_DATA_LSB ||
        header->identification[6] !=
            ELF_VERSION_CURRENT)
    {
        printf(
            "ELF: unsupported identification\n");

        return false;
    }

    if (header->type !=
            ELF_TYPE_EXECUTABLE ||
        header->machine !=
            ELF_MACHINE_I386 ||
        header->version !=
            ELF_VERSION_CURRENT)
    {
        printf(
            "ELF: unsupported type/machine/version\n");

        return false;
    }

    if (header->header_size !=
            sizeof(elf32_header_t) ||
        header->program_header_entry_size !=
            sizeof(elf32_program_header_t) ||
        header->program_header_count == 0)
    {
        printf(
            "ELF: invalid ELF header sizes\n");

        return false;
    }

    size_t program_table_offset =
        (size_t)
            header->program_header_offset;

    size_t program_count =
        (size_t)
            header->program_header_count;

    if (program_table_offset >
        file_size)
    {
        printf(
            "ELF: program table outside file\n");

        return false;
    }

    if (program_count >
        (file_size -
         program_table_offset) /
            sizeof(elf32_program_header_t))
    {
        printf(
            "ELF: program table truncated\n");

        return false;
    }

    uintptr_t directory = 0;

    if (!paging_create_user_directory(
            &directory))
    {
        printf(
            "ELF: failed creating user directory\n");

        return false;
    }

    bool found_loadable_segment =
        false;

    bool entry_is_executable =
        false;

    for (size_t i = 0;
         i < program_count;
         ++i)
    {
        size_t offset =
            program_table_offset +
            i *
            sizeof(elf32_program_header_t);

        const elf32_program_header_t *segment =
            (const elf32_program_header_t *)(
                bytes + offset);

        if (segment->type !=
            ELF_PROGRAM_LOAD)
        {
            continue;
        }

        /*
         * Some linkers emit an empty PT_LOAD.
         * It contributes no memory mapping and can be ignored.
         */
        if (segment->memory_size == 0)
            continue;

        found_loadable_segment =
            true;

        uint64_t segment_start =
            (uint64_t)
                segment->virtual_address;

        uint64_t segment_end =
            segment_start +
            segment->memory_size;

        if ((segment->flags &
             ELF_PROGRAM_FLAG_EXECUTE) != 0 &&
            (uint64_t)header->entry >=
                segment_start &&
            (uint64_t)header->entry <
                segment_end)
        {
            entry_is_executable =
                true;
        }

        if (!load_segment(
                directory,
                bytes,
                file_size,
                segment))
        {
            printf(
                "ELF: PH%lu load failed\n",
                (unsigned long)i);

            paging_destroy_user_directory(
                directory);

            return false;
        }
    }

    if (!found_loadable_segment)
    {
        printf(
            "ELF: no non-empty LOAD segment\n");

        paging_destroy_user_directory(
            directory);

        return false;
    }

    if (!entry_is_executable)
    {
        printf(
            "ELF: entry is not inside executable LOAD\n");

        paging_destroy_user_directory(
            directory);

        return false;
    }

    if (!map_user_stack(
            directory,
            stack_address,
            stack_top))
    {
        printf(
            "ELF: stack mapping failed\n");

        paging_destroy_user_directory(
            directory);

        return false;
    }

    /*
     * All inactive-directory operations must leave the canonical
     * kernel address space active.
     */
    if (paging_current_directory() !=
        paging_kernel_directory())
    {
        printf(
            "ELF: kernel CR3 was not restored\n");

        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    image->page_directory =
        directory;

    image->entry =
        (uintptr_t)header->entry;

    image->stack_top =
        stack_top;

    return true;
}