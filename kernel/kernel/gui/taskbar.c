#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/image.h>
#include <kernel/gui/painter.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/taskbar.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/window.h>

#include <kernel/vfs.h>


#define GUI_TASKBAR_WIDTH_PERCENT \
    75u

#define GUI_TASKBAR_ICON_SIZE \
    48u

#define GUI_TASKBAR_ICON_PADDING_Y \
    8u

#define GUI_TASKBAR_ICON_GAP \
    12u

#define GUI_TASKBAR_PANEL_PADDING_X \
    12u

#define GUI_TASKBAR_PANEL_HEIGHT \
    (GUI_TASKBAR_ICON_SIZE + \
     GUI_TASKBAR_ICON_PADDING_Y * 2u)

#define GUI_TASKBAR_BOTTOM_MARGIN \
    12u

#define GUI_TASKBAR_SHADOW_PADDING_X \
    16u

#define GUI_TASKBAR_SHADOW_PADDING_TOP \
    12u

#define GUI_TASKBAR_SHADOW_PADDING_BOTTOM \
    18u

#define GUI_TASKBAR_DIRECTORY \
    "/taskbar"

#define GUI_TASKBAR_MAX_PINNED \
    16u

#define GUI_TASKBAR_PATH_MAX \
    256u

#define GUI_TASKBAR_DESCRIPTOR_MAX \
    1024u


typedef struct gui_taskbar_pinned_app
{
    char link_name[
        GUI_TASKBAR_PATH_MAX];

    char application_path[
        GUI_TASKBAR_PATH_MAX];

    char icon_path[
        GUI_TASKBAR_PATH_MAX];

    const gui_image_t *icon;

} gui_taskbar_pinned_app_t;


static bool
    taskbar_initialized;

static gui_window_t
    taskbar_window;

static gui_taskbar_pinned_app_t
    pinned_apps[
        GUI_TASKBAR_MAX_PINNED];

static size_t
    pinned_app_count;


/*
 * ------------------------------------------------------------
 * Small string helpers
 * ------------------------------------------------------------
 */

static size_t gui_taskbar_string_length(
    const char *text)
{
    size_t length =
        0u;

    if (text == NULL)
        return 0u;

    while (text[length] != '\0')
        ++length;

    return length;
}


static int gui_taskbar_string_compare(
    const char *left,
    const char *right)
{
    if (left == NULL &&
        right == NULL)
    {
        return 0;
    }

    if (left == NULL)
        return -1;

    if (right == NULL)
        return 1;

    while (*left != '\0' &&
           *right != '\0')
    {
        if ((unsigned char)*left <
            (unsigned char)*right)
        {
            return -1;
        }

        if ((unsigned char)*left >
            (unsigned char)*right)
        {
            return 1;
        }

        ++left;
        ++right;
    }

    if (*left == '\0' &&
        *right == '\0')
    {
        return 0;
    }

    return
        *left == '\0'
            ? -1
            : 1;
}


static bool gui_taskbar_string_equals(
    const char *left,
    const char *right)
{
    return
        gui_taskbar_string_compare(
            left,
            right) == 0;
}


static bool gui_taskbar_copy_string(
    char *destination,
    size_t capacity,
    const char *source)
{
    if (destination == NULL ||
        source == NULL ||
        capacity == 0u)
    {
        return false;
    }

    size_t length =
        gui_taskbar_string_length(
            source);

    if (length + 1u >
        capacity)
    {
        return false;
    }

    for (size_t i = 0u;
         i <= length;
         ++i)
    {
        destination[i] =
            source[i];
    }

    return true;
}


static bool gui_taskbar_is_space(
    char character)
{
    return
        character == ' ' ||
        character == '\t' ||
        character == '\r';
}


static char *gui_taskbar_trim_left(
    char *text)
{
    if (text == NULL)
        return NULL;

    while (gui_taskbar_is_space(
        *text))
    {
        ++text;
    }

    return text;
}


static void gui_taskbar_trim_right(
    char *text)
{
    if (text == NULL)
        return;

    size_t length =
        gui_taskbar_string_length(
            text);

    while (length != 0u &&
           gui_taskbar_is_space(
               text[length - 1u]))
    {
        --length;
    }

    text[length] =
        '\0';
}


