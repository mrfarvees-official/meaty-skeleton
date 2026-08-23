#include <stdint.h>

#include "runtime.h"


static int touch_file(
    const char *path)
{
    int32_t fd =
        user_open(
            path,
            USER_OPEN_WRITE |
            USER_OPEN_CREATE);

    if (fd < 0)
    {
        user_write_string(
            USER_STDERR,
            "touch: cannot create ");

        user_write_string(
            USER_STDERR,
            path);

        user_write_string(
            USER_STDERR,
            "\n");

        return 1;
    }

    if (user_close(
            fd) != 0)
    {
        return 2;
    }

    return 0;
}


int main(
    int argc,
    char **argv)
{
    if (argc < 2 ||
        argv == 0)
    {
        user_write_string(
            USER_STDERR,
            "usage: touch FILE...\n");

        return 1;
    }

    for (int index = 1;
         index < argc;
         ++index)
    {
        if (argv[index] == 0)
        {
            return 1;
        }

        int result =
            touch_file(
                argv[index]);

        if (result != 0)
        {
            return result;
        }
    }

    return 0;
}