#include <stddef.h>

static int string_equal(
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
        if (*left !=
            *right)
        {
            return 0;
        }

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
    if (argc != 3)
        return 70;

    if (argv == NULL ||
        argv[0] == NULL ||
        argv[1] == NULL ||
        argv[2] == NULL ||
        argv[3] != NULL)
    {
        return 71;
    }

    if (!string_equal(
            argv[0],
            "/bin/spawn-child.nex") ||
        !string_equal(
            argv[1],
            "alpha") ||
        !string_equal(
            argv[2],
            "beta"))
    {
        return 72;
    }

    /*
     * P1F success status.
     */
    return 73;
}