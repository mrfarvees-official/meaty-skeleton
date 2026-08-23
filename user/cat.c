#include <stddef.h>
#include <stdint.h>

#include "runtime.h"

#define CAT_BUFFER_SIZE 128u


static int cat_file(
    const char *path)
{
    int32_t fd =
        user_open(
            path,
            USER_OPEN_READ);

    if (fd < 0)
    {
        user_write_string(
            USER_STDERR,
            "cat: cannot open ");

        user_write_string(
            USER_STDERR,
            path);

        user_write_string(
            USER_STDERR,
            "\n");

        return 1;
    }

    char buffer[
        CAT_BUFFER_SIZE];

    for (;;)
    {
        int32_t bytes_read =
            user_read(
                fd,
                buffer,
                sizeof(buffer));

        if (bytes_read < 0)
        {
            user_write_string(
                USER_STDERR,
                "cat: read failed\n");

            user_close(
                fd);

            return 2;
        }

        if (bytes_read == 0)
        {
            break;
        }

        size_t written =
            0u;

        while (written <
               (size_t)bytes_read)
        {
            int32_t result =
                user_write(
                    USER_STDOUT,
                    buffer + written,
                    (size_t)bytes_read -
                        written);

            if (result <= 0)
            {
                user_close(
                    fd);

                return 3;
            }

            written +=
                (size_t)result;
        }
    }

    if (user_close(
            fd) != 0)
    {
        return 4;
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
            "usage: cat FILE...\n");

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
            cat_file(
                argv[index]);

        if (result != 0)
        {
            return result;
        }
    }

    return 0;
}