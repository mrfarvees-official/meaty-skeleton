#include <stddef.h>

#include "runtime.h"
#include "app_descriptor.h"


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
            "usage: appinfo FILE.app\n");

        return 1;
    }

    app_descriptor_t application;

    if (app_descriptor_load(
            argv[1],
            &application) != 0)
    {
        user_write_string(
            USER_STDERR,
            "appinfo: invalid application descriptor\n");

        return 2;
    }

    user_write_string(
        USER_STDOUT,
        "Name: ");

    user_write_string(
        USER_STDOUT,
        application.name);

    user_write_string(
        USER_STDOUT,
        "\nExec: ");

    user_write_string(
        USER_STDOUT,
        application.executable);

    user_write_string(
        USER_STDOUT,
        "\nIcon: ");

    user_write_string(
        USER_STDOUT,
        application.icon);

    user_write_string(
        USER_STDOUT,
        "\n");

    return 0;
}