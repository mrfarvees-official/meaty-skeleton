#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/user_image.h>

/*
 * Initial userspace argument limits.
 *
 * Deliberately keeps argument construction inside the one-page
 * initial user stack.
 */
#define ELF_USER_MAX_ARGS       16u
#define ELF_USER_MAX_ARG_LENGTH 256u

/*
 * Load a little-endian ELF32 i386 ET_EXEC image into a fresh private
 * userspace address space.
 *
 * Supported:
 *
 *     ELFCLASS32
 *     ELFDATA2LSB
 *     EM_386
 *     ET_EXEC
 *     PT_LOAD
 *     BSS via p_memsz > p_filesz
 *     initial argc/argv stack
 *
 * Dynamic linking, relocations, PIE and envp remain outside this
 * loader for now.
 *
 * argv strings are copied into the new private userspace stack.
 *
 * On success image->stack_top contains the initial ESP value that
 * must be supplied to arch_enter_user().
 */
bool elf_load_user_image(
    user_image_t *image,
    const void *file_data,
    size_t file_size,
    uintptr_t stack_address,
    uintptr_t stack_top,
    size_t argc,
    const char *const argv[]);

#endif