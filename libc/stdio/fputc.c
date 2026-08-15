#include <stdio.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/tty.h>
#endif

int fputc(int ic, FILE *stream)
{
    if (stream == NULL)
        return EOF;

    if (!(stream->flags & _IO_WRITE)) 
    {
        stream->flags |= _IO_ERROR;
        return EOF;
    }

    char c = (char)ic;

#if defined(__is_libk)
    /**
     * For now stdout/stderr both map to kernel terminal
     * 
     * Once userspace exists, this becomes write(stream->fd, ...).
     */
    if (stream->fd == 1 || stream->fd == 2) 
    {
        terminal_write(&c, sizeof(c));
        return (unsigned char)c;
    }

    stream->flags |= _IO_ERROR;
    return EOF;
#else
    /**
     * Userspace backend will eventually be:
     * 
     * ssize_t result = write(stream->fd, &c, 1);
     * if (result != 1) 
     * {
     *      stream->flags |= _IO_ERROR;
     *      return EOF;
     * }
     */

     stream->flags |= _IO_ERROR;
     return EOF;
#endif
}