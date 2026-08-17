#ifndef KERNEL_ARCH_I386_SYSCALL_H
#define KERNEL_ARCH_I386_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#define I386_SYSCALL_VECTOR         0x80u

/*
 * Syscall numbers.
 *
 * 0 and 1 were U2 bring-up probes and are intentionally no longer
 * part of the active syscall ABI.
 */
#define I386_SYSCALL_GETTID         2u
/*
 * Temporary U2d user-copy validation syscall.
 */
#define I386_SYSCALL_USERCOPY_TEST  3u
/*
 * Temporary direct debug-console output.
 *
 * This is deliberately not the future write(fd, buffer, length)
 * interface.
 */
#define I386_SYSCALL_DEBUG_WRITE    4u

/*
 * Terminate the current user task.
 *
 * EBX carries an exit status for future process bookkeeping.
 * U7 does not store that status yet.
 */
#define I386_SYSCALL_EXIT           5u

/*
 * Syscall result convention:
 *
 *     EAX >= 0    success
 *     EAX <  0    error
 *
 * Keep the initial error namespace deliberately tiny.
 */
#define I386_SYSCALL_ERROR_NO_SUCH_SYSCALL (-1)
#define I386_SYSCALL_ERROR_INVALID_STATE   (-2)
#define I386_SYSCALL_ERROR_BAD_ADDRESS     (-3)
#define I386_SYSCALL_ERROR_INVALID_LENGTH  (-4)

bool syscall_initialize(void);

#endif