static char *gui_taskbar_find_character(
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


static bool gui_taskbar_has_suffix(
    const char *text,
    const char *suffix)
{
    if (text == NULL ||
        suffix == NULL)
    {
        return false;
    }

    size_t text_length =
        gui_taskbar_string_length(
            text);

    size_t suffix_length =
        gui_taskbar_string_length(
            suffix);

    if (suffix_length >
        text_length)
    {
        return false;
    }

    size_t start =
        text_length -
        suffix_length;

    for (size_t i = 0u;
         i < suffix_length;
         ++i)
    {
        if (text[start + i] !=
            suffix[i])
        {
            return false;
        }
    }

    return true;
}


/*
 * ------------------------------------------------------------
 * Filesystem helpers
 * ------------------------------------------------------------
 */

static bool gui_taskbar_build_child_path(
    const char *directory,
    const char *name,
    char *output,
    size_t capacity)
{
    if (directory == NULL ||
        name == NULL ||
        output == NULL ||
        capacity == 0u)
    {
        return false;
    }

    size_t directory_length =
        gui_taskbar_string_length(
            directory);

    size_t name_length =
        gui_taskbar_string_length(
            name);

    if (directory_length == 0u ||
        name_length == 0u)
    {
        return false;
    }

    bool needs_slash =
        directory[
            directory_length - 1u] != '/';

    size_t required =
        directory_length +
        (needs_slash ? 1u : 0u) +
        name_length +
        1u;

    if (required >
        capacity)
    {
        return false;
    }

    size_t output_index =
        0u;

    for (size_t i = 0u;
         i < directory_length;
         ++i)
    {
        output[output_index++] =
            directory[i];
    }

    if (needs_slash)
    {
        output[output_index++] =
            '/';
    }

    for (size_t i = 0u;
         i < name_length;
         ++i)
    {
        output[output_index++] =
            name[i];
    }

    output[output_index] =
        '\0';

    return true;
}


static bool gui_taskbar_read_file(
    const char *path,
    char *buffer,
    size_t capacity)
{
    if (path == NULL ||
        buffer == NULL ||
        capacity < 2u)
    {
        return false;
    }

    file_t *file =
        NULL;

    if (vfs_open(
            path,
            VFS_OPEN_READ,
            &file) != 0)
    {
        return false;
    }

    if (file == NULL)
        return false;

    size_t used =
        0u;

    bool success =
        true;

    for (;;)
    {
        if (used ==
            capacity - 1u)
        {
            char extra;

            size_t bytes_read =
                0u;

            if (vfs_read(
                    file,
                    &extra,
                    1u,
                    &bytes_read) != 0)
            {
                success =
                    false;

                break;
            }

            if (bytes_read != 0u)
            {
                success =
                    false;
            }

            break;
        }

        size_t bytes_read =
            0u;

        if (vfs_read(
                file,
                buffer + used,
                capacity -
                    1u -
                    used,
                &bytes_read) != 0)
        {
            success =
                false;

            break;
        }

        if (bytes_read == 0u)
            break;

        if (bytes_read >
            capacity -
                1u -
                used)
        {
            success =
                false;

            break;
        }

        used +=
            bytes_read;
    }

    vfs_close(
        file);

    if (!success ||
        used == 0u)
    {
        return false;
    }

    buffer[used] =
        '\0';

    return true;
}


/*
 * ------------------------------------------------------------
 * Descriptor parser
 * ------------------------------------------------------------
 *
 * Kernel GUI intentionally reads only the fields it needs.
 *
 * .link:
 *
 *     [Link]
 *     Application=/apps/terminal.app
 *
 * .app:
 *
 *     [Application]
 *     Icon=/icons/apps/terminal.png
 *
 * Userspace remains the canonical validator for the complete
 * descriptor formats.
 * ------------------------------------------------------------
 */

static bool gui_taskbar_read_ini_value(
    const char *path,
    const char *wanted_section,
    const char *wanted_key,
    char *output,
    size_t output_capacity)
{
    if (path == NULL ||
        wanted_section == NULL ||
        wanted_key == NULL ||
        output == NULL ||
        output_capacity == 0u)
    {
        return false;
    }

    char contents[
        GUI_TASKBAR_DESCRIPTOR_MAX +
        1u];

    if (!gui_taskbar_read_file(
            path,
            contents,
            sizeof(contents)))
    {
        return false;
    }

    bool in_wanted_section =
        false;

    bool wanted_section_seen =
        false;

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

        bool end_of_file =
            *cursor == '\0';

        if (!end_of_file)
        {
            *cursor =
                '\0';

            ++cursor;
        }

        char *text =
            gui_taskbar_trim_left(
                line);

        gui_taskbar_trim_right(
            text);

        if (text[0] == '\0' ||
            text[0] == '#' ||
            text[0] == ';')
        {
            if (end_of_file)
                break;

            continue;
        }

        if (text[0] == '[')
        {
            size_t length =
                gui_taskbar_string_length(
                    text);

            if (length >= 2u &&
                text[length - 1u] == ']')
            {
                text[length - 1u] =
                    '\0';

                char *section =
                    gui_taskbar_trim_left(
                        text + 1);

                gui_taskbar_trim_right(
                    section);

                in_wanted_section =
                    gui_taskbar_string_equals(
                        section,
                        wanted_section);

                if (in_wanted_section)
                {
                    if (wanted_section_seen)
                    {
                        return false;
                    }

                    wanted_section_seen =
                        true;
                }
            }
            else
            {
                return false;
            }

            if (end_of_file)
                break;

            continue;
        }

        if (!in_wanted_section)
        {
            if (end_of_file)
                break;

            continue;
        }

        char *equals =
            gui_taskbar_find_character(
                text,
                '=');

        if (equals == NULL)
        {
            return false;
        }

        *equals =
            '\0';

        char *key =
            gui_taskbar_trim_left(
                text);

        char *value =
            gui_taskbar_trim_left(
                equals + 1);

        gui_taskbar_trim_right(
            key);

        gui_taskbar_trim_right(
            value);

        if (gui_taskbar_string_equals(
                key,
                wanted_key))
        {
            if (value[0] != '/')
                return false;

            return gui_taskbar_copy_string(
                output,
                output_capacity,
                value);
        }

        if (end_of_file)
            break;
    }

    return false;
}


