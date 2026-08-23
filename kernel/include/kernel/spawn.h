#ifndef KERNEL_SPAWN_H
#define KERNEL_SPAWN_H

#include <stddef.h>

#include <kernel/process.h>

/*
 * Load an ELF executable from VFS and start it as
 * a new userspace process.
 *
 * Returns:
 *
 *     non-zero process ID    success
 *     PROCESS_ID_INVALID     failure
 *
 * The returned PID is the userspace-visible process identity
 * used by waitpid().
 */
process_id_t process_spawn_user(
    const char *path,
    size_t argc,
    const char *const argv[]);

#endif