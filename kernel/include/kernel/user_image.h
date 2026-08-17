#ifndef KERNEL_USER_IMAGE_H
#define KERNEL_USER_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uintptr_t page_directory;
    uintptr_t entry;
    uintptr_t stack_top;
} user_image_t;

/*
 * Prepare a minimal userspace image containing:
 *
 *     - one private code page
 *     - one private writable stack page
 *     - one private page directory
 *
 * code_source must point at one complete page of kernel-accessible
 * memory.  The page is copied into a newly allocated physical frame.
 *
 * code_address and stack_address must be page aligned.
 * stack_top must lie immediately above the mapped stack page.
 *
 * The canonical kernel page directory must be active.
 */
bool user_image_prepare_single_page(
    user_image_t *image,
    const void *code_source,
    uintptr_t code_address,
    uintptr_t stack_address,
    uintptr_t stack_top);

/*
 * Destroy the address space owned by a prepared image.
 *
 * The image's page directory must not currently be active.
 */
void user_image_destroy(
    user_image_t *image);

#endif