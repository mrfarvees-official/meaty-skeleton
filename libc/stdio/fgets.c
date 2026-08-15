#include <stdio.h>
#include <stddef.h>

char *fgets(char *str, int size, FILE *stream)
{
    if (str == NULL || stream == NULL || size <= 0)
        return NULL;

    int pos = 0;

    while (pos < size - 1)
    {
        int c = fgetc(stream);

        if (c == EOF)
        {
            if (pos == 0)
                return NULL;

            break;
        }

        str[pos++] = (char)c;

        if (c == '\n')
            break;
    }

    str[pos] = '\0';

    return str;
}