#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#if defined(__is_libk)
#include <kernel/fd.h>
#endif

long ftell(FILE *stream)
{
    if (stream == NULL)
        return -1L;

#if defined(__is_libk)

    if (stream->fd < KERNEL_FD_FIRST)
    {
        stream->flags |= _IO_ERROR;
        return -1L;
    }

    size_t pos = 0;

    if (kernel_fd_seek(
            stream->fd,
            0,
            KERNEL_FD_SEEK_CUR,
            &pos) != 0)
    {
        stream->flags |= _IO_ERROR;
        return -1L;
    }

    /*
     * Pending buffered output is part of the logical stdio position
     * even though the fd offset has not advanced yet.
     */
    if (stream->write_buffer_used >
        (size_t)LONG_MAX - position)
    {
        stream->flags |= _IO_ERROR;
        return -1L;
    }

    position +=
        stream->write_buffer_used;

    /**
     * A pending pushed-back character logically moves the stream
     * position back by one byte, even though the fd offset itself
     * was not changed.
     */
    if (stream->has_pushback)
    {
        if (pos == 0)
        {
            stream->flags |= _IO_ERROR;
            return -1L;
        }

        pos--;
    }

    if (pos > (size_t)LONG_MAX)
    {
        stream->flags |= _IO_ERROR;
        return -1L;
    }

    return (long)pos;

#else

    stream->flags |= _IO_ERROR;
    return -1L;

#endif
}
