#include <stdio.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/fd.h>
#include <kernel/tty.h>
#endif

int fflush(FILE *stream)
{
    if (stream == NULL)
    {
        /*
         * Still no global FILE registry.
         *
         * We will make fflush(NULL) enumerate all open output
         * streams in a later phase.
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

    /*
     * Buffered stdout/stderr.
     */
    if (stream->fd == 1 ||
        stream->fd == 2)
    {
        terminal_write(
            (const char *)stream->write_buffer,
            stream->write_buffer_used);

        stream->write_buffer_used = 0;

        return 0;
    }

    /*
     * VFS-backed regular file.
     */
    if (stream->fd >= KERNEL_FD_FIRST)
    {
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
    }

#endif

    stream->flags |= _IO_ERROR;
    return EOF;
}