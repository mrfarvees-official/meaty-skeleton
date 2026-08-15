#include <stdio.h>
#include <stddef.h>

int fputs(const char *string, FILE *stream)
{
    if (string == NULL || stream == NULL) 
    {
        if (stream != NULL)
            stream->flags |= _IO_ERROR;

        return EOF;
    }

    while (*string != '\0')
    {
        if (fputc((unsigned char)*string, stream) == EOF)
            return EOF;

        ++string;
    }

    return 0;
}