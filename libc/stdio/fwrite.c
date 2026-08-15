#include <stdio.h>
#include <stddef.h>

size_t fwrite(
    const void *ptr,
    size_t size,
    size_t nmemb,
    FILE *stream)
{
    if (size == 0 || nmemb == 0) 
        return 0;

    if (ptr == NULL || stream == NULL)
    {
        if (stream != NULL)
            stream->flags |= _IO_ERROR;
        
        return 0;
    }

    if (!(stream->flags & _IO_WRITE))
    {
        stream->flags |= _IO_ERROR;
        return 0;
    }

    const unsigned char *data =
        (const unsigned char *)ptr;

    size_t completed = 0;

    for (size_t element = 0; element < nmemb; ++element)
    {
        for (size_t byte = 0; byte < size; ++byte)
        {
            size_t offset = element * size + byte;

            if (fputc(data[offset], stream) == EOF)
                return completed;
        }

        ++completed;
    }

    return completed;
}