#include "runtime.h"


int main(
    int argc,
    char **argv)
{
    if (argc < 1 ||
        argv == 0)
    {
        return 1;
    }

    for (int index = 1;
         index < argc;
         ++index)
    {
        if (index > 1)
        {
            if (user_write_string(
                    USER_STDOUT,
                    " ") != 0)
            {
                return 2;
            }
        }

        if (argv[index] == 0 ||
            user_write_string(
                USER_STDOUT,
                argv[index]) != 0)
        {
            return 3;
        }
    }

    if (user_write_string(
            USER_STDOUT,
            "\n") != 0)
    {
        return 4;
    }

    return 0;
}