/*
 * ------------------------------------------------------------
 * Pinned application discovery
 * ------------------------------------------------------------
 */

static bool gui_taskbar_load_one_link(
    const char *link_name,
    gui_taskbar_pinned_app_t *result)
{
    if (link_name == NULL ||
        result == NULL)
    {
        return false;
    }

    if (!gui_taskbar_has_suffix(
            link_name,
            ".link"))
    {
        return false;
    }

    char link_path[
        GUI_TASKBAR_PATH_MAX];

    if (!gui_taskbar_build_child_path(
            GUI_TASKBAR_DIRECTORY,
            link_name,
            link_path,
            sizeof(link_path)))
    {
        return false;
    }

    char application_path[
        GUI_TASKBAR_PATH_MAX];

    if (!gui_taskbar_read_ini_value(
            link_path,
            "Link",
            "Application",
            application_path,
            sizeof(application_path)))
    {
        return false;
    }

    if (!gui_taskbar_has_suffix(
            application_path,
            ".app"))
    {
        return false;
    }

    char icon_path[
        GUI_TASKBAR_PATH_MAX];

    if (!gui_taskbar_read_ini_value(
            application_path,
            "Application",
            "Icon",
            icon_path,
            sizeof(icon_path)))
    {
        return false;
    }

    const gui_image_t *icon =
        NULL;

    if (!gui_image_get(
            icon_path,
            &icon))
    {
        return false;
    }

    if (icon == NULL)
        return false;

    if (!gui_taskbar_copy_string(
            result->link_name,
            sizeof(result->link_name),
            link_name))
    {
        return false;
    }

    if (!gui_taskbar_copy_string(
            result->application_path,
            sizeof(
                result->application_path),
            application_path))
    {
        return false;
    }

    if (!gui_taskbar_copy_string(
            result->icon_path,
            sizeof(result->icon_path),
            icon_path))
    {
        return false;
    }

    result->icon =
        icon;

    return true;
}


