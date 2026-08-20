#ifndef KERNEL_SPAWN_H
#define KERNEL_SPAWN_H

#include <stddef.h>

#include <kernel/task.h>

/*
 * Load an ELF executable from VFS and start it as
 * a userspace task.
 *
 * Returns:
 *
 *     non-zero task ID    success
 *     0                   failure
 *
 * We return a task ID instead of task_t * because after
 * publication another CPU may run, exit, and reap the task.
 */
task_id_t process_spawn_user(
    const char *path,
    size_t argc,
    const char *const argv[]);

#endif