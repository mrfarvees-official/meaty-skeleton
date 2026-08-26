#ifndef KERNEL_SPAWN_H
#define KERNEL_SPAWN_H

#include <stddef.h>

#include <kernel/process.h>


/*
 * Optional callback invoked after the process and its main task are
 * completely prepared, but BEFORE the task becomes scheduler-visible.
 *
 * The callback must be short and must not block.
 *
 * This allows subsystems such as the GUI Terminal to establish process
 * ownership before userspace can execute its first instruction.
 */
typedef void (*process_spawn_prepare_t)(
    process_id_t pid,
    void *context);


/*
 * Normal userspace process creation.
 */
process_id_t process_spawn_user(
    const char *path,
    size_t argc,
    const char *const argv[]);


/*
 * Userspace process creation with a pre-publication preparation hook.
 *
 * prepare() runs before task_publish().
 */
process_id_t process_spawn_user_prepared(
    const char *path,
    size_t argc,
    const char *const argv[],
    process_spawn_prepare_t prepare,
    void *prepare_context);


#endif