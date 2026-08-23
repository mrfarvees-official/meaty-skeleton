#ifndef KERNEL_ARCH_I386_SYSCALL_H
#define KERNEL_ARCH_I386_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#define I386_SYSCALL_VECTOR         0x80u

/*
 * Existing userspace ABI.
 */
#define I386_SYSCALL_GETTID         2u
#define I386_SYSCALL_USERCOPY_TEST  3u
#define I386_SYSCALL_DEBUG_WRITE    4u
#define I386_SYSCALL_EXIT           5u

/*
 * Create another userspace task sharing the caller's
 * address space.
 *
 * EBX = userspace worker entry address.
 *
 * Returns new TID or a negative error.
 */
#define I386_SYSCALL_THREAD_CREATE  6u

/*
 * Voluntarily yield the current task.
 */
#define I386_SYSCALL_YIELD          7u

/*
 * Non-blocking child collection.
 *
 * EBX = child PID.
 * ECX = userspace int *status, or NULL to discard status.
 *
 * Returns:
 *
 *     1   zombie child collected
 *     0   child still running or PID is not a child
 *    <0   syscall error
 */
#define I386_SYSCALL_WAITPID        8u

/*
 * Spawn a new userspace process.
 *
 * EBX = const char *path
 * ECX = argc
 * EDX = const char *const argv[]
 *
 * Returns:
 *
 *     PID > 0    child created
 *     EAX < 0    error
 */
#define I386_SYSCALL_SPAWN          9u

/*
 * Minimal shell-v0 stdin.
 *
 * EBX = fd
 * ECX = userspace destination buffer
 * EDX = maximum byte count
 *
 * Only fd 0 is supported.
 *
 * Behavior:
 *
 *     - count == 0 returns 0 immediately
 *     - otherwise blocks until at least one keyboard character exists
 *     - then drains any additional characters already queued, up to count
 *     - does not echo
 *     - does not perform line editing
 *
 * Returns number of bytes copied, or a negative error.
 */
#define I386_SYSCALL_READ           10u

/*
 * Minimal shell-v0 stdout/stderr.
 *
 * EBX = fd
 * ECX = userspace source buffer
 * EDX = byte count
 *
 * Supported:
 *
 *     fd 1 = terminal output
 *     fd 2 = terminal output
 *
 * Returns number of bytes written, or a negative error.
 */
#define I386_SYSCALL_WRITE          11u


/*
 * Syscall result convention:
 *
 *     EAX >= 0    success
 *     EAX <  0    error
 */
#define I386_SYSCALL_ERROR_NO_SUCH_SYSCALL (-1)
#define I386_SYSCALL_ERROR_INVALID_STATE   (-2)
#define I386_SYSCALL_ERROR_BAD_ADDRESS     (-3)
#define I386_SYSCALL_ERROR_INVALID_LENGTH  (-4)
#define I386_SYSCALL_ERROR_BAD_FD          (-5)

bool syscall_initialize(void);

#endif