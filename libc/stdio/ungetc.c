#include <stdio.h>
#include <stddef.h>

int ungetc(int c, FILE *stream)
{
    if (stream == NULL)
        return EOF;

    if (c == EOF)
        return EOF;

    if (!(stream->flags & _IO_READ))
    {
        stream->flags |= _IO_ERROR;
        return EOF;
    }

    if (stream->has_pushback)
        return EOF;

    stream->pushback = c;
    stream->has_pushback = 1;
    stream->flags &= ~_IO_EOF;

    return stream->pushback;
}