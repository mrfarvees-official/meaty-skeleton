#include <stddef.h>

#include "runtime.h"
#include "link_descriptor.h"
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
            "usage: linkinfo FILE.link\n");

        return 1;
    }

    link_descriptor_t link;

    if (link_descriptor_load(
            argv[1],
            &link) != 0)
    {
        user_write_string(
            USER_STDERR,
            "linkinfo: invalid link descriptor\n");

        return 2;
    }

    app_descriptor_t application;

    if (app_descriptor_load(
            link.application,
            &application) != 0)
    {
        user_write_string(
            USER_STDERR,
            "linkinfo: target application is invalid\n");

        return 3;
    }

    user_write_string(
        USER_STDOUT,
        "Application: ");

    user_write_string(
        USER_STDOUT,
        link.application);

    user_write_string(
        USER_STDOUT,
        "\nName: ");

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