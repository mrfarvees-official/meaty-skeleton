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

    /*
     * Any stream configured with an output buffer goes through
     * fwrite(), including stdout/stderr if setvbuf() changes them.
     */
    if (stream->write_buffer != NULL &&
        stream->write_buffer_size != 0)
    {
        if (fwrite(
                &c,
                1,
                1,
                stream) != 1)
        {
            stream->flags |= _IO_ERROR;
            return EOF;
        }

        return (int)c;
    }

#if defined(__is_libk)

    /*
     * stdout and stderr map to the terminal.
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
     * fd >= 3 is a VFS-backed file.
     */
    if (stream->fd >= KERNEL_FD_FIRST)
    {
        if (fwrite(
                &c,
                1,
                1,
                stream) != 1)
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