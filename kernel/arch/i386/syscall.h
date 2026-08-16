#ifndef KERNEL_ARCH_I386_SYSCALL_H
#define KERNEL_ARCH_I386_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#define I386_SYSCALL_VECTOR       0x80u

/*
 * Syscall numbers.
 *
 * 0 and 1 were U2 bring-up probes and are intentionally no longer
 * part of the active syscall ABI.
 */
#define I386_SYSCALL_GETTID 2u

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

bool syscall_initialize(void);

#endif