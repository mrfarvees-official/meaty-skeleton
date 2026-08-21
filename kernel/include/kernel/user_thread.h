#ifndef KERNEL_USER_THREAD_H
#define KERNEL_USER_THREAD_H

#include <stdint.h>

#include <kernel/task.h>

/*
 * Create a new userspace thread belonging to the CURRENT
 * userspace task's address space.
 *
 * entry is a userspace virtual address inside the current
 * process image.
 *
 * The new thread receives:
 *
 *     - its own task_t
 *     - its own kernel stack
 *     - its own physically-backed userspace stack
 *     - the SAME address_space_t / CR3 as the caller
 *
 * Returns:
 *
 *     non-zero TID    success
 *     0               failure
 *
 * U12.4 intentionally leaves the stack mapping alive until
 * the complete address space is destroyed.
 *
 * Per-thread stack retirement/reuse belongs to the next stage.
 */
task_id_t user_thread_create_current(
    uintptr_t entry);

#endif