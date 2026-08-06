#include <string.h>

char *strcat(char *restrict destination, const char *restrict source)
{
    char *result = destination;

    while (*destination != '\0')
        destination++;

    while ((*destination++ = *source++) != '\0')
        ;

    return result;
}