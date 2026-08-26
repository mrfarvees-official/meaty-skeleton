#include <stddef.h>

#include "runtime.h"


int main(
    int argc,
    char **argv)
{
    if (argc != 2 ||
        argv == NULL ||
        argv[1] == NULL)
    {
        user_write_string(
            USER_STDERR,
            "usage: mkdir DIRECTORY\n");

        return 1;
    }

    if (user_mkdir(
            argv[1]) != 0)
    {
        user_write_string(
            USER_STDERR,
            "mkdir: failed\n");

        return 2;
    }

    return 0;
}