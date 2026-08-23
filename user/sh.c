#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime.h"

#define SHELL_LINE_CAPACITY 128u
#define SHELL_ARGV_CAPACITY 16u
#define SHELL_PATH_CAPACITY 256u


static int shell_write(
    const char *string)
{
    return user_write_string(
        USER_STDOUT,
        string);
}


static int shell_write_error(
    const char *string)
{
    return user_write_string(
        USER_STDERR,
        string);
}


static int shell_write_character(
    char character)
{
    return user_write(
               USER_STDOUT,
               &character,
               1u) == 1
               ? 0
               : -1;
}


static size_t shell_string_length(
    const char *string)
{
    size_t length =
        0u;

    if (string == NULL)
        return 0u;

    while (string[length] !=
           '\0')
    {
        ++length;
    }

    return length;
}


static bool shell_string_ends_with_nex(
    const char *string)
{
    size_t length =
        shell_string_length(
            string);

    if (length < 4u)
        return false;

    return
        string[length - 4u] == '.' &&
        string[length - 3u] == 'n' &&
        string[length - 2u] == 'e' &&
        string[length - 1u] == 'x';
}


static bool shell_command_has_path(
    const char *command)
{
    if (command == NULL)
        return false;

    while (*command != '\0')
    {
        if (*command == '/')
        {
            return true;
        }

        ++command;
    }

    return false;
}


static bool shell_resolve_command(
    const char *command,
    char *path,
    size_t capacity)
{
    if (command == NULL ||
        path == NULL ||
        capacity == 0u)
    {
        return false;
    }

    /*
     * Explicit pathname:
     *
     *     /bin/foo.nex
     *     ./foo.nex
     *     dir/foo.nex
     *
     * Use literally.
     */
    if (shell_command_has_path(
            command))
    {
        size_t length =
            shell_string_length(
                command);

        if (length + 1u >
            capacity)
        {
            return false;
        }

        for (size_t index = 0;
             index <= length;
             ++index)
        {
            path[index] =
                command[index];
        }

        return true;
    }

    /*
     * Bare command:
     *
     *     echo      -> /bin/echo.nex
     *     echo.nex  -> /bin/echo.nex
     */
    static const char prefix[] =
        "/bin/";

    static const char suffix[] =
        ".nex";

    size_t position =
        0u;

    for (size_t index = 0;
         prefix[index] != '\0';
         ++index)
    {
        if (position + 1u >=
            capacity)
        {
            return false;
        }

        path[position++] =
            prefix[index];
    }

    for (size_t index = 0;
         command[index] != '\0';
         ++index)
    {
        if (position + 1u >=
            capacity)
        {
            return false;
        }

        path[position++] =
            command[index];
    }

    if (!shell_string_ends_with_nex(
            command))
    {
        for (size_t index = 0;
             suffix[index] != '\0';
             ++index)
        {
            if (position + 1u >=
                capacity)
            {
                return false;
            }

            path[position++] =
                suffix[index];
        }
    }

    path[position] =
        '\0';

    return true;
}


static int shell_write_unsigned(
    uint32_t value)
{
    char buffer[16];

    size_t position =
        sizeof(buffer);

    if (value == 0u)
    {
        return shell_write_character(
            '0');
    }

    while (value != 0u)
    {
        --position;

        buffer[position] =
            (char)(
                '0' +
                (value % 10u));

        value /=
            10u;
    }

    size_t length =
        sizeof(buffer) -
        position;

    return user_write(
               USER_STDOUT,
               &buffer[position],
               length) ==
               (int32_t)length
               ? 0
               : -1;
}


static int shell_write_integer(
    int value)
{
    if (value < 0)
    {
        if (shell_write_character(
                '-') != 0)
        {
            return -1;
        }

        uint32_t magnitude =
            (uint32_t)(
                -(value + 1));

        ++magnitude;

        return shell_write_unsigned(
            magnitude);
    }

    return shell_write_unsigned(
        (uint32_t)value);
}


