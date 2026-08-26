#include <stddef.h>
#include <stdint.h>

#include "runtime.h"
#include "link_descriptor.h"


#define LINK_DESCRIPTOR_FILE_MAX 512u


static size_t link_string_length(
    const char *string)
{
    size_t length =
        0u;

    if (string == NULL)
        return 0u;

    while (string[length] != '\0')
        ++length;

    return length;
}


static int link_string_equal(
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


static int link_copy_string(
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
        link_string_length(
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


static int link_is_space(
    char character)
{
    return
        character == ' ' ||
        character == '\t' ||
        character == '\r';
}


static char *link_trim_left(
    char *text)
{
    if (text == NULL)
        return NULL;

    while (link_is_space(
        *text))
    {
        ++text;
    }

    return text;
}


static void link_trim_right(
    char *text)
{
    if (text == NULL)
        return;

    size_t length =
        link_string_length(
            text);

    while (length != 0u &&
           link_is_space(
               text[length - 1u]))
    {
        --length;
    }

    text[length] =
        '\0';
}


static char *link_find_character(
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


static int link_application_path_valid(
    const char *path)
{
    if (path == NULL)
        return 0;

    /*
     * Links always point to canonical application
     * descriptors by absolute path.
     */
    if (path[0] != '/')
        return 0;

    if (path[1] == '\0')
        return 0;

    size_t length =
        link_string_length(
            path);

    /*
     * Require the canonical .app suffix.
     */
    if (length < 5u)
        return 0;

    if (path[length - 4u] != '.' ||
        path[length - 3u] != 'a' ||
        path[length - 2u] != 'p' ||
        path[length - 1u] != 'p')
    {
        return 0;
    }

    return 1;
}


static int link_descriptor_parse(
    char *contents,
    link_descriptor_t *descriptor)
{
    if (contents == NULL ||
        descriptor == NULL)
    {
        return -1;
    }

    descriptor->application[0] =
        '\0';

    int link_section_seen =
        0;

    int in_link_section =
        0;

    int application_seen =
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
            link_trim_left(
                line);

        link_trim_right(
            text);

        /*
         * Blank line or comment.
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
            if (link_string_equal(
                    text,
                    "[Link]"))
            {
                if (link_section_seen)
                    return -1;

                link_section_seen =
                    1;

                in_link_section =
                    1;
            }
            else
            {
                in_link_section =
                    0;
            }

            if (*cursor == '\0')
                break;

            continue;
        }

        /*
         * Ignore unrelated future sections.
         */
        if (!in_link_section)
        {
            if (*cursor == '\0')
                break;

            continue;
        }

        char *equals =
            link_find_character(
                text,
                '=');

        if (equals == NULL)
            return -1;

        *equals =
            '\0';

        char *key =
            link_trim_left(
                text);

        char *value =
            link_trim_left(
                equals + 1);

        link_trim_right(
            key);

        link_trim_right(
            value);

        if (key[0] == '\0' ||
            value[0] == '\0')
        {
            return -1;
        }

        if (link_string_equal(
                key,
                "Application"))
        {
            if (application_seen)
                return -1;

            if (!link_application_path_valid(
                    value))
            {
                return -1;
            }

            if (link_copy_string(
                    descriptor->application,
                    sizeof(
                        descriptor->application),
                    value) != 0)
            {
                return -1;
            }

            application_seen =
                1;
        }

        /*
         * Unknown keys are ignored so the format can grow later.
         */

        if (*cursor == '\0')
            break;
    }

    if (!link_section_seen ||
        !application_seen)
    {
        return -1;
    }

    return 0;
}


int link_descriptor_load(
    const char *path,
    link_descriptor_t *descriptor)
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
        LINK_DESCRIPTOR_FILE_MAX +
        1u];

    size_t used =
        0u;

    for (;;)
    {
        if (used ==
            LINK_DESCRIPTOR_FILE_MAX)
        {
            char extra;

            int32_t extra_read =
                user_read(
                    fd,
                    &extra,
                    1u);

            user_close(
                fd);

            /*
             * Exactly 512 bytes is acceptable.
             * Anything larger is rejected.
             */
            if (extra_read != 0)
                return -1;

            break;
        }

        int32_t bytes_read =
            user_read(
                fd,
                contents + used,
                LINK_DESCRIPTOR_FILE_MAX -
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
            LINK_DESCRIPTOR_FILE_MAX -
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

    return link_descriptor_parse(
        contents,
        descriptor);
}