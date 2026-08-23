#include <stdbool.h>
#include <stdint.h>

#include "runtime.h"


static bool echo_strings_equal(
    const char *left,
    const char *right)
{
    if (left == 0 ||
        right == 0)
    {
        return false;
    }

    size_t index =
        0u;

    for (;;)
    {
        if (left[index] !=
            right[index])
        {
            return false;
        }

        if (left[index] ==
            '\0')
        {
            return true;
        }

        ++index;
    }
}


static int echo_write_arguments(
    int fd,
    int first,
    int end,
    char **argv)
{
    if (argv == 0)
        return -1;

    for (int index = first;
         index < end;
         ++index)
    {
        if (argv[index] == 0)
            return -1;

        /*
         * Separate arguments with exactly one space.
         */
        if (index > first)
        {
            if (user_write_string(
                    fd,
                    " ") != 0)
            {
                return -1;
            }
        }

        if (user_write_string(
                fd,
                argv[index]) != 0)
        {
            return -1;
        }
    }

    /*
     * echo always terminates its output with a newline.
     */
    if (user_write_string(
            fd,
            "\n") != 0)
    {
        return -1;
    }

    return 0;
}


static int echo_write_redirected(
    int argc,
    char **argv,
    int redirect_index,
    bool append)
{
    /*
     * For this small echo-local implementation the syntax is:
     *
     *     echo WORD... > FILE
     *     echo WORD... >> FILE
     *
     * The filename must be the final argument.
     */
    if (redirect_index + 1 >= argc ||
        redirect_index + 2 != argc ||
        argv[redirect_index + 1] == 0 ||
        argv[redirect_index + 1][0] == '\0')
    {
        user_write_string(
            USER_STDERR,
            "echo: invalid redirection\n");

        return 1;
    }

    const char *path =
        argv[redirect_index + 1];

    uint32_t flags =
        USER_OPEN_WRITE |
        USER_OPEN_CREATE;

    if (append)
    {
        flags |=
            USER_OPEN_APPEND;
    }
    else
    {
        /*
         * Shell-style '>' replaces previous contents.
         */
        flags |=
            USER_OPEN_TRUNC;
    }

    int32_t fd =
        user_open(
            path,
            flags);

    if (fd < 0)
    {
        user_write_string(
            USER_STDERR,
            "echo: cannot open ");

        user_write_string(
            USER_STDERR,
            path);

        user_write_string(
            USER_STDERR,
            "\n");

        return 2;
    }

    int write_result =
        echo_write_arguments(
            (int)fd,
            1,
            redirect_index,
            argv);

    /*
     * Always attempt to close the file, even if writing failed.
     */
    int32_t close_result =
        user_close(
            (int)fd);

    if (write_result != 0)
    {
        user_write_string(
            USER_STDERR,
            "echo: write failed\n");

        return 3;
    }

    if (close_result != 0)
    {
        user_write_string(
            USER_STDERR,
            "echo: close failed\n");

        return 4;
    }

    return 0;
}


int main(
    int argc,
    char **argv)
{
    if (argc < 1 ||
        argv == 0)
    {
        return 1;
    }

    /*
     * Look for echo-local output redirection.
     *
     * The shell currently tokenizes only on spaces/tabs, so:
     *
     *     echo hello > a.txt
     *
     * arrives as:
     *
     *     argv[1] = "hello"
     *     argv[2] = ">"
     *     argv[3] = "a.txt"
     *
     * This intentionally does not attempt to implement general
     * shell redirection yet.
     */
    for (int index = 1;
         index < argc;
         ++index)
    {
        if (argv[index] == 0)
            return 1;

        if (echo_strings_equal(
                argv[index],
                ">"))
        {
            return echo_write_redirected(
                argc,
                argv,
                index,
                false);
        }

        if (echo_strings_equal(
                argv[index],
                ">>"))
        {
            return echo_write_redirected(
                argc,
                argv,
                index,
                true);
        }
    }

    /*
     * Normal echo:
     *
     *     echo hello world
     *
     * still writes directly to stdout.
     */
    if (echo_write_arguments(
            USER_STDOUT,
            1,
            argc,
            argv) != 0)
    {
        return 2;
    }

    return 0;
}