static int shell_read_line(
    char *buffer,
    size_t capacity)
{
    if (buffer == NULL ||
        capacity == 0u)
    {
        return -1;
    }

    size_t length =
        0u;

    buffer[0] =
        '\0';

    for (;;)
    {
        char character =
            '\0';

        int32_t result =
            user_read(
                USER_STDIN,
                &character,
                1u);

        if (result < 0)
        {
            return -1;
        }

        if (result == 0)
        {
            user_yield();
            continue;
        }

        if (character == '\n' ||
            character == '\r')
        {
            buffer[length] =
                '\0';

            if (shell_write_character(
                    '\n') != 0)
            {
                return -1;
            }

            return 0;
        }

        if (character == '\b')
        {
            if (length == 0u)
            {
                continue;
            }

            --length;

            buffer[length] =
                '\0';

            if (shell_write_character(
                    '\b') != 0)
            {
                return -1;
            }

            continue;
        }

        unsigned char value =
            (unsigned char)
                character;

        bool accepted =
            (value >= 0x20u &&
             value <= 0x7eu) ||
            character == '\t';

        if (!accepted)
        {
            continue;
        }

        if (length >=
            capacity - 1u)
        {
            continue;
        }

        buffer[length++] =
            character;

        buffer[length] =
            '\0';

        if (shell_write_character(
                character) != 0)
        {
            return -1;
        }
    }
}


static bool shell_is_separator(
    char character)
{
    return
        character == ' ' ||
        character == '\t';
}


static int shell_split_arguments(
    char *line,
    char *argv[],
    size_t argv_capacity)
{
    if (line == NULL ||
        argv == NULL ||
        argv_capacity == 0u)
    {
        return -1;
    }

    size_t argc =
        0u;

    char *position =
        line;

    for (;;)
    {
        while (shell_is_separator(
                   *position))
        {
            ++position;
        }

        if (*position == '\0')
        {
            break;
        }

        if (argc >=
            argv_capacity)
        {
            return -1;
        }

        argv[argc++] =
            position;

        while (*position != '\0' &&
               !shell_is_separator(
                   *position))
        {
            ++position;
        }

        if (*position == '\0')
        {
            break;
        }

        *position++ =
            '\0';
    }

    argv[argc] =
        NULL;

    return (int)argc;
}


static int shell_run_command(
    char *line)
{
    char *argv[
        SHELL_ARGV_CAPACITY + 1u];

    int parsed_argc =
        shell_split_arguments(
            line,
            argv,
            SHELL_ARGV_CAPACITY);

    if (parsed_argc < 0)
    {
        shell_write_error(
            "sh: too many arguments\n");

        return 0;
    }

    if (parsed_argc == 0)
    {
        return 0;
    }

    char executable_path[
        SHELL_PATH_CAPACITY];

    if (!shell_resolve_command(
            argv[0],
            executable_path,
            sizeof(executable_path)))
    {
        shell_write_error(
            "sh: command path too long\n");

        return 0;
    }

    /*
     * argv[0] becomes the resolved executable path.
     *
     * This preserves the same argv[0] behavior whether the user types:
     *
     *     spawn-child
     *
     * or:
     *
     *     /bin/spawn-child.nex
     */
    argv[0] =
        executable_path;

    uint32_t argc =
        (uint32_t)
            parsed_argc;

    int32_t child_pid =
        user_spawn(
            executable_path,
            argc,
            (const char *const *)
                argv);

    if (child_pid <= 0)
    {
        shell_write_error(
            "sh: command not found: ");

        shell_write_error(
            executable_path);

        shell_write_error(
            "\n");

        return 0;
    }

    int status =
        0;

    for (;;)
    {
        int32_t wait_result =
            user_waitpid(
                (uint32_t)child_pid,
                &status);

        if (wait_result < 0)
        {
            shell_write_error(
                "sh: waitpid failed\n");

            return 0;
        }

        if (wait_result == 1)
        {
            break;
        }

        user_yield();
    }

    /*
     * Normal successful commands stay quiet.
     */
    if (status == 0)
    {
        return 0;
    }

    if (shell_write(
            "sh: exit status ") != 0)
    {
        return -1;
    }

    if (shell_write_integer(
            status) != 0)
    {
        return -1;
    }

    if (shell_write_character(
            '\n') != 0)
    {
        return -1;
    }

    return 0;
}


int main(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    if (shell_write(
            "\nMeaty shell v0\n") != 0)
    {
        return 1;
    }

    for (;;)
    {
        char line[
            SHELL_LINE_CAPACITY];

        if (shell_write(
                "meaty> ") != 0)
        {
            return 2;
        }

        if (shell_read_line(
                line,
                sizeof(line)) != 0)
        {
            shell_write_error(
                "\nsh: stdin failed\n");

            return 3;
        }

        if (shell_run_command(
                line) != 0)
        {
            return 4;
        }
    }
}