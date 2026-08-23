#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime.h"

#define SHELL_LINE_CAPACITY 128u
#define SHELL_ARGV_CAPACITY 16u


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
    int32_t result =
        user_write(
            USER_STDOUT,
            &character,
            1u);

    return result == 1
               ? 0
               : -1;
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
        uint32_t digit =
            value % 10u;

        --position;

        buffer[position] =
            (char)('0' + digit);

        value /= 10u;
    }

    size_t length =
        sizeof(buffer) -
        position;

    int32_t result =
        user_write(
            USER_STDOUT,
            &buffer[position],
            length);

    return result ==
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

        /*
         * Avoid signed overflow for INT_MIN.
         */
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


/*
 * Read one editable command line.
 *
 * Editing intentionally lives in userspace:
 *
 *     Enter      submits
 *     Backspace  erases one character
 *     printable ASCII and Tab are accepted
 *
 * The kernel read syscall only supplies keyboard characters.
 */
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

            /*
             * terminal_putchar('\b') already moves backward
             * and erases the previous VGA cell.
             */
            if (shell_write_character(
                    '\b') != 0)
            {
                return -1;
            }

            continue;
        }

        unsigned char value =
            (unsigned char)character;

        bool accepted =
            (value >= 0x20u &&
             value <= 0x7eu) ||
            character == '\t';

        if (!accepted)
        {
            continue;
        }

        /*
         * Always reserve one byte for '\0'.
         */
        if (length >=
            capacity - 1u)
        {
            continue;
        }

        buffer[length] =
            character;

        ++length;

        buffer[length] =
            '\0';

        if (shell_write_character(
                character) != 0)
        {
            return -1;
        }
    }
}


static int shell_is_separator(
    char character)
{
    return
        character == ' ' ||
        character == '\t';
}


/*
 * Split the command line in place.
 *
 * Example:
 *
 *     /bin/foo.nex alpha beta
 *
 * becomes:
 *
 *     argv[0] = "/bin/foo.nex"
 *     argv[1] = "alpha"
 *     argv[2] = "beta"
 *     argv[3] = NULL
 *
 * Returns argc.
 *
 * If there are more than SHELL_ARGV_CAPACITY arguments,
 * returns -1.
 */
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

        argv[argc] =
            position;

        ++argc;

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

        *position =
            '\0';

        ++position;
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

    uint32_t argc =
        (uint32_t)parsed_argc;

    int32_t child_pid =
        user_spawn(
            argv[0],
            argc,
            (const char *const *)argv);

    if (child_pid <= 0)
    {
        shell_write_error(
            "sh: spawn failed\n");

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