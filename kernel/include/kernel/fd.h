#ifndef KERNEL_FD_H
#define KERNEL_FD_H

#include <stddef.h>
#include <stdint.h>

#define KERNEL_FD_FIRST 3
#define KERNEL_FD_MAX   64

#define KERNEL_FD_READ  (1U << 0)
#define KERNEL_FD_WRITE (1U << 1)

/**
 * Open a VFS-backed regular file and return a descriptor >= 3.
 * 
 * Returns -1 on failure.
 */
int kernel_fd_open(
    const char *path,
    uint32_t flags);

/**
 * Read from an open descriptor.
 * 
 * Returns:
 *      0 on success
 *     -1 on failure
 * 
 * A successful read with *bytes_read == 0 mean EOF.
 */
int kernel_fd_read(
    int fd,
    void *buffer,
    size_t size,
    size_t *bytes_read);

/**
 * Write to an open descriptor
 * 
 * Returns:
 *      0 on success
 *     -1 on failure
 */
int kernel_fd_write(
    int fd,
    const void *buffer,
    size_t size,
    size_t *bytes_written);

/**
 * Close an open descriptor.
 * 
 * Returns:
 *      0 on success
 *     -1 on failure
 */
int kernel_fd_close(int fd);

#endif