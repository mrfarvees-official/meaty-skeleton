#include <stdio.h>
#include <stddef.h>

int feof(FILE *stream)
{
    if (stream == NULL)
        return 0;

    return (stream->flags & _IO_EOF) != 0;
}