#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/user_image.h>

/**
 * Load a little-endian ELF32 i386 ET_EXEC image into a fresh private
 * userspace address space.
 * 
 * Supported:
 *  
 *      ELFCLASS32
 *      ELFDATA2LSB
 *      EM_386
 *      ET_EXEC
 *      PT_LOAD
 *      BSS via p_memsz > p_filesz
 * 
 * Dynamic linking, relallocations, PIE, argv and envp are intentionally
 * outside now.
 */
bool elf_load_user_image(
    user_image_t *image,
    const void *file_data,
    size_t file_size,
    uintptr_t stack_address,
    uintptr_t stack_top);

#endif