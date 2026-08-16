#include <stdio.h>
#include <stddef.h>

size_t fread(
    void *ptr,
    size_t size,
    size_t nmemb,
    FILE *stream)
{
    unsigned char *destination;
    size_t total_bytes;
    size_t bytes_read;
    int c;

    if (size == 0 || nmemb == 0)
        return 0;

    if (ptr == NULL || stream == NULL)
        return 0;

    /**
     * Avoid overflowing size_t when caclulating the total byte
     * count.
     */
    if (nmemb > ((size_t) -1) / size)
        return 0;

    destination = (unsigned char *)ptr;
    total_bytes = size * nmemb;
    bytes_read = 0;

    while (bytes_read < total_bytes)
    {
        c = fgetc(stream);

        if (c == EOF)
            break;

        destination[bytes_read++] = (unsigned char)c;
    }

    /**
     * fread() reports complete elements, not bytes.
     * 
     * Any paritial final element has still been transferred into
     * the destination buffer but is not inculded in the return
     * value.
     */
    return bytes_read / size;
}