#include <stdio.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/fd.h>
#include <kernel/heap.h>
#endif

static int fopen_parse_mode(
    const char *mode,
    unsigned int *stdio_flags,
    unsigned int *kernel_flags)
{
    if (mode == NULL || stdio_flags == NULL || kernel_flags == NULL)
        return 0;

    /**
     * Text and binary modes are identical for now
     */

    /**
     * r
     * rb
     * r+
     * rb+
     * r+b
     */
    if (mode[0] == 'r')
    {
        if (mode[1] == '\0')
        {
            *stdio_flags = _IO_READ;

            *kernel_flags = KERNEL_FD_READ;

            return 1;
        }

        if (mode[1] == 'b' && mode[2] == '\0')
        {
            *stdio_flags = _IO_READ;

            *kernel_flags = KERNEL_FD_READ;

            return 1;
        }

        if (mode[1] == '+' && mode[2] == '\0')
        {
            *stdio_flags = _IO_READ | _IO_WRITE;

            *kernel_flags = KERNEL_FD_READ | KERNEL_FD_WRITE;

            return 1;
        }

        if (mode[1] == 'b' && mode[2] == '+' && mode[3] == '\0')
        {
            *stdio_flags = _IO_READ | _IO_WRITE;

            *kernel_flags = KERNEL_FD_READ | KERNEL_FD_WRITE;

            return 1;
        }

        if (mode[1] == '+' && mode[2] == 'b' && mode[3] == '\0')
        {
            *stdio_flags = _IO_READ | _IO_WRITE;
            *kernel_flags = KERNEL_FD_READ | KERNEL_FD_WRITE;

            return 1;
        }
    }

    /**
     * w
     * wb
     * w+
     * wb+
     * w+b
     *
     * Existing file:
     *      truncate to zero
     *
     * Missing file:
     *      create it
     */
    if (mode[0] == 'w')
    {
        if (mode[1] == '\0')
        {
            *stdio_flags = _IO_WRITE;
            *kernel_flags = KERNEL_FD_WRITE | KERNEL_FD_CREATE | KERNEL_FD_TRUNC;
            return 1;
        }

        if (mode[1] == 'b' && mode[2] == '\0')
        {
            *stdio_flags = _IO_WRITE;
            *kernel_flags = KERNEL_FD_WRITE | KERNEL_FD_CREATE | KERNEL_FD_TRUNC;
            return 1;
        }

        if (mode[1] == '+' && mode[2] == '\0')
        {
            *stdio_flags = _IO_READ | _IO_WRITE;
            *kernel_flags = KERNEL_FD_READ | KERNEL_FD_WRITE | KERNEL_FD_CREATE | KERNEL_FD_TRUNC;
            return 1;
        }

        if (mode[1] == 'b' && mode[2] == '+' && mode[3] == '\0')
        {
            *stdio_flags = _IO_READ | _IO_WRITE;
            *kernel_flags = KERNEL_FD_READ | KERNEL_FD_WRITE | KERNEL_FD_CREATE | KERNEL_FD_TRUNC;
            return 1;
        }

        if (mode[1] == '+' && mode[2] == 'b' && mode[3] == '\0')
        {
            *stdio_flags = _IO_READ | _IO_WRITE;
            *kernel_flags = KERNEL_FD_READ | KERNEL_FD_WRITE | KERNEL_FD_CREATE | KERNEL_FD_TRUNC;
            return 1;
        }

        return 0;
    }

    /**
     * a
     * ab
     * a+
     * ab+
     * a+b
     *
     * Existing file:
     *      preserve contents.
     *
     * Missing file:
     *      create it.
     *
     * Every write:
     *      goes to current EOF.
     */
    if (mode[0] == 'a')
    {
        if (mode[1] == '\0')
        {
            *stdio_flags = _IO_WRITE;
            *kernel_flags = KERNEL_FD_WRITE | KERNEL_FD_CREATE | KERNEL_FD_APPEND;
            return 1;
        }

        if (mode[1] == 'b' && mode[2] == '\0')
        {
            *stdio_flags = _IO_WRITE;
            *kernel_flags = KERNEL_FD_WRITE | KERNEL_FD_CREATE | KERNEL_FD_APPEND;
            return 1;
        }

        if (mode[1] == '+' && mode[2] == '\0')
        {
            *stdio_flags = _IO_READ | _IO_WRITE;
            *kernel_flags = KERNEL_FD_READ | KERNEL_FD_WRITE | KERNEL_FD_CREATE | KERNEL_FD_APPEND;
            return 1;
        }

        if (mode[1] == 'b' && mode[2] == '+' && mode[3] == '\0')
        {
            *stdio_flags = _IO_READ | _IO_WRITE;
            *kernel_flags = KERNEL_FD_READ | KERNEL_FD_WRITE | KERNEL_FD_CREATE | KERNEL_FD_APPEND;
            return 1;
        }

        if (mode[1] == '+' && mode[2] == 'b' && mode[3] == '\0')
        {
            *stdio_flags = _IO_READ | _IO_WRITE;
            *kernel_flags = KERNEL_FD_READ | KERNEL_FD_WRITE | KERNEL_FD_CREATE | KERNEL_FD_APPEND;
            return 1;
        }

        return 0;
    }

    return 0;
}

FILE *fopen(
    const char *path,
    const char *mode)
{
#if defined(__is_libk)

    FILE *stream;
    unsigned int stdio_flags;
    unsigned int kernel_flags;
    int fd;

    if (path == NULL || mode == NULL)
        return NULL;

    if (!fopen_parse_mode(mode, &stdio_flags, &kernel_flags))
        return NULL;

    fd = kernel_fd_open(
        path,
        kernel_flags);

    if (fd < 0)
        return NULL;

    stream = kmalloc(sizeof(FILE));

    if (stream == NULL)
    {
        kernel_fd_close(fd);
        return NULL;
    }

    stream->fd = fd;
    stream->flags = stdio_flags;
    stream->pushback = 0;
    stream->has_pushback = 0;
    stream->write_buffer = NULL;
    stream->write_buffer_size = 0;
    stream->write_buffer_used = 0;
    stream->buffering_mode = _IONBF;

    if (stdio_flags & _IO_WRITE)
    {
        if (setvbuf(
                stream,
                NULL,
                _IOFBF,
                BUFSIZ) != 0)
        {
            kernel_fd_close(fd);
            kfree(stream);
            return NULL;
        }
    }

    return stream;

#else

    (void)path;
    (void)mode;

    return NULL;

#endif
}