#ifndef USER_RUNTIME_H
#define USER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#define USER_STDIN  0
#define USER_STDOUT 1
#define USER_STDERR 2

/*
 * Read up to count bytes.
 *
 * For shell v0 only fd 0 is supported by the kernel.
 *
 * Returns:
 *
 *     >= 0    bytes read
 *     <  0    syscall error
 */
int32_t user_read(
    int fd,
    void *buffer,
    size_t count);

/*
 * Write count bytes.
 *
 * For shell v0 only fd 1 and fd 2 are supported.
 *
 * Returns:
 *
 *     >= 0    bytes written
 *     <  0    syscall error
 */
int32_t user_write(
    int fd,
    const void *buffer,
    size_t count);

/*
 * Convenience helper for NUL-terminated strings.
 *
 * Returns 0 on success, -1 on failure.
 */
int user_write_string(
    int fd,
    const char *string);

/*
 * Spawn a userspace process.
 *
 * argv must contain argc non-NULL entries followed by NULL.
 *
 * Returns:
 *
 *     > 0     child PID
 *     < 0     syscall error
 */
int32_t user_spawn(
    const char *path,
    uint32_t argc,
    const char *const argv[]);

/*
 * Non-blocking child collection.
 *
 * Returns:
 *
 *     1       child collected
 *     0       child still running / not collectable
 *     < 0     syscall error
 */
int32_t user_waitpid(
    uint32_t pid,
    int *status);

/*
 * Voluntarily yield the current task.
 */
void user_yield(void);

/*
 * Terminate the current process.
 */
void user_exit(
    int status)
    __attribute__((noreturn));

#endif