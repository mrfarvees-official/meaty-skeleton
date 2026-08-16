#include <stdio.h>
#include <stddef.h>

int ferror(FILE *stream)
{
    if (stream == NULL)
        return 0;

    return (stream->flags & _IO_ERROR) != 0;
}