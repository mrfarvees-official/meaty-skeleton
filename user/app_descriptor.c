#include <stddef.h>
#include <stdint.h>

#include "runtime.h"
#include "app_descriptor.h"


#define APP_DESCRIPTOR_FILE_MAX 1024u


static size_t app_string_length(
    const char *string)
{
    size_t length = 0u;

    if (string == NULL)
        return 0u;

    while (string[length] != '\0')
        ++length;

    return length;
}


static int app_string_equal(
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


static int app_copy_string(
    char *destination,
    size_t capacity,
    const char *source)
{
    if (destination == NULL ||
        source == NULL ||
        capacity == 0u)
    {
        return -1;
    }

    size_t length =
        app_string_length(
            source);

    if (length + 1u >
        capacity)
    {
        return -1;
    }

    for (size_t index = 0u;
         index <= length;
         ++index)
    {
        destination[index] =
            source[index];
    }

    return 0;
}


static int app_is_space(
    char character)
{
    return
        character == ' ' ||
        character == '\t' ||
        character == '\r';
}


static char *app_trim_left(
    char *text)
{
    if (text == NULL)
        return NULL;

    while (app_is_space(
        *text))
    {
        ++text;
    }

    return text;
}


static void app_trim_right(
    char *text)
{
    if (text == NULL)
        return;

    size_t length =
        app_string_length(
            text);

    while (length != 0u &&
           app_is_space(
               text[length - 1u]))
    {
        --length;
    }

    text[length] =
        '\0';
}


static char *app_find_character(
    char *text,
    char wanted)
{
    if (text == NULL)
        return NULL;

    while (*text != '\0')
    {
        if (*text == wanted)
            return text;

        ++text;
    }

    return NULL;
}


static int app_path_valid(
    const char *path)
{
    if (path == NULL)
        return 0;

    /*
     * Application executable/icon locations are always
     * filesystem-absolute.
     */
    if (path[0] != '/')
        return 0;

    if (path[1] == '\0')
        return 0;

    return 1;
}


static int app_descriptor_parse(
    char *contents,
    app_descriptor_t *descriptor)
{
    if (contents == NULL ||
        descriptor == NULL)
    {
        return -1;
    }

    descriptor->name[0] =
        '\0';

    descriptor->executable[0] =
        '\0';

    descriptor->icon[0] =
        '\0';

    int in_application_section =
        0;

    int application_section_seen =
        0;

    int name_seen =
        0;

    int executable_seen =
        0;

    int icon_seen =
        0;

    int type_seen =
        0;

    char *cursor =
        contents;

    for (;;)
    {
        char *line =
            cursor;

        while (*cursor != '\0' &&
               *cursor != '\n')
        {
            ++cursor;
        }

        if (*cursor == '\n')
        {
            *cursor =
                '\0';

            ++cursor;
        }

        char *text =
            app_trim_left(
                line);

        app_trim_right(
            text);

        /*
         * Empty line or comment.
         */
        if (text[0] == '\0' ||
            text[0] == '#' ||
            text[0] == ';')
        {
            if (*cursor == '\0')
                break;

            continue;
        }

        /*
         * Section header.
         */
        if (text[0] == '[')
        {
            if (app_string_equal(
                    text,
                    "[Application]"))
            {
                if (application_section_seen)
                    return -1;

                application_section_seen =
                    1;

                in_application_section =
                    1;
            }
            else
            {
                /*
                 * Future sections may exist, but their keys
                 * aren't part of the Application section.
                 */
                in_application_section =
                    0;
            }

            if (*cursor == '\0')
                break;

            continue;
        }

        /*
         * Ignore keys outside [Application].
         */
        if (!in_application_section)
        {
            if (*cursor == '\0')
                break;

            continue;
        }

        char *equals =
            app_find_character(
                text,
                '=');

        if (equals == NULL)
            return -1;

        *equals =
            '\0';

        char *key =
            app_trim_left(
                text);

        char *value =
            app_trim_left(
                equals + 1);

        app_trim_right(
            key);

        app_trim_right(
            value);

        if (key[0] == '\0' ||
            value[0] == '\0')
        {
            return -1;
        }

        if (app_string_equal(
                key,
                "Name"))
        {
            if (name_seen)
                return -1;

            if (app_copy_string(
                    descriptor->name,
                    sizeof(
                        descriptor->name),
                    value) != 0)
            {
                return -1;
            }

            name_seen =
                1;
        }
        else if (app_string_equal(
                     key,
                     "Exec"))
        {
            if (executable_seen ||
                !app_path_valid(
                    value))
            {
                return -1;
            }

            if (app_copy_string(
                    descriptor->executable,
                    sizeof(
                        descriptor->executable),
                    value) != 0)
            {
                return -1;
            }

            executable_seen =
                1;
        }
        else if (app_string_equal(
                     key,
                     "Icon"))
        {
            if (icon_seen ||
                !app_path_valid(
                    value))
            {
                return -1;
            }

            if (app_copy_string(
                    descriptor->icon,
                    sizeof(
                        descriptor->icon),
                    value) != 0)
            {
                return -1;
            }

            icon_seen =
                1;
        }
        else if (app_string_equal(
                     key,
                     "Type"))
        {
            if (type_seen)
                return -1;

            if (!app_string_equal(
                    value,
                    "Application"))
            {
                return -1;
            }

            type_seen =
                1;
        }

        /*
         * Unknown keys are deliberately ignored for forward
         * compatibility.
         */

        if (*cursor == '\0')
            break;
    }

    if (!application_section_seen ||
        !name_seen ||
        !executable_seen ||
        !icon_seen ||
        !type_seen)
    {
        return -1;
    }

    return 0;
}


int app_descriptor_load(
    const char *path,
    app_descriptor_t *descriptor)
{
    if (path == NULL ||
        descriptor == NULL)
    {
        return -1;
    }

    int32_t fd =
        user_open(
            path,
            USER_OPEN_READ);

    if (fd < 0)
        return -1;

    char contents[
        APP_DESCRIPTOR_FILE_MAX +
        1u];

    size_t used =
        0u;

    for (;;)
    {
        if (used ==
            APP_DESCRIPTOR_FILE_MAX)
        {
            /*
             * Probe once more to distinguish an exactly-full
             * descriptor from an oversized descriptor.
             */
            char extra;

            int32_t extra_read =
                user_read(
                    fd,
                    &extra,
                    1u);

            user_close(
                fd);

            if (extra_read != 0)
                return -1;

            break;
        }

        int32_t bytes_read =
            user_read(
                fd,
                contents + used,
                APP_DESCRIPTOR_FILE_MAX -
                    used);

        if (bytes_read < 0)
        {
            user_close(
                fd);

            return -1;
        }

        if (bytes_read == 0)
            break;

        if ((size_t)bytes_read >
            APP_DESCRIPTOR_FILE_MAX -
                used)
        {
            user_close(
                fd);

            return -1;
        }

        used +=
            (size_t)bytes_read;
    }

    if (user_close(
            fd) != 0)
    {
        return -1;
    }

    if (used == 0u)
        return -1;

    contents[used] =
        '\0';

    return app_descriptor_parse(
        contents,
        descriptor);
}