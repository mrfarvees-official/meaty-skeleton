#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <kernel/elf.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>

#define ELF_CLASS_32 1u
#define ELF_DATA_LSB 1u
#define ELF_VERSION_CURRENT 1u

#define ELF_TYPE_EXECUTABLE 2u
#define ELF_MACHINE_I386 3u

#define ELF_PROGRAM_LOAD 1u

#define ELF_PROGRAM_FLAG_EXECUTE 0x1u
#define ELF_PROGRAM_FLAG_WRITE 0x2u

/*
 * Keep userspace below 3 GiB for this initial loader.
 */
#define ELF_USER_LIMIT 0xC0000000u

/*
 * Temporary supervisor alias used while initializing one newly
 * allocated physical user page.
 */
#define ELF_PAGE_SCRATCH_ADDRESS 0xE0000000u

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

    return length <=
           file_size - offset;
}

static uintptr_t page_align_down(
    uintptr_t address)
{
    return address &
           ~(uintptr_t)(PAGE_SIZE - 1u);
}

static bool bounded_string_length(
    const char *string,
    size_t maximum,
    size_t *length)
{
    if (string == NULL ||
        length == NULL ||
        maximum == 0)
    {
        return false;
    }

    for (size_t i = 0;
         i < maximum;
         ++i)
    {
        if (string[i] == '\0')
        {
            *length = i;
            return true;
        }
    }

    return false;
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
            (size_t)(copy_start -
                     page_start);

        size_t source_offset =
            (size_t)segment->offset +
            (size_t)(copy_start -
                     file_virtual_start);

        size_t copy_length =
            (size_t)(copy_end -
                     copy_start);

        memcpy(
            (void *)(ELF_PAGE_SCRATCH_ADDRESS +
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
    uintptr_t stack_top,
    size_t argc,
    const char *const argv[],
    uintptr_t *initial_stack_pointer)
{
    if (initial_stack_pointer == NULL)
        return false;

    *initial_stack_pointer = 0;

    /*
     * Stack bottom must be page aligned.
     */
    if ((stack_address &
         (PAGE_SIZE - 1u)) != 0)
    {
        printf(
            "ELF: stack address is not page aligned\n");

        return false;
    }

    /*
     * Stack top must also be page aligned.
     */
    if ((stack_top &
         (PAGE_SIZE - 1u)) != 0)
    {
        printf(
            "ELF: stack top is not page aligned\n");

        return false;
    }

    /*
     * Stack must be a valid userspace range.
     */
    if (stack_address == 0 ||
        stack_address >= ELF_USER_LIMIT ||
        stack_top > ELF_USER_LIMIT ||
        stack_top <= stack_address)
    {
        printf(
            "ELF: invalid user stack range\n");

        return false;
    }

    /*
     * Stack size must contain whole pages.
     */
    uintptr_t stack_size =
        stack_top - stack_address;

    if ((stack_size &
         (PAGE_SIZE - 1u)) != 0)
    {
        printf(
            "ELF: stack size is not page aligned\n");

        return false;
    }

    size_t stack_page_count =
        (size_t)(stack_size / PAGE_SIZE);

    if (stack_page_count == 0)
    {
        printf(
            "ELF: stack contains no pages\n");

        return false;
    }

    // printf(
    //     "ELF: allocating user stack: %lu bytes, %lu pages\n",
    //     (unsigned long)stack_size,
    //     (unsigned long)stack_page_count);

    /*
     * Validate argc/argv before allocating anything.
     */
    if (argc == 0 ||
        argc > ELF_USER_MAX_ARGS ||
        argv == NULL)
    {
        printf(
            "ELF: invalid userspace argument vector\n");

        return false;
    }

    size_t argument_lengths[ELF_USER_MAX_ARGS];

    for (size_t i = 0;
         i < argc;
         ++i)
    {
        if (!bounded_string_length(
                argv[i],
                ELF_USER_MAX_ARG_LENGTH,
                &argument_lengths[i]))
        {
            printf(
                "ELF: argument %lu is invalid or too long\n",
                (unsigned long)i);

            return false;
        }
    }

    /*
     * ------------------------------------------------------------------
     * Allocate every stack page.
     * ------------------------------------------------------------------
     *
     * For a 1 MiB stack:
     *
     *     1 MiB / 4096 = 256 physical frames
     *
     * Each frame is:
     *
     *     1. allocated
     *     2. temporarily mapped into kernel space
     *     3. zeroed
     *     4. unmapped from scratch address
     *     5. mapped into the new userspace page directory
     */

    uintptr_t top_stack_frame = 0;

    uintptr_t top_stack_page =
        stack_top - PAGE_SIZE;

    for (uintptr_t virtual_page = stack_address;
         virtual_page < stack_top;
         virtual_page += PAGE_SIZE)
    {
        uintptr_t frame =
            pmm_allocate_frame();

        if (frame == 0)
        {
            printf(
                "ELF: stack frame allocation failed at 0x%lx\n",
                (unsigned long)virtual_page);

            return false;
        }

        /*
         * Map physical frame temporarily into
         * the currently active kernel address space.
         */
        if (!paging_map_page(
                ELF_PAGE_SCRATCH_ADDRESS,
                frame,
                PAGE_WRITABLE))
        {
            printf(
                "ELF: stack scratch mapping failed\n");

            pmm_free_frame(frame);

            return false;
        }

        /*
         * New stack pages start zeroed.
         */
        memset(
            (void *)ELF_PAGE_SCRATCH_ADDRESS,
            0,
            PAGE_SIZE);

        if (!paging_unmap_page(
                ELF_PAGE_SCRATCH_ADDRESS,
                false))
        {
            printf(
                "ELF: stack scratch unmap failed\n");

            for (;;)
                __asm__ volatile(
                    "cli; hlt");
        }

        /*
         * Map this physical frame into
         * the userspace address space.
         */
        if (!paging_map_page_in_directory(
                directory,
                virtual_page,
                frame,
                PAGE_USER |
                    PAGE_WRITABLE))
        {
            printf(
                "ELF: user stack mapping failed at 0x%lx\n",
                (unsigned long)virtual_page);

            pmm_free_frame(frame);

            return false;
        }

        /*
         * Remember the physical frame belonging
         * to the very top stack page.
         *
         * argc/argv will be written there.
         */
        if (virtual_page ==
            top_stack_page)
        {
            top_stack_frame =
                frame;
        }
    }

    if (top_stack_frame == 0)
    {
        printf(
            "ELF: failed finding top stack frame\n");

        return false;
    }

    /*
     * ------------------------------------------------------------------
     * Construct argc/argv in the TOP stack page.
     * ------------------------------------------------------------------
     *
     * Even though the process has a full 1 MiB stack,
     * we keep the initial process-start data in the top 4 KiB.
     *
     * This keeps your existing startup ABI simple.
     */

    if (!paging_map_page(
            ELF_PAGE_SCRATCH_ADDRESS,
            top_stack_frame,
            PAGE_WRITABLE))
    {
        printf(
            "ELF: top stack scratch mapping failed\n");

        return false;
    }

    uintptr_t user_argument_addresses[ELF_USER_MAX_ARGS];

    uintptr_t stack_pointer =
        stack_top;

    /*
     * Initial argc/argv must currently fit inside
     * the highest stack page.
     */
    uintptr_t argument_page_bottom =
        stack_top - PAGE_SIZE;

    /*
     * Copy argument strings downward.
     *
     * Example:
     *
     *     "two\0"
     *     "one\0"
     *     "/bin/hello.nex\0"
     */
    for (size_t remaining = argc;
         remaining > 0;
         --remaining)
    {
        size_t index =
            remaining - 1u;

        size_t length_with_nul =
            argument_lengths[index] + 1u;

        if (stack_pointer <
            argument_page_bottom +
                length_with_nul)
        {
            printf(
                "ELF: arguments do not fit top stack page\n");

            paging_unmap_page(
                ELF_PAGE_SCRATCH_ADDRESS,
                false);

            return false;
        }

        stack_pointer -=
            length_with_nul;

        /*
         * Offset inside the highest physical page.
         */
        size_t page_offset =
            (size_t)(stack_pointer -
                     argument_page_bottom);

        memcpy(
            (void *)(ELF_PAGE_SCRATCH_ADDRESS +
                     page_offset),
            argv[index],
            length_with_nul);

        /*
         * Store USERSPACE pointer, not kernel
         * scratch address.
         */
        user_argument_addresses[index] =
            stack_pointer;
    }

    /*
     * i386 stack word alignment.
     */
    stack_pointer &=
        ~(uintptr_t)0x3u;

    /*
     * Startup table:
     *
     *     argc
     *     argv[0]
     *     argv[1]
     *     ...
     *     argv[argc - 1]
     *     NULL
     */
    size_t table_words =
        argc + 2u;

    size_t table_size =
        table_words *
        sizeof(uint32_t);

    if (stack_pointer <
        argument_page_bottom +
            table_size)
    {
        printf(
            "ELF: argument table does not fit top stack page\n");

        paging_unmap_page(
            ELF_PAGE_SCRATCH_ADDRESS,
            false);

        return false;
    }

    stack_pointer -=
        table_size;

    size_t table_offset =
        (size_t)(stack_pointer -
                 argument_page_bottom);

    uint32_t *table =
        (uint32_t *)(ELF_PAGE_SCRATCH_ADDRESS +
                     table_offset);

    /*
     * argc
     */
    table[0] =
        (uint32_t)argc;

    /*
     * argv pointers
     */
    for (size_t i = 0;
         i < argc;
         ++i)
    {
        table[i + 1u] =
            (uint32_t)
                user_argument_addresses[i];
    }

    /*
     * argv[argc] == NULL
     */
    table[argc + 1u] = 0;

    /*
     * Remove temporary kernel mapping.
     */
    if (!paging_unmap_page(
            ELF_PAGE_SCRATCH_ADDRESS,
            false))
    {
        printf(
            "ELF: top stack scratch unmap failed\n");

        for (;;)
            __asm__ volatile(
                "cli; hlt");
    }

    /*
     * ESP supplied to arch_enter_user().
     */
    *initial_stack_pointer =
        stack_pointer;

    // printf(
    //     "ELF: stack ready bottom=0x%lx top=0x%lx esp=0x%lx pages=%lu\n",
    //     (unsigned long)stack_address,
    //     (unsigned long)stack_top,
    //     (unsigned long)stack_pointer,
    //     (unsigned long)stack_page_count);

    return true;
}

bool elf_load_user_image(
    user_image_t *image,
    const void *file_data,
    size_t file_size,
    uintptr_t stack_address,
    uintptr_t stack_top,
    size_t argc,
    const char *const argv[])
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
            (const elf32_program_header_t *)(bytes + offset);

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

    uintptr_t initial_stack_pointer = 0;

    if (!map_user_stack(
            directory,
            stack_address,
            stack_top,
            argc,
            argv,
            &initial_stack_pointer))
    {
        printf(
            "ELF: stack mapping failed\n");

        paging_destroy_user_directory(
            directory);

        return false;
    }

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

    /*
     * This is the initial userspace ESP, not merely the page boundary.
     */
    image->stack_top =
        initial_stack_pointer;

    return true;
}