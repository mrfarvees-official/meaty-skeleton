#include <stdio.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/tty.h>
#include <kernel/fd.h>
#endif

int fputc(int ic, FILE *stream)
{
    unsigned char c;

    if (stream == NULL)
        return EOF;

    if (!(stream->flags & _IO_WRITE))
    {
        stream->flags |= _IO_ERROR;
        return EOF;
    }

    c = (unsigned char)ic;

#if defined(__is_libk)

    /*
     * stdout and stderr map to the kernel terminal.
     */
    if (stream->fd == 1 || stream->fd == 2)
    {
        char character = (char)c;

        terminal_write(
            &character,
            sizeof(character));

        return (int)c;
    }

    /*
     * Descriptors >= 3 are VFS-backed files.
     */
    if (stream->fd >= KERNEL_FD_FIRST)
    {
        size_t bytes_written = 0;

        if (kernel_fd_write(
                stream->fd,
                &c,
                1,
                &bytes_written) != 0)
        {
            stream->flags |= _IO_ERROR;
            return EOF;
        }

        if (bytes_written != 1)
        {
            stream->flags |= _IO_ERROR;
            return EOF;
        }

        return (int)c;
    }

#endif

    stream->flags |= _IO_ERROR;
    return EOF;
}