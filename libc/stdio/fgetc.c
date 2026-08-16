#include <stdio.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/keyboard.h>
#include <kernel/fd.h>
#endif

int fgetc(FILE *stream)
{
    if (stream == NULL)
        return EOF;

    if (!(stream->flags & _IO_READ))
    {
        stream->flags |= _IO_ERROR;
        return EOF;
    }

    if (stream->has_pushback)
    {
        int c = stream->pushback;

        stream->has_pushback = 0;

        return c;
    }

#if defined(__is_libk)

    /**
     * stdin currently maps to then kernel keyboard character queue.
     */
    if (stream->fd == 0)
    {
        char character;

        if (!keyboard_wait_character(&character))
        {
            stream->flags |= _IO_ERROR;
            return EOF;
        }

        return (unsigned char)character;
    }

    /**
     * Descriptors >= 3 are VFS-backed regular files
     */
    if (stream->fd >= KERNEL_FD_FIRST)
    {
        unsigned char character;
        size_t bytes_read = 0;

        if (kernel_fd_read(
                stream->fd,
                &character,
                1,
                &bytes_read) != 0)
        {
            stream->flags |= _IO_ERROR;
            return EOF;
        }

        /**
         * A successful zero-byte VFS read means that the file
         * offset has reached end-of-file.
         */
        if (bytes_read == 0)
        {
            stream->flags |= _IO_EOF;
            return EOF;
        }

        return (int)character;
    }

#endif

    stream->flags |= _IO_ERROR;
    return EOF;
}