static void gui_taskbar_sort_pinned_apps(void)
{
    if (pinned_app_count < 2u)
        return;

    for (size_t i = 1u;
         i < pinned_app_count;
         ++i)
    {
        gui_taskbar_pinned_app_t current =
            pinned_apps[i];

        size_t j =
            i;

        while (j > 0u &&
               gui_taskbar_string_compare(
                   pinned_apps[j - 1u].
                       link_name,
                   current.link_name) > 0)
        {
            pinned_apps[j] =
                pinned_apps[j - 1u];

            --j;
        }

        pinned_apps[j] =
            current;
    }
}


static void gui_taskbar_discover_pinned_apps(void)
{
    pinned_app_count =
        0u;

    file_t *directory =
        NULL;

    if (vfs_open(
            GUI_TASKBAR_DIRECTORY,
            VFS_OPEN_READ,
            &directory) != 0)
    {
        /*
         * A missing /taskbar directory is not a GUI failure.
         * The dock simply remains empty.
         */
        return;
    }

    if (directory == NULL)
        return;

    for (;;)
    {
        vfs_dirent_t entry;

        int result =
            vfs_readdir(
                directory,
                &entry);

        if (result <= 0)
            break;

        if (entry.type !=
            VNODE_REGULAR)
        {
            continue;
        }

        if (!gui_taskbar_has_suffix(
                entry.name,
                ".link"))
        {
            continue;
        }

        if (pinned_app_count >=
            GUI_TASKBAR_MAX_PINNED)
        {
            break;
        }

        gui_taskbar_pinned_app_t app;

        app.link_name[0] =
            '\0';

        app.application_path[0] =
            '\0';

        app.icon_path[0] =
            '\0';

        app.icon =
            NULL;

        if (!gui_taskbar_load_one_link(
                entry.name,
                &app))
        {
            /*
             * Invalid links, missing applications, and missing
             * icons are ignored rather than breaking the shell.
             */
            continue;
        }

        pinned_apps[
            pinned_app_count] =
                app;

        pinned_app_count++;
    }

    vfs_close(
        directory);

    /*
     * ext2 directory iteration order is not a shell ordering
     * contract. Sort by filename so names like:
     *
     *     10-explorer.link
     *     20-terminal.link
     *     30-settings.link
     *
     * produce deterministic taskbar placement.
     */
    gui_taskbar_sort_pinned_apps();
}


/*
 * ------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------
 */

static bool gui_taskbar_render_surface(void)
{
    gui_surface_t *surface =
        gui_window_surface(
            &taskbar_window);

    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return false;
    }

    const gui_theme_t *theme =
        gui_theme_default();

    if (theme == NULL)
        return false;

    gui_surface_clear(
        surface,
        GUI_TRANSPARENT);

    gui_rect_t panel;

    panel.x =
        GUI_TASKBAR_SHADOW_PADDING_X;

    panel.y =
        GUI_TASKBAR_SHADOW_PADDING_TOP;

    panel.width =
        surface->width -
        GUI_TASKBAR_SHADOW_PADDING_X *
            2u;

    panel.height =
        GUI_TASKBAR_PANEL_HEIGHT;

    gui_painter_draw_rounded_shadow(
        surface,
        panel,
        theme->taskbar_corner_radius,
        theme->taskbar_shadow_offset_x,
        theme->taskbar_shadow_offset_y,
        theme->taskbar_shadow_spread,
        theme->taskbar_shadow_blur,
        theme->taskbar_shadow);

    gui_painter_fill_rounded_vertical_gradient(
        surface,
        panel,
        theme->taskbar_corner_radius,
        theme->taskbar_gradient_top,
        theme->taskbar_gradient_bottom);

    gui_painter_stroke_rounded_rect(
        surface,
        panel,
        theme->taskbar_corner_radius,
        theme->taskbar_border_thickness,
        theme->taskbar_border);

    if (pinned_app_count == 0u)
        return true;

    uint64_t icons_width =
        (uint64_t)pinned_app_count *
        GUI_TASKBAR_ICON_SIZE;

    uint64_t gaps_width =
        pinned_app_count > 1u
            ? (uint64_t)
              (pinned_app_count - 1u) *
              GUI_TASKBAR_ICON_GAP
            : 0u;

    uint64_t content_width =
        icons_width +
        gaps_width;

    uint32_t usable_width =
        panel.width >
                GUI_TASKBAR_PANEL_PADDING_X *
                    2u
            ? panel.width -
              GUI_TASKBAR_PANEL_PADDING_X *
                  2u
            : 0u;

    if (content_width >
        usable_width)
    {
        /*
         * GUI_TASKBAR_MAX_PINNED is deliberately conservative,
         * but never draw outside the usable panel if the screen
         * is unusually narrow.
         */
        return true;
    }

    int32_t icon_x =
        panel.x +
        (int32_t)(
            (panel.width -
             (uint32_t)content_width) /
            2u);

    int32_t icon_y =
        panel.y +
        (int32_t)
            GUI_TASKBAR_ICON_PADDING_Y;

    for (size_t i = 0u;
         i < pinned_app_count;
         ++i)
    {
        if (pinned_apps[i].icon !=
            NULL)
        {
            gui_rect_t icon_rect;

            icon_rect.x =
                icon_x;

            icon_rect.y =
                icon_y;

            icon_rect.width =
                GUI_TASKBAR_ICON_SIZE;

            icon_rect.height =
                GUI_TASKBAR_ICON_SIZE;

            gui_image_draw_scaled(
                surface,
                pinned_apps[i].icon,
                icon_rect);
        }

        icon_x +=
            (int32_t)(
                GUI_TASKBAR_ICON_SIZE +
                GUI_TASKBAR_ICON_GAP);
    }

    return true;
}


