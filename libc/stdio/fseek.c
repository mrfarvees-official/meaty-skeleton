#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__is_libk)
#include <kernel/fd.h>
#endif

int fseek(
    FILE *stream,
    long offset,
    int whence)
{
    if (stream == NULL)
        return -1;

#if defined(__is_libk)

    /**
     * stdin/stdout/stderr are not seekable in the current
     * implementation.
     */
    if (stream->fd < KERNEL_FD_FIRST)
    {
        stream->flags |= _IO_ERROR;
        return -1;
    }

    int kernel_whence;

    switch(whence)
    {
        case SEEK_SET:
            kernel_whence = KERNEL_FD_SEEK_SET;
            break;
        
        case SEEK_CUR:
            kernel_whence = KERNEL_FD_SEEK_CUR;
            break;

        case SEEK_END:
            kernel_whence = KERNEL_FD_SEEK_END;
            break;

        default:
            stream->flags |= _IO_ERROR;
            return -1;
    }

    int64_t kernel_offset = (int64_t)offset;

    /**
     * ungetc() does not move the underlying fd offset backwards.
     * 
     * If one pushed-back byte is pending, the logical stream
     * position is one byte before the underlying descriptor
     * position. Account for that when SEEK_CUR is requested.
     */
    if (whence == SEEK_CUR && stream->has_pushback)
    {
        kernel_offset -= 1;
    }

    size_t new_offset = 0;

    if (kernel_fd_seek(
            stream->fd,
            kernel_offset,
            kernel_whence,
            &new_offset) != 0)
    {
        stream->flags |= _IO_ERROR;
        return -1;
    }

    (void)new_offset;

    /**
     * Successful repositioning discards pushback and clears EOF.
     */
    stream->has_pushback = 0;
    stream->pushback = 0;
    stream->flags &= ~_IO_EOF;

    return 0;

#else

    (void)offset;
    (void)whence;

    stream->flags |= _IO_ERROR;
    return -1;

#endif
}