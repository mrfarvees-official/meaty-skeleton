#ifndef USER_RUNTIME_H
#define USER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#define USER_STDIN  0
#define USER_STDOUT 1
#define USER_STDERR 2

#define USER_OPEN_READ    (1u << 0)
#define USER_OPEN_WRITE   (1u << 1)
#define USER_OPEN_APPEND  (1u << 2)
#define USER_OPEN_CREATE  (1u << 3)
#define USER_OPEN_TRUNC   (1u << 4)


/*
 * Ordered keyboard event values.
 *
 * Values <= 0xff are ordinary characters.
 */
#define USER_KEY_LEFT      0x100
#define USER_KEY_RIGHT     0x101
#define USER_KEY_UP        0x102
#define USER_KEY_DOWN      0x103
#define USER_KEY_HOME      0x104
#define USER_KEY_END       0x105
#define USER_KEY_DELETE    0x106


int32_t user_read(
    int fd,
    void *buffer,
    size_t count);

int32_t user_write(
    int fd,
    const void *buffer,
    size_t count);

int user_write_string(
    int fd,
    const char *string);

int32_t user_open(
    const char *path,
    uint32_t flags);

int32_t user_close(
    int fd);

int32_t user_spawn(
    const char *path,
    uint32_t argc,
    const char *const argv[]);

int32_t user_waitpid(
    uint32_t pid,
    int *status);

/*
 * Non-blocking ordered keyboard event.
 *
 * Returns:
 *
 *     1..255        character
 *     USER_KEY_*    special key
 *     0             no input
 *     <0            error
 */
int32_t user_key_event(void);

void user_yield(void);

void user_exit(
    int status)
    __attribute__((noreturn));

#endif