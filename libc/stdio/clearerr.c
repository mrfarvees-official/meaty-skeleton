#include <stdio.h>
#include <stddef.h>

void clearerr(FILE *stream)
{
    if (stream == NULL)
        return;

    stream->flags &= ~(_IO_EOF | _IO_ERROR);
}