#include <stdint.h>

#include "runtime.h"


int main(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    char character =
        '\f';

    if (user_write(
            USER_STDOUT,
            &character,
            1u) != 1)
    {
        return 1;
    }

    return 0;
}