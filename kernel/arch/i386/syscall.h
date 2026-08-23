#ifndef KERNEL_ARCH_I386_SYSCALL_H
#define KERNEL_ARCH_I386_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#define I386_SYSCALL_VECTOR         0x80u

#define I386_SYSCALL_GETTID         2u
#define I386_SYSCALL_USERCOPY_TEST  3u
#define I386_SYSCALL_DEBUG_WRITE    4u
#define I386_SYSCALL_EXIT           5u
#define I386_SYSCALL_THREAD_CREATE  6u
#define I386_SYSCALL_YIELD          7u
#define I386_SYSCALL_WAITPID        8u
#define I386_SYSCALL_SPAWN          9u

/*
 * EBX = fd
 * ECX = userspace buffer
 * EDX = byte count
 *
 * fd 0:
 *     non-blocking keyboard input
 *
 * fd >= 3:
 *     VFS-backed file input
 */
#define I386_SYSCALL_READ           10u

/*
 * EBX = fd
 * ECX = userspace buffer
 * EDX = byte count
 *
 * fd 1/2:
 *     terminal output
 *
 * fd >= 3:
 *     VFS-backed file output
 */
#define I386_SYSCALL_WRITE          11u

/*
 * EBX = userspace path
 * ECX = open flags
 *
 * Returns fd >= 3 or negative error.
 */
#define I386_SYSCALL_OPEN           12u

/*
 * EBX = fd
 *
 * Returns 0 or negative error.
 */
#define I386_SYSCALL_CLOSE          13u


#define I386_SYSCALL_ERROR_NO_SUCH_SYSCALL (-1)
#define I386_SYSCALL_ERROR_INVALID_STATE   (-2)
#define I386_SYSCALL_ERROR_BAD_ADDRESS     (-3)
#define I386_SYSCALL_ERROR_INVALID_LENGTH  (-4)
#define I386_SYSCALL_ERROR_BAD_FD          (-5)

bool syscall_initialize(void);

#endif