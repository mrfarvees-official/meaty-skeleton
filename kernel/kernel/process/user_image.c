#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/paging.h>
#include <kernel/pmm.h>
#include <kernel/user_image.h>

/*
 * Temporary supervisor alias used while populating a newly allocated
 * physical code frame.
 *
 * paging_create_user_directory() uses the same address internally,
 * but removes its temporary mapping before returning.
 */
#define USER_IMAGE_COPY_ADDRESS 0xE0000000u

static bool address_page_aligned(
    uintptr_t address)
{
    return (address &
            (PAGE_SIZE - 1u)) == 0;
}

bool user_image_prepare_single_page(
    user_image_t *image,
    const void *code_source,
    uintptr_t code_address,
    uintptr_t stack_address,
    uintptr_t stack_top)
{
    if (image == NULL ||
        code_source == NULL)
    {
        return false;
    }

    image->page_directory = 0;
    image->entry = 0;
    image->stack_top = 0;

    if (!address_page_aligned(
            code_address))
    {
        return false;
    }

    if (!address_page_aligned(
            stack_address))
    {
        return false;
    }

    if (stack_top !=
        stack_address + PAGE_SIZE)
    {
        return false;
    }

    uintptr_t kernel_directory =
        paging_kernel_directory();

    if (kernel_directory == 0 ||
        paging_current_directory() !=
            kernel_directory)
    {
        return false;
    }

    uintptr_t user_directory = 0;

    if (!paging_create_user_directory(
            &user_directory))
    {
        return false;
    }

    /*
     * Allocate a private physical code page.
     */
    uintptr_t code_frame =
        pmm_allocate_frame();

    if (code_frame == 0)
    {
        paging_destroy_user_directory(
            user_directory);

        return false;
    }

    /*
     * Temporarily map the physical code frame into the kernel CR3 so
     * the supplied source page can be copied into it.
     */
    if (!paging_map_page(
            USER_IMAGE_COPY_ADDRESS,
            code_frame,
            PAGE_WRITABLE))
    {
        pmm_free_frame(
            code_frame);

        paging_destroy_user_directory(
            user_directory);

        return false;
    }

    memcpy(
        (void *)USER_IMAGE_COPY_ADDRESS,
        code_source,
        PAGE_SIZE);

    if (!paging_unmap_page(
            USER_IMAGE_COPY_ADDRESS,
            false))
    {
        /*
         * The code frame is still reachable through the temporary
         * mapping here.  This is an unrecoverable paging invariant
         * failure for this small loader stage.
         */
        for (;;)
            __asm__ volatile("cli; hlt");
    }

    /*
     * Once this succeeds, ownership of code_frame belongs to the user
     * directory.  paging_destroy_user_directory() will reclaim it.
     */
    if (!paging_map_page_in_directory(
            user_directory,
            code_address,
            code_frame,
            PAGE_USER))
    {
        pmm_free_frame(
            code_frame);

        paging_destroy_user_directory(
            user_directory);

        return false;
    }

    /*
     * Allocate the private user stack.
     */
    uintptr_t stack_frame =
        pmm_allocate_frame();

    if (stack_frame == 0)
    {
        paging_destroy_user_directory(
            user_directory);

        return false;
    }

    /*
     * Once mapped, stack_frame is also owned by the user directory.
     */
    if (!paging_map_page_in_directory(
            user_directory,
            stack_address,
            stack_frame,
            PAGE_USER | PAGE_WRITABLE))
    {
        pmm_free_frame(
            stack_frame);

        paging_destroy_user_directory(
            user_directory);

        return false;
    }

    /*
     * Both inactive-directory mapping calls must restore the kernel
     * CR3 before returning.
     */
    if (paging_current_directory() !=
        kernel_directory)
    {
        for (;;)
            __asm__ volatile("cli; hlt");
    }

    image->page_directory =
        user_directory;

    image->entry =
        code_address;

    image->stack_top =
        stack_top;

    return true;
}

uintptr_t user_image_detach_directory(
    user_image_t *image)
{
    if (image == NULL)
        return 0;

    uintptr_t directory =
        image->page_directory;

    image->page_directory = 0;

    return directory;
}

void user_image_destroy(
    user_image_t *image)
{
    if (image == NULL)
        return;

    if (image->page_directory == 0)
        return;

    paging_destroy_user_directory(
        image->page_directory);

    image->page_directory = 0;
    image->entry = 0;
    image->stack_top = 0;
}