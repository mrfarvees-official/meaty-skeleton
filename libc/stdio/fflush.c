#include <stdio.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/fd.h>
#endif

int fflush(FILE *stream)
{
    if (stream == NULL)
    {
        /*
         * We don't yet maintain a global registry of all open FILE
         * objects, so there is nothing useful to enumerate here.
         *
         * stdout/stderr are unbuffered and regular file streams must
         * currently be flushed individually.
         */
        return 0;
    }

    if (!(stream->flags & _IO_WRITE))
    {
        stream->flags |= _IO_ERROR;
        return EOF;
    }

    if (stream->write_buffer_used == 0)
        return 0;

#if defined(__is_libk)

    if (stream->fd < KERNEL_FD_FIRST)
    {
        /*
         * Standard output streams are currently unbuffered, so this
         * should never contain pending bytes.
         */
        stream->flags |= _IO_ERROR;
        return EOF;
    }

    size_t offset = 0;

    while (offset <
           stream->write_buffer_used)
    {
        size_t written = 0;

        if (kernel_fd_write(
                stream->fd,
                stream->write_buffer + offset,
                stream->write_buffer_used - offset,
                &written) != 0)
        {
            stream->flags |= _IO_ERROR;
            return EOF;
        }

        if (written == 0)
        {
            stream->flags |= _IO_ERROR;
            return EOF;
        }

        offset += written;
    }

    stream->write_buffer_used = 0;

    return 0;

#else

    stream->flags |= _IO_ERROR;
    return EOF;

#endif
}