#ifndef KERNEL_ARCH_I386_SYSCALL_H
#define KERNEL_ARCH_I386_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#define I386_SYSCALL_VECTOR                 0x80u

#define I386_SYSCALL_GETTID                 2u
#define I386_SYSCALL_USERCOPY_TEST          3u
#define I386_SYSCALL_DEBUG_WRITE            4u
#define I386_SYSCALL_EXIT                   5u
#define I386_SYSCALL_THREAD_CREATE          6u
#define I386_SYSCALL_YIELD                  7u
#define I386_SYSCALL_WAITPID                8u
#define I386_SYSCALL_SPAWN                  9u
#define I386_SYSCALL_READ                   10u
#define I386_SYSCALL_WRITE                  11u
#define I386_SYSCALL_OPEN                   12u
#define I386_SYSCALL_CLOSE                  13u
#define I386_SYSCALL_READDIR                17u
#define I386_SYSCALL_MKDIR                  18u

#define I386_DIRENT_TYPE_REGULAR            1u
#define I386_DIRENT_TYPE_DIRECTORY          2u
#define I386_DIRENT_NAME_MAX                256u

typedef struct i386_syscall_dirent
{
    uint32_t inode;
    uint32_t type;
    char name[I386_DIRENT_NAME_MAX];
} i386_syscall_dirent_t;

/*
 * Non-blocking ordered keyboard input event.
 *
 * Returns:
 *
 *     0x01..0xff   translated character
 *     0x100+       special key
 *     0            no event available
 *     < 0          error
 *
 * Only key-press events are returned.
 */
#define I386_SYSCALL_KEY_EVENT              14u
#define I386_SYSCALL_CHDIR                  15u
#define I386_SYSCALL_GETCWD                 16u

#define I386_KEY_EVENT_LEFT                 0x100
#define I386_KEY_EVENT_RIGHT                0x101
#define I386_KEY_EVENT_UP                   0x102
#define I386_KEY_EVENT_DOWN                 0x103
#define I386_KEY_EVENT_HOME                 0x104
#define I386_KEY_EVENT_END                  0x105
#define I386_KEY_EVENT_DELETE               0x106


#define I386_SYSCALL_ERROR_NO_SUCH_SYSCALL  (-1)
#define I386_SYSCALL_ERROR_INVALID_STATE    (-2)
#define I386_SYSCALL_ERROR_BAD_ADDRESS      (-3)
#define I386_SYSCALL_ERROR_INVALID_LENGTH   (-4)
#define I386_SYSCALL_ERROR_BAD_FD           (-5)

bool syscall_initialize(void);

#endif