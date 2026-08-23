#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime.h"

#define SHELL_LINE_CAPACITY 73u
#define SHELL_ARGV_CAPACITY 16u
#define SHELL_PATH_CAPACITY 256u

#define SHELL_HISTORY_CAPACITY 16u

#define SHELL_PROMPT "meaty> "

static char shell_history[SHELL_HISTORY_CAPACITY]
                         [SHELL_LINE_CAPACITY];

static size_t shell_history_count =
    0u;

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

static void shell_copy_string(
    char *destination,
    size_t capacity,
    const char *source)
{
    if (destination == NULL ||
        capacity == 0u)
    {
        return;
    }

    size_t index =
        0u;

    if (source != NULL)
    {
        while (source[index] != '\0' &&
               index + 1u < capacity)
        {
            destination[index] =
                source[index];

            ++index;
        }
    }

    destination[index] =
        '\0';
}

static bool shell_strings_equal(
    const char *left,
    const char *right)
{
    if (left == NULL ||
        right == NULL)
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

static void shell_history_add(
    const char *line)
{
    if (line == NULL ||
        line[0] == '\0')
    {
        return;
    }

    /*
     * Do not store the same command twice consecutively.
     */
    if (shell_history_count != 0u &&
        shell_strings_equal(
            shell_history[shell_history_count - 1u],
            line))
    {
        return;
    }

    if (shell_history_count <
        SHELL_HISTORY_CAPACITY)
    {
        shell_copy_string(
            shell_history[shell_history_count],
            SHELL_LINE_CAPACITY,
            line);

        ++shell_history_count;

        return;
    }

    /*
     * Small fixed history: discard the oldest command.
     */
    for (size_t index = 1u;
         index <
         SHELL_HISTORY_CAPACITY;
         ++index)
    {
        shell_copy_string(
            shell_history[index - 1u],
            SHELL_LINE_CAPACITY,
            shell_history[index]);
    }

    shell_copy_string(
        shell_history[SHELL_HISTORY_CAPACITY - 1u],
        SHELL_LINE_CAPACITY,
        line);
}

static bool shell_string_ends_with_nex(
    const char *string)
{
    size_t length =
        shell_string_length(
            string);

    if (length < 4u)
        return false;

    return string[length - 4u] == '.' &&
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
            return true;

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

        for (size_t index = 0u;
             index <= length;
             ++index)
        {
            path[index] =
                command[index];
        }

        return true;
    }

    static const char prefix[] =
        "/bin/";

    static const char suffix[] =
        ".nex";

    size_t position =
        0u;

    for (size_t index = 0u;
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

    for (size_t index = 0u;
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
        for (size_t index = 0u;
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
            (char)('0' +
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
            (uint32_t)(-(value + 1));

        ++magnitude;

        return shell_write_unsigned(
            magnitude);
    }

    return shell_write_unsigned(
        (uint32_t)value);
}

/*
 * Redraw the complete editable line, then position the VGA cursor at
 * the logical insertion position.
 *
 * terminal '\r' moves to column zero without destroying contents.
 */
static int shell_redraw_line(
    const char *buffer,
    size_t length,
    size_t cursor,
    size_t old_rendered_length)
{
    if (shell_write_character(
            '\r') != 0)
    {
        return -1;
    }

    if (shell_write(
            SHELL_PROMPT) != 0)
    {
        return -1;
    }

    if (length != 0u)
    {
        int32_t result =
            user_write(
                USER_STDOUT,
                buffer,
                length);

        if (result !=
            (int32_t)length)
        {
            return -1;
        }
    }

    /*
     * Erase characters left over from a previously longer line.
     */
    for (size_t index = length;
         index < old_rendered_length;
         ++index)
    {
        if (shell_write_character(
                ' ') != 0)
        {
            return -1;
        }
    }

    /*
     * Return to the beginning and walk forward to the desired
     * insertion point.
     */
    if (shell_write_character(
            '\r') != 0)
    {
        return -1;
    }

    if (shell_write(
            SHELL_PROMPT) != 0)
    {
        return -1;
    }

    if (cursor != 0u)
    {
        int32_t result =
            user_write(
                USER_STDOUT,
                buffer,
                cursor);

        if (result !=
            (int32_t)cursor)
        {
            return -1;
        }
    }

    return 0;
}

static int shell_read_byte(
    char *character)
{
    if (character == NULL)
        return -1;

    for (;;)
    {
        int32_t result =
            user_read(
                USER_STDIN,
                character,
                1u);

        if (result < 0)
        {
            return -1;
        }

        if (result == 1)
        {
            return 0;
        }

        user_yield();
    }
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

    size_t cursor =
        0u;

    size_t rendered_length =
        0u;

    int history_position =
        -1;

    char draft[SHELL_LINE_CAPACITY];

    draft[0] =
        '\0';

    buffer[0] =
        '\0';

    if (shell_write(
            SHELL_PROMPT) != 0)
    {
        return -1;
    }

    for (;;)
    {
        char character =
            '\0';

        if (shell_read_byte(
                &character) != 0)
        {
            return -1;
        }

        /*
         * ----------------------------------------------------------
         * TERMINAL ESCAPE SEQUENCES
         * ----------------------------------------------------------
         */
        if ((unsigned char)character ==
            0x1bu)
        {
            char second =
                '\0';

            if (shell_read_byte(
                    &second) != 0)
            {
                return -1;
            }

            if (second != '[')
            {
                continue;
            }

            char third =
                '\0';

            if (shell_read_byte(
                    &third) != 0)
            {
                return -1;
            }

            /*
             * Left.
             */
            if (third == 'D')
            {
                if (cursor != 0u)
                {
                    --cursor;

                    if (shell_redraw_line(
                            buffer,
                            length,
                            cursor,
                            rendered_length) != 0)
                    {
                        return -1;
                    }
                }

                continue;
            }

            /*
             * Right.
             */
            if (third == 'C')
            {
                if (cursor < length)
                {
                    ++cursor;

                    if (shell_redraw_line(
                            buffer,
                            length,
                            cursor,
                            rendered_length) != 0)
                    {
                        return -1;
                    }
                }

                continue;
            }

            /*
             * Home.
             */
            if (third == 'H')
            {
                cursor =
                    0u;

                if (shell_redraw_line(
                        buffer,
                        length,
                        cursor,
                        rendered_length) != 0)
                {
                    return -1;
                }

                continue;
            }

            /*
             * End.
             */
            if (third == 'F')
            {
                cursor =
                    length;

                if (shell_redraw_line(
                        buffer,
                        length,
                        cursor,
                        rendered_length) != 0)
                {
                    return -1;
                }

                continue;
            }

            /*
             * Previous command.
             */
            if (third == 'A')
            {
                if (shell_history_count ==
                    0u)
                {
                    continue;
                }

                if (history_position < 0)
                {
                    shell_copy_string(
                        draft,
                        sizeof(draft),
                        buffer);

                    history_position =
                        (int)shell_history_count -
                        1;
                }
                else if (history_position >
                         0)
                {
                    --history_position;
                }

                size_t old_length =
                    rendered_length;

                shell_copy_string(
                    buffer,
                    capacity,
                    shell_history[history_position]);

                length =
                    shell_string_length(
                        buffer);

                cursor =
                    length;

                if (shell_redraw_line(
                        buffer,
                        length,
                        cursor,
                        old_length) != 0)
                {
                    return -1;
                }

                rendered_length =
                    length;

                continue;
            }

            /*
             * Next command.
             */
            if (third == 'B')
            {
                if (history_position < 0)
                {
                    continue;
                }

                size_t old_length =
                    rendered_length;

                if ((size_t)(history_position + 1) <
                    shell_history_count)
                {
                    ++history_position;

                    shell_copy_string(
                        buffer,
                        capacity,
                        shell_history[history_position]);
                }
                else
                {
                    history_position =
                        -1;

                    shell_copy_string(
                        buffer,
                        capacity,
                        draft);
                }

                length =
                    shell_string_length(
                        buffer);

                cursor =
                    length;

                if (shell_redraw_line(
                        buffer,
                        length,
                        cursor,
                        old_length) != 0)
                {
                    return -1;
                }

                rendered_length =
                    length;

                continue;
            }

            /*
             * Delete:
             *
             * ESC [ 3 ~
             */
            if (third == '3')
            {
                char fourth =
                    '\0';

                if (shell_read_byte(
                        &fourth) != 0)
                {
                    return -1;
                }

                if (fourth != '~')
                {
                    continue;
                }

                if (cursor >= length)
                {
                    continue;
                }

                size_t old_length =
                    rendered_length;

                for (size_t index = cursor;
                     index < length;
                     ++index)
                {
                    buffer[index] =
                        buffer[index + 1u];
                }

                --length;

                if (shell_redraw_line(
                        buffer,
                        length,
                        cursor,
                        old_length) != 0)
                {
                    return -1;
                }

                rendered_length =
                    length;

                history_position =
                    -1;

                continue;
            }

            continue;
        }

        /*
         * ----------------------------------------------------------
         * ENTER
         * ----------------------------------------------------------
         */
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

            shell_history_add(
                buffer);

            return 0;
        }

        /*
         * ----------------------------------------------------------
         * BACKSPACE
         * ----------------------------------------------------------
         */
        if (character == '\b')
        {
            if (cursor == 0u)
            {
                continue;
            }

            size_t old_length =
                rendered_length;

            for (size_t index =
                     cursor - 1u;
                 index < length;
                 ++index)
            {
                buffer[index] =
                    buffer[index + 1u];
            }

            --cursor;
            --length;

            if (shell_redraw_line(
                    buffer,
                    length,
                    cursor,
                    old_length) != 0)
            {
                return -1;
            }

            rendered_length =
                length;

            history_position =
                -1;

            continue;
        }

        /*
         * ----------------------------------------------------------
         * PRINTABLE INPUT
         * ----------------------------------------------------------
         */
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

        /*
         * Insert at current cursor position.
         */
        for (size_t index = length;
             index > cursor;
             --index)
        {
            buffer[index] =
                buffer[index - 1u];
        }

        buffer[cursor] =
            character;

        ++cursor;
        ++length;

        buffer[length] =
            '\0';

        if (shell_redraw_line(
                buffer,
                length,
                cursor,
                rendered_length) != 0)
        {
            return -1;
        }

        rendered_length =
            length;

        history_position =
            -1;
    }
}

static bool shell_is_separator(
    char character)
{
    return character == ' ' ||
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

static int shell_run_builtin(
    int argc,
    char *argv[],
    bool *handled)
{
    if (handled == NULL)
        return -1;

    *handled =
        false;

    if (argc <= 0 ||
        argv == NULL)
    {
        return 0;
    }

    /*
     * pwd
     */
    if (shell_strings_equal(
            argv[0],
            "pwd"))
    {
        *handled =
            true;

        if (argc != 1)
        {
            shell_write_error(
                "pwd: too many arguments\n");

            return 0;
        }

        char cwd[SHELL_PATH_CAPACITY];

        if (user_getcwd(
                cwd,
                sizeof(cwd)) < 0)
        {
            shell_write_error(
                "pwd: cannot get current directory\n");

            return 0;
        }

        if (shell_write(
                cwd) != 0 ||
            shell_write_character(
                '\n') != 0)
        {
            return -1;
        }

        return 0;
    }

    /*
     * cd
     *
     * With no HOME/environment support yet, plain "cd"
     * returns to root.
     */
    if (shell_strings_equal(
            argv[0],
            "cd"))
    {
        *handled =
            true;

        if (argc > 2)
        {
            shell_write_error(
                "cd: too many arguments\n");

            return 0;
        }

        const char *path =
            argc == 1
                ? "/"
                : argv[1];

        if (user_chdir(
                path) != 0)
        {
            shell_write_error(
                "cd: cannot change directory: ");

            shell_write_error(
                path);

            shell_write_error(
                "\n");
        }

        return 0;
    }

    return 0;
}

static int shell_run_command(
    char *line)
{
    char *argv[SHELL_ARGV_CAPACITY + 1u];

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

    /*
     * Builtins run inside the shell process.
     */
    bool builtin_handled =
        false;

    int builtin_result =
        shell_run_builtin(
            parsed_argc,
            argv,
            &builtin_handled);

    if (builtin_result != 0)
    {
        return builtin_result;
    }

    if (builtin_handled)
    {
        return 0;
    }

    char executable_path[SHELL_PATH_CAPACITY];

    if (!shell_resolve_command(
            argv[0],
            executable_path,
            sizeof(executable_path)))
    {
        shell_write_error(
            "sh: command path too long\n");

        return 0;
    }

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
        char line[SHELL_LINE_CAPACITY];

        if (shell_read_line(
                line,
                sizeof(line)) != 0)
        {
            shell_write_error(
                "\nsh: input failed\n");

            return 3;
        }

        if (shell_run_command(
                line) != 0)
        {
            return 4;
        }
    }
}