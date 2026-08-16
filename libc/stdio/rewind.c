#include <stdio.h>

void rewind(FILE *stream)
{
    if (stream == NULL)
        return;

    if (fseek(
            stream,
            0,
            SEEK_SET) == 0)
    {
        /**
         * rewind() clear both EOF and error state on success.
         */
        clearerr(stream);
    }
}