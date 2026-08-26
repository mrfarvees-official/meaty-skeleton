#include <stddef.h>
#include <stdint.h>

#include "runtime.h"

#define LS_PATH_MAX 256u


static int string_equals(
    const char *left,
    const char *right)
{
    if (left == NULL ||
        right == NULL)
    {
        return 0;
    }

    while (*left != '\0' &&
           *right != '\0')
    {
        if (*left != *right)
            return 0;

        ++left;
        ++right;
    }

    return
        *left == '\0' &&
        *right == '\0';
}


int main(
    int argc,
    char **argv)
{
    const char *path =
        NULL;

    char cwd[LS_PATH_MAX];

    if (argc >= 2)
    {
        path =
            argv[1];
    }
    else
    {
        if (user_getcwd(
                cwd,
                sizeof(cwd)) < 0)
        {
            user_write_string(
                USER_STDERR,
                "ls: getcwd failed\n");

            return 1;
        }

        path =
            cwd;
    }

    int32_t fd =
        user_open(
            path,
            USER_OPEN_READ);

    if (fd < 0)
    {
        user_write_string(
            USER_STDERR,
            "ls: cannot open directory\n");

        return 1;
    }

    for (;;)
    {
        user_dirent_t entry;

        int32_t result =
            user_readdir(
                fd,
                &entry);

        if (result == 0)
            break;

        if (result < 0)
        {
            user_write_string(
                USER_STDERR,
                "ls: readdir failed\n");

            user_close(
                fd);

            return 1;
        }

        /*
         * Hide ext2's structural entries from ordinary listing.
         */
        if (string_equals(
                entry.name,
                ".") ||
            string_equals(
                entry.name,
                ".."))
        {
            continue;
        }

        user_write_string(
            USER_STDOUT,
            entry.name);

        if (entry.type ==
            USER_DIRENT_TYPE_DIRECTORY)
        {
            user_write_string(
                USER_STDOUT,
                "/");
        }

        user_write_string(
            USER_STDOUT,
            "\n");
    }

    user_close(
        fd);

    return 0;
}