/*
 * ------------------------------------------------------------
 * Initialization
 * ------------------------------------------------------------
 */

bool gui_taskbar_initialize(void)
{
    if (taskbar_initialized)
        return true;

    if (!gui_compositor_is_initialized())
        return false;

    gui_surface_t *screen =
        gui_compositor_surface();

    if (screen == NULL ||
        screen->pixels == NULL ||
        screen->width == 0u ||
        screen->height == 0u)
    {
        return false;
    }

    /*
     * Load pinned applications before painting the taskbar.
     *
     * Missing /taskbar or invalid descriptors simply produce
     * an empty dock.
     */
    gui_taskbar_discover_pinned_apps();

    uint32_t panel_width =
        (uint32_t)
        (((uint64_t)screen->width *
          GUI_TASKBAR_WIDTH_PERCENT) /
         100u);

    if (panel_width == 0u ||
        panel_width > screen->width)
    {
        return false;
    }

    uint32_t panel_x =
        (screen->width -
         panel_width) /
        2u;

    uint32_t surface_width =
        panel_width +
        GUI_TASKBAR_SHADOW_PADDING_X *
            2u;

    uint32_t surface_height =
        GUI_TASKBAR_SHADOW_PADDING_TOP +
        GUI_TASKBAR_PANEL_HEIGHT +
        GUI_TASKBAR_SHADOW_PADDING_BOTTOM;

    if (panel_x <
        GUI_TASKBAR_SHADOW_PADDING_X)
    {
        return false;
    }

    if (surface_height +
            GUI_TASKBAR_BOTTOM_MARGIN >
        screen->height)
    {
        return false;
    }

    int32_t window_x =
        (int32_t)
        (panel_x -
         GUI_TASKBAR_SHADOW_PADDING_X);

    uint32_t visible_panel_y =
        screen->height -
        GUI_TASKBAR_BOTTOM_MARGIN -
        GUI_TASKBAR_PANEL_HEIGHT;

    if (visible_panel_y <
        GUI_TASKBAR_SHADOW_PADDING_TOP)
    {
        return false;
    }

    int32_t window_y =
        (int32_t)
        (visible_panel_y -
         GUI_TASKBAR_SHADOW_PADDING_TOP);

    if (!gui_window_create(
            &taskbar_window,
            window_x,
            window_y,
            surface_width,
            surface_height,
            GUI_Z_TASKBAR))
    {
        return false;
    }

    if (!gui_taskbar_render_surface())
    {
        gui_window_destroy(
            &taskbar_window);

        return false;
    }

    taskbar_initialized =
        true;

    return true;
}


/*
 * ------------------------------------------------------------
 * Composition
 * ------------------------------------------------------------
 */

void gui_taskbar_composite(void)
{
    if (!taskbar_initialized)
        return;

    gui_window_composite(
        &taskbar_window);
}