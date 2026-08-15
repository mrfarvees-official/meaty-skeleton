#include <stdio.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/keyboard.h>
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

#endif

    stream->flags |= _IO_ERROR;
    return EOF;
}