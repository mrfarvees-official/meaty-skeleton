#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/gui/components.h>
#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/font.h>
#include <kernel/gui/image.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/topbar.h>
#include <kernel/gui/widget.h>
#include <kernel/gui/window.h>

#include <kernel/sleep_queue.h>
#include <kernel/system_time.h>
#include <kernel/task.h>


#define GUI_TOPBAR_FONT_PIXEL_HEIGHT \
    16u

#define GUI_TOPBAR_CLOCK_BUFFER_SIZE \
    40u

#define GUI_TOPBAR_LEFT_MARGIN \
    8

#define GUI_TOPBAR_RIGHT_MARGIN \
    8

#define GUI_TOPBAR_CONTROL_Y \
    2

#define GUI_TOPBAR_CONTROL_HEIGHT \
    28u

#define GUI_TOPBAR_MEATY_WIDTH \
    116u

#define GUI_TOPBAR_POWER_WIDTH \
    44u

#define GUI_TOPBAR_CLOCK_GAP \
    12


#define GUI_START_MENU_WIDTH \
    280u

#define GUI_START_MENU_HEIGHT \
    220u

#define GUI_POWER_MENU_WIDTH \
    220u

#define GUI_POWER_MENU_HEIGHT \
    154u

#define GUI_TOPBAR_POPUP_GAP \
    6


/*
 * ------------------------------------------------------------
 * Filesystem assets
 * ------------------------------------------------------------
 */

#define GUI_ICON_POWER \
    "/icons/system/power.png"

#define GUI_ICON_RESTART \
    "/icons/system/restart.png"

#define GUI_ICON_SHUTDOWN \
    "/icons/system/shutdown.png"

#define GUI_ICON_SETTINGS \
    "/icons/system/settings.png"

#define GUI_ICON_TERMINAL \
    "/icons/apps/terminal.png"

#define GUI_ICON_EXPLORER \
    "/icons/apps/explorer.png"


typedef enum gui_topbar_popup
{
    GUI_TOPBAR_POPUP_NONE = 0,

    GUI_TOPBAR_POPUP_START,
    GUI_TOPBAR_POPUP_POWER

} gui_topbar_popup_t;


/*
 * ------------------------------------------------------------
 * Main state
 * ------------------------------------------------------------
 */

static bool
    topbar_initialized;

static task_t *
    topbar_clock_task;


static gui_window_t
    topbar_window;

static gui_window_t
    start_menu_window;

static gui_window_t
    power_menu_window;


static gui_topbar_popup_t
    active_popup;


/*
 * ------------------------------------------------------------
 * Root widget trees
 * ------------------------------------------------------------
 */

static gui_widget_t
    topbar_root;

static gui_widget_t
    start_menu_root;

static gui_widget_t
    power_menu_root;


/*
 * ------------------------------------------------------------
 * Topbar widgets
 * ------------------------------------------------------------
 */

static gui_button_t
    meaty_button;

static gui_button_t
    power_button;


/*
 * ------------------------------------------------------------
 * Start-menu widgets
 * ------------------------------------------------------------
 */

static gui_panel_t
    start_menu_panel;

static gui_label_t
    start_menu_title;

static gui_button_t
    explorer_button;

static gui_button_t
    terminal_button;

static gui_button_t
    settings_button;


/*
 * ------------------------------------------------------------
 * Power-menu widgets
 * ------------------------------------------------------------
 */

static gui_panel_t
    power_menu_panel;

static gui_label_t
    power_menu_title;

static gui_button_t
    restart_button;

static gui_button_t
    shutdown_button;


/*
 * ------------------------------------------------------------
 * Pointer state
 * ------------------------------------------------------------
 */

static gui_button_t *
    hovered_button;

static gui_button_t *
    pressed_button;


/*
 * ------------------------------------------------------------
 * Images
 * ------------------------------------------------------------
 */

static const gui_image_t *
    icon_power;

static const gui_image_t *
    icon_restart;

static const gui_image_t *
    icon_shutdown;

static const gui_image_t *
    icon_settings;

static const gui_image_t *
    icon_terminal;

static const gui_image_t *
    icon_explorer;


/*
 * ------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------
 */

static void gui_topbar_meaty_clicked(
    gui_button_t *button,
    void *context);

static void gui_topbar_power_clicked(
    gui_button_t *button,
    void *context);

static void gui_topbar_start_item_clicked(
    gui_button_t *button,
    void *context);

static void gui_topbar_set_popup(
    gui_topbar_popup_t popup);

static void gui_topbar_close_popup(void);

static bool gui_topbar_render_widget_window(
    gui_window_t *window,
    gui_widget_t *root);

static bool gui_topbar_render_widgets(void);


/*
 * ============================================================
 * Calendar helpers
 * ============================================================
 */

static uint8_t gui_topbar_weekday(
    uint16_t year,
    uint8_t month,
    uint8_t day)
{
    static const uint8_t offsets[12] =
    {
        0u, 3u, 2u, 5u,
        0u, 3u, 5u, 1u,
        4u, 6u, 2u, 4u
    };

    uint32_t calculation_year =
        year;

    if (month < 3u)
        --calculation_year;

    return
        (uint8_t)
        ((calculation_year +
          calculation_year / 4u -
          calculation_year / 100u +
          calculation_year / 400u +
          offsets[month - 1u] +
          day) %
         7u);
}


static char *gui_topbar_append_text(
    char *destination,
    const char *text)
{
    while (*text != '\0')
    {
        *destination++ =
            *text++;
    }

    return destination;
}


static char *gui_topbar_append_two_digits(
    char *destination,
    uint8_t value)
{
    *destination++ =
        (char)
        ('0' +
         ((value / 10u) % 10u));

    *destination++ =
        (char)
        ('0' +
         (value % 10u));

    return destination;
}


static char *gui_topbar_append_year(
    char *destination,
    uint16_t year)
{
    *destination++ =
        (char)
        ('0' +
         ((year / 1000u) % 10u));

    *destination++ =
        (char)
        ('0' +
         ((year / 100u) % 10u));

    *destination++ =
        (char)
        ('0' +
         ((year / 10u) % 10u));

    *destination++ =
        (char)
        ('0' +
         (year % 10u));

    return destination;
}


static bool gui_topbar_format_datetime(
    const rtc_datetime_t *datetime,
    char *buffer,
    size_t capacity)
{
    if (datetime == NULL ||
        buffer == NULL ||
        capacity <
            GUI_TOPBAR_CLOCK_BUFFER_SIZE)
    {
        return false;
    }

    static const char *weekday_names[7] =
    {
        "Sun",
        "Mon",
        "Tue",
        "Wed",
        "Thu",
        "Fri",
        "Sat"
    };

    static const char *month_names[12] =
    {
        "Jan",
        "Feb",
        "Mar",
        "Apr",
        "May",
        "Jun",
        "Jul",
        "Aug",
        "Sep",
        "Oct",
        "Nov",
        "Dec"
    };

    if (datetime->month < 1u ||
        datetime->month > 12u)
    {
        return false;
    }

    uint8_t weekday =
        gui_topbar_weekday(
            datetime->year,
            datetime->month,
            datetime->day);

    uint8_t hour_12 =
        (uint8_t)
        (datetime->hour %
         12u);

    if (hour_12 == 0u)
        hour_12 = 12u;

    bool afternoon =
        datetime->hour >= 12u;

    char *cursor =
        buffer;

    cursor =
        gui_topbar_append_text(
            cursor,
            weekday_names[
                weekday]);

    *cursor++ = ' ';

    cursor =
        gui_topbar_append_text(
            cursor,
            month_names[
                datetime->month -
                1u]);

    *cursor++ = ' ';

    if (datetime->day >= 10u)
    {
        *cursor++ =
            (char)
            ('0' +
             datetime->day /
             10u);
    }

    *cursor++ =
        (char)
        ('0' +
         datetime->day %
         10u);

    *cursor++ = ' ';

    cursor =
        gui_topbar_append_year(
            cursor,
            datetime->year);

    *cursor++ = ' ';
    *cursor++ = ' ';
    *cursor++ = ' ';

    if (hour_12 >= 10u)
    {
        *cursor++ =
            (char)
            ('0' +
             hour_12 /
             10u);
    }

    *cursor++ =
        (char)
        ('0' +
         hour_12 %
         10u);

    *cursor++ = ':';

    cursor =
        gui_topbar_append_two_digits(
            cursor,
            datetime->minute);

    *cursor++ = ':';

    cursor =
        gui_topbar_append_two_digits(
            cursor,
            datetime->second);

    *cursor++ = ' ';

    cursor =
        gui_topbar_append_text(
            cursor,
            afternoon
                ? "PM"
                : "AM");

    *cursor =
        '\0';

    return true;
}


/*
 * ============================================================
 * Text measurement
 * ============================================================
 */

static int32_t gui_topbar_measure_text(
    gui_font_t *font,
    uint32_t pixel_height,
    const char *text)
{
    if (font == NULL ||
        text == NULL)
    {
        return 0;
    }

    int32_t width =
        0;

    while (*text != '\0')
    {
        uint32_t codepoint =
            (uint8_t)
            *text++;

        if (codepoint >= 0x80u)
            codepoint = '?';

        gui_font_glyph_metrics_t
            metrics;

        if (!gui_font_get_glyph_metrics(
                font,
                codepoint,
                pixel_height,
                &metrics))
        {
            continue;
        }

        width +=
            metrics.advance;
    }

    return width;
}


/*
 * ============================================================
 * Asset loading
 * ============================================================
 */

static void gui_topbar_load_icons(void)
{
    icon_power =
        NULL;

    icon_restart =
        NULL;

    icon_shutdown =
        NULL;

    icon_settings =
        NULL;

    icon_terminal =
        NULL;

    icon_explorer =
        NULL;

    /*
     * Assets remain filesystem-loaded.
     *
     * Missing icons are non-fatal because every control retains a
     * textual fallback.
     */
    (void)gui_image_get(
        GUI_ICON_POWER,
        &icon_power);

    (void)gui_image_get(
        GUI_ICON_RESTART,
        &icon_restart);

    (void)gui_image_get(
        GUI_ICON_SHUTDOWN,
        &icon_shutdown);

    (void)gui_image_get(
        GUI_ICON_SETTINGS,
        &icon_settings);

    (void)gui_image_get(
        GUI_ICON_TERMINAL,
        &icon_terminal);

    (void)gui_image_get(
        GUI_ICON_EXPLORER,
        &icon_explorer);
}


/*
 * ============================================================
 * Common light-button styling
 * ============================================================
 */

static void gui_topbar_style_light_button(
    gui_button_t *button,
    gui_color_t text_color)
{
    if (button == NULL)
        return;

    button->text_color =
        text_color;

    button->background_normal =
        GUI_TRANSPARENT;

    button->background_hover =
        GUI_RGBA(
            30u,
            40u,
            55u,
            16u);

    button->background_pressed =
        GUI_RGBA(
            30u,
            40u,
            55u,
            30u);

    button->background_disabled =
        GUI_RGBA(
            90u,
            100u,
            115u,
            8u);

    button->disabled_text_color =
        GUI_RGBA(
            70u,
            80u,
            95u,
            105u);

    button->border =
        GUI_TRANSPARENT;

    button->border_thickness =
        0u;

    button->corner_radius =
        8u;
}


/*
 * ============================================================
 * Main topbar widget tree
 * ============================================================
 */

static bool gui_topbar_build_main_widgets(
    uint32_t width,
    uint32_t height)
{
    const gui_theme_t *theme =
        gui_theme_default();

    if (theme == NULL)
        return false;

    gui_rect_t root_bounds =
    {
        .x = 0,
        .y = 0,
        .width = width,
        .height = height
    };

    gui_widget_initialize(
        &topbar_root,
        root_bounds,
        NULL,
        NULL);


    /*
     * --------------------------------------------------------
     * Meaty OS / Start
     * --------------------------------------------------------
     */

    gui_rect_t meaty_bounds =
    {
        .x =
            GUI_TOPBAR_LEFT_MARGIN,

        .y =
            GUI_TOPBAR_CONTROL_Y,

        .width =
            GUI_TOPBAR_MEATY_WIDTH,

        .height =
            GUI_TOPBAR_CONTROL_HEIGHT
    };

    gui_button_initialize(
        &meaty_button,
        meaty_bounds,
        "Meaty OS");

    meaty_button.font_pixel_height =
        GUI_TOPBAR_FONT_PIXEL_HEIGHT;

    meaty_button.horizontal_padding =
        10u;

    gui_topbar_style_light_button(
        &meaty_button,
        theme->topbar_text);

    gui_button_set_click_handler(
        &meaty_button,
        gui_topbar_meaty_clicked,
        NULL);

    if (!gui_widget_add_child(
            &topbar_root,
            gui_button_widget(
                &meaty_button)))
    {
        return false;
    }


    /*
     * --------------------------------------------------------
     * Power
     * --------------------------------------------------------
     */

    gui_rect_t power_bounds =
    {
        .x =
            (int32_t)width -
            GUI_TOPBAR_RIGHT_MARGIN -
            GUI_TOPBAR_POWER_WIDTH,

        .y =
            GUI_TOPBAR_CONTROL_Y,

        .width =
            GUI_TOPBAR_POWER_WIDTH,

        .height =
            GUI_TOPBAR_CONTROL_HEIGHT
    };

    gui_button_initialize(
        &power_button,
        power_bounds,
        NULL);

    gui_topbar_style_light_button(
        &power_button,
        theme->topbar_text);

    power_button.horizontal_padding =
        8u;

    gui_button_set_icon(
        &power_button,
        icon_power);

    /*
     * Never leave an invisible control.
     */
    if (icon_power == NULL)
    {
        power_button.text =
            "P";

        power_button.horizontal_padding =
            16u;
    }

    gui_button_set_click_handler(
        &power_button,
        gui_topbar_power_clicked,
        NULL);

    if (!gui_widget_add_child(
            &topbar_root,
            gui_button_widget(
                &power_button)))
    {
        return false;
    }

    return true;
}


/*
 * ============================================================
 * Start menu
 * ============================================================
 */

static bool gui_topbar_build_start_menu(void)
{
    const gui_theme_t *theme =
        gui_theme_default();

    if (theme == NULL)
        return false;

    gui_rect_t root_bounds =
    {
        .x = 0,
        .y = 0,

        .width =
            GUI_START_MENU_WIDTH,

        .height =
            GUI_START_MENU_HEIGHT
    };

    gui_widget_initialize(
        &start_menu_root,
        root_bounds,
        NULL,
        NULL);


    /*
     * White popup panel.
     */
    gui_rect_t panel_bounds =
    {
        .x = 8,
        .y = 8,

        .width =
            GUI_START_MENU_WIDTH -
            16u,

        .height =
            GUI_START_MENU_HEIGHT -
            16u
    };

    gui_panel_initialize(
        &start_menu_panel,
        panel_bounds);

    start_menu_panel.corner_radius =
        14u;

    start_menu_panel.gradient_top =
        GUI_RGBA(
            255u,
            255u,
            255u,
            250u);

    start_menu_panel.gradient_bottom =
        GUI_RGBA(
            238u,
            242u,
            248u,
            248u);

    start_menu_panel.border =
        GUI_RGBA(
            75u,
            85u,
            100u,
            36u);

    start_menu_panel.border_thickness =
        1u;

    start_menu_panel.shadow_enabled =
        true;

    start_menu_panel.shadow =
        GUI_RGBA(
            0u,
            0u,
            0u,
            55u);

    start_menu_panel.shadow_offset_x =
        0;

    start_menu_panel.shadow_offset_y =
        5;

    start_menu_panel.shadow_spread =
        1u;

    start_menu_panel.shadow_blur =
        9u;

    if (!gui_widget_add_child(
            &start_menu_root,
            gui_panel_widget(
                &start_menu_panel)))
    {
        return false;
    }


    /*
     * Title.
     */
    gui_rect_t title_bounds =
    {
        .x = 18,
        .y = 10,

        .width =
            panel_bounds.width -
            36u,

        .height =
            36u
    };

    gui_label_initialize(
        &start_menu_title,
        title_bounds,
        "Meaty OS");

    start_menu_title.pixel_height =
        18u;

    start_menu_title.color =
        theme->text_primary;

    if (!gui_widget_add_child(
            gui_panel_widget(
                &start_menu_panel),
            gui_label_widget(
                &start_menu_title)))
    {
        return false;
    }


    /*
     * --------------------------------------------------------
     * Explorer
     * --------------------------------------------------------
     */

    gui_rect_t item =
    {
        .x = 10,
        .y = 52,

        .width =
            panel_bounds.width -
            20u,

        .height = 42u
    };

    gui_button_initialize(
        &explorer_button,
        item,
        "Explorer");

    gui_topbar_style_light_button(
        &explorer_button,
        theme->text_primary);

    gui_button_set_icon(
        &explorer_button,
        icon_explorer);

    gui_button_set_click_handler(
        &explorer_button,
        gui_topbar_start_item_clicked,
        NULL);

    if (!gui_widget_add_child(
            gui_panel_widget(
                &start_menu_panel),
            gui_button_widget(
                &explorer_button)))
    {
        return false;
    }


    /*
     * --------------------------------------------------------
     * Terminal
     * --------------------------------------------------------
     */

    item.y +=
        44;

    gui_button_initialize(
        &terminal_button,
        item,
        "Terminal");

    gui_topbar_style_light_button(
        &terminal_button,
        theme->text_primary);

    gui_button_set_icon(
        &terminal_button,
        icon_terminal);

    gui_button_set_click_handler(
        &terminal_button,
        gui_topbar_start_item_clicked,
        NULL);

    if (!gui_widget_add_child(
            gui_panel_widget(
                &start_menu_panel),
            gui_button_widget(
                &terminal_button)))
    {
        return false;
    }


    /*
     * --------------------------------------------------------
     * Settings
     * --------------------------------------------------------
     */

    item.y +=
        44;

    gui_button_initialize(
        &settings_button,
        item,
        "Settings");

    gui_topbar_style_light_button(
        &settings_button,
        theme->text_primary);

    gui_button_set_icon(
        &settings_button,
        icon_settings);

    gui_button_set_click_handler(
        &settings_button,
        gui_topbar_start_item_clicked,
        NULL);

    if (!gui_widget_add_child(
            gui_panel_widget(
                &start_menu_panel),
            gui_button_widget(
                &settings_button)))
    {
        return false;
    }

    return true;
}


/*
 * ============================================================
 * Power menu
 * ============================================================
 */

static bool gui_topbar_build_power_menu(void)
{
    const gui_theme_t *theme =
        gui_theme_default();

    if (theme == NULL)
        return false;

    gui_rect_t root_bounds =
    {
        .x = 0,
        .y = 0,

        .width =
            GUI_POWER_MENU_WIDTH,

        .height =
            GUI_POWER_MENU_HEIGHT
    };

    gui_widget_initialize(
        &power_menu_root,
        root_bounds,
        NULL,
        NULL);


    gui_rect_t panel_bounds =
    {
        .x = 8,
        .y = 8,

        .width =
            GUI_POWER_MENU_WIDTH -
            16u,

        .height =
            GUI_POWER_MENU_HEIGHT -
            16u
    };

    gui_panel_initialize(
        &power_menu_panel,
        panel_bounds);

    power_menu_panel.corner_radius =
        14u;

    power_menu_panel.gradient_top =
        GUI_RGBA(
            255u,
            255u,
            255u,
            250u);

    power_menu_panel.gradient_bottom =
        GUI_RGBA(
            238u,
            242u,
            248u,
            248u);

    power_menu_panel.border =
        GUI_RGBA(
            75u,
            85u,
            100u,
            36u);

    power_menu_panel.border_thickness =
        1u;

    power_menu_panel.shadow_enabled =
        true;

    power_menu_panel.shadow =
        GUI_RGBA(
            0u,
            0u,
            0u,
            55u);

    power_menu_panel.shadow_offset_x =
        0;

    power_menu_panel.shadow_offset_y =
        5;

    power_menu_panel.shadow_spread =
        1u;

    power_menu_panel.shadow_blur =
        9u;

    if (!gui_widget_add_child(
            &power_menu_root,
            gui_panel_widget(
                &power_menu_panel)))
    {
        return false;
    }


    /*
     * Title.
     */
    gui_rect_t title_bounds =
    {
        .x = 16,
        .y = 8,

        .width =
            panel_bounds.width -
            32u,

        .height = 30u
    };

    gui_label_initialize(
        &power_menu_title,
        title_bounds,
        "Power");

    power_menu_title.pixel_height =
        17u;

    power_menu_title.color =
        theme->text_primary;

    if (!gui_widget_add_child(
            gui_panel_widget(
                &power_menu_panel),
            gui_label_widget(
                &power_menu_title)))
    {
        return false;
    }


    /*
     * Restart.
     */
    gui_rect_t restart_bounds =
    {
        .x = 10,
        .y = 44,

        .width =
            panel_bounds.width -
            20u,

        .height = 38u
    };

    gui_button_initialize(
        &restart_button,
        restart_bounds,
        "Restart");

    gui_topbar_style_light_button(
        &restart_button,
        theme->text_primary);

    gui_button_set_icon(
        &restart_button,
        icon_restart);

    /*
     * Intentionally disabled until generic system power exists.
     */
    gui_widget_set_enabled(
        gui_button_widget(
            &restart_button),
        false);

    if (!gui_widget_add_child(
            gui_panel_widget(
                &power_menu_panel),
            gui_button_widget(
                &restart_button)))
    {
        return false;
    }


    /*
     * Shut Down.
     */
    gui_rect_t shutdown_bounds =
    {
        .x = 10,
        .y = 86,

        .width =
            panel_bounds.width -
            20u,

        .height = 38u
    };

    gui_button_initialize(
        &shutdown_button,
        shutdown_bounds,
        "Shut Down");

    gui_topbar_style_light_button(
        &shutdown_button,
        theme->text_primary);

    gui_button_set_icon(
        &shutdown_button,
        icon_shutdown);

    gui_widget_set_enabled(
        gui_button_widget(
            &shutdown_button),
        false);

    if (!gui_widget_add_child(
            gui_panel_widget(
                &power_menu_panel),
            gui_button_widget(
                &shutdown_button)))
    {
        return false;
    }

    return true;
}


/*
 * ============================================================
 * Popup rendering
 * ============================================================
 */

static bool gui_topbar_render_widget_window(
    gui_window_t *window,
    gui_widget_t *root)
{
    if (window == NULL ||
        root == NULL)
    {
        return false;
    }

    gui_surface_t *surface =
        gui_window_surface(
            window);

    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return false;
    }

    gui_surface_clear(
        surface,
        GUI_TRANSPARENT);

    return
        gui_widget_render_tree(
            root,
            surface);
}


/*
 * ============================================================
 * Main topbar rendering
 * ============================================================
 */

bool gui_topbar_refresh(void)
{
    if (!topbar_initialized)
        return false;

    gui_surface_t *surface =
        gui_window_surface(
            &topbar_window);

    if (surface == NULL ||
        surface->pixels == NULL)
    {
        return false;
    }

    const gui_theme_t *theme =
        gui_theme_default();

    gui_font_t *font =
        gui_font_default();

    if (theme == NULL ||
        font == NULL)
    {
        return false;
    }


    /*
     * --------------------------------------------------------
     * White translucent system bar
     * --------------------------------------------------------
     */

    gui_surface_clear(
        surface,
        GUI_TRANSPARENT);

    gui_rect_t bar_rect =
    {
        .x = 0,
        .y = 0,

        .width =
            surface->width,

        .height =
            surface->height
    };

    gui_surface_fill_rect(
        surface,
        bar_rect,
        theme->topbar_background);


    /*
     * Bottom separator.
     */
    if (surface->height > 0u)
    {
        gui_rect_t border =
        {
            .x = 0,

            .y =
                (int32_t)
                (surface->height -
                 1u),

            .width =
                surface->width,

            .height = 1u
        };

        gui_surface_fill_rect(
            surface,
            border,
            theme->topbar_border);
    }


    /*
     * Meaty OS + Power.
     */
    if (!gui_widget_render_tree(
            &topbar_root,
            surface))
    {
        return false;
    }


    /*
     * --------------------------------------------------------
     * Clock
     * --------------------------------------------------------
     */

    rtc_datetime_t datetime;

    char datetime_text[
        GUI_TOPBAR_CLOCK_BUFFER_SIZE];

    const char *display_text =
        "RTC unavailable";

    gui_color_t text_color =
        theme->topbar_text_secondary;

    if (system_time_local_datetime(
            &datetime) &&
        gui_topbar_format_datetime(
            &datetime,
            datetime_text,
            sizeof(datetime_text)))
    {
        display_text =
            datetime_text;

        text_color =
            theme->topbar_text;
    }

    int32_t text_width =
        gui_topbar_measure_text(
            font,
            GUI_TOPBAR_FONT_PIXEL_HEIGHT,
            display_text);

    /*
     * Clock lives immediately to the left of Power.
     */
    int32_t right_edge =
        (int32_t)
            surface->width -
        GUI_TOPBAR_RIGHT_MARGIN -
        GUI_TOPBAR_POWER_WIDTH -
        GUI_TOPBAR_CLOCK_GAP;

    int32_t text_x =
        right_edge -
        text_width;

    int32_t minimum_x =
        GUI_TOPBAR_LEFT_MARGIN +
        GUI_TOPBAR_MEATY_WIDTH +
        16;

    if (text_x <
        minimum_x)
    {
        text_x =
            minimum_x;
    }

    int32_t text_y =
        ((int32_t)
             surface->height -
         (int32_t)
             GUI_TOPBAR_FONT_PIXEL_HEIGHT) /
        2;

    return
        gui_font_draw_text(
            surface,
            font,
            text_x,
            text_y,
            GUI_TOPBAR_FONT_PIXEL_HEIGHT,
            display_text,
            text_color);
}


static bool gui_topbar_render_widgets(void)
{
    if (!gui_topbar_refresh())
        return false;

    if (!gui_topbar_render_widget_window(
            &start_menu_window,
            &start_menu_root))
    {
        return false;
    }

    if (!gui_topbar_render_widget_window(
            &power_menu_window,
            &power_menu_root))
    {
        return false;
    }

    return true;
}


/*
 * ============================================================
 * Popup state
 * ============================================================
 */

static void gui_topbar_set_popup(
    gui_topbar_popup_t popup)
{
    active_popup =
        popup;

    gui_window_set_visible(
        &start_menu_window,
        popup ==
            GUI_TOPBAR_POPUP_START);

    gui_window_set_visible(
        &power_menu_window,
        popup ==
            GUI_TOPBAR_POPUP_POWER);
}


static void gui_topbar_close_popup(void)
{
    gui_topbar_set_popup(
        GUI_TOPBAR_POPUP_NONE);
}


/*
 * ============================================================
 * Button callbacks
 * ============================================================
 */

static void gui_topbar_meaty_clicked(
    gui_button_t *button,
    void *context)
{
    (void)button;
    (void)context;

    if (active_popup ==
        GUI_TOPBAR_POPUP_START)
    {
        gui_topbar_close_popup();
    }
    else
    {
        gui_topbar_set_popup(
            GUI_TOPBAR_POPUP_START);
    }
}


static void gui_topbar_power_clicked(
    gui_button_t *button,
    void *context)
{
    (void)button;
    (void)context;

    if (active_popup ==
        GUI_TOPBAR_POPUP_POWER)
    {
        gui_topbar_close_popup();
    }
    else
    {
        gui_topbar_set_popup(
            GUI_TOPBAR_POPUP_POWER);
    }
}


static void gui_topbar_start_item_clicked(
    gui_button_t *button,
    void *context)
{
    (void)button;
    (void)context;

    /*
     * Real application launching comes later.
     */
    gui_topbar_close_popup();
}


/*
 * ============================================================
 * Clock task
 * ============================================================
 */

static void gui_topbar_clock_thread(
    void *argument)
{
    (void)argument;

    for (;;)
    {
        /*
         * GUI rendering remains in normal task context,
         * never IRQ0.
         */
        if (topbar_initialized &&
            gui_topbar_refresh())
        {
            gui_desktop_render();
        }

        task_sleep(
            1000u);
    }
}


/*
 * ============================================================
 * Initialization
 * ============================================================
 */

bool gui_topbar_initialize(void)
{
    if (topbar_initialized)
        return true;

    if (!gui_compositor_is_initialized())
        return false;

    gui_surface_t *screen =
        gui_compositor_surface();

    const gui_theme_t *theme =
        gui_theme_default();

    if (screen == NULL ||
        screen->pixels == NULL ||
        screen->width == 0u ||
        screen->height == 0u ||
        theme == NULL ||
        theme->topbar_height == 0u ||
        theme->topbar_height >
            screen->height)
    {
        return false;
    }


    /*
     * Filesystem assets are optional.
     */
    gui_topbar_load_icons();


    /*
     * Main topbar.
     */
    if (!gui_window_create(
            &topbar_window,
            0,
            0,
            screen->width,
            theme->topbar_height,
            GUI_Z_TASKBAR))
    {
        return false;
    }


    /*
     * Popups open below the topbar.
     */
    int32_t popup_y =
        (int32_t)
            theme->topbar_height +
        GUI_TOPBAR_POPUP_GAP;


    /*
     * Start menu.
     */
    if (!gui_window_create(
            &start_menu_window,
            GUI_TOPBAR_LEFT_MARGIN,
            popup_y,
            GUI_START_MENU_WIDTH,
            GUI_START_MENU_HEIGHT,
            GUI_Z_POPUP))
    {
        gui_window_destroy(
            &topbar_window);

        return false;
    }


    /*
     * Power menu.
     */
    int32_t power_menu_x =
        (int32_t)
            screen->width -
        GUI_TOPBAR_RIGHT_MARGIN -
        GUI_POWER_MENU_WIDTH;

    if (!gui_window_create(
            &power_menu_window,
            power_menu_x,
            popup_y,
            GUI_POWER_MENU_WIDTH,
            GUI_POWER_MENU_HEIGHT,
            GUI_Z_POPUP))
    {
        gui_window_destroy(
            &start_menu_window);

        gui_window_destroy(
            &topbar_window);

        return false;
    }


    gui_window_set_visible(
        &start_menu_window,
        false);

    gui_window_set_visible(
        &power_menu_window,
        false);


    /*
     * Build all widget trees before allowing refresh.
     */
    if (!gui_topbar_build_main_widgets(
            screen->width,
            theme->topbar_height) ||
        !gui_topbar_build_start_menu() ||
        !gui_topbar_build_power_menu())
    {
        gui_window_destroy(
            &power_menu_window);

        gui_window_destroy(
            &start_menu_window);

        gui_window_destroy(
            &topbar_window);

        return false;
    }


    active_popup =
        GUI_TOPBAR_POPUP_NONE;

    hovered_button =
        NULL;

    pressed_button =
        NULL;


    /*
     * refresh() validates this flag.
     */
    topbar_initialized =
        true;


    /*
     * Render everything once.
     */
    if (!gui_topbar_render_widgets())
    {
        topbar_initialized =
            false;

        gui_window_destroy(
            &power_menu_window);

        gui_window_destroy(
            &start_menu_window);

        gui_window_destroy(
            &topbar_window);

        return false;
    }


    /*
     * NORMAL kernel task.
     */
    topbar_clock_task =
        task_create_kernel(
            gui_topbar_clock_thread,
            NULL);

    if (topbar_clock_task == NULL)
    {
        topbar_initialized =
            false;

        gui_window_destroy(
            &power_menu_window);

        gui_window_destroy(
            &start_menu_window);

        gui_window_destroy(
            &topbar_window);

        return false;
    }

    return true;
}


/*
 * ============================================================
 * Composition
 * ============================================================
 */

void gui_topbar_composite(void)
{
    if (!topbar_initialized)
        return;

    gui_window_composite(
        &topbar_window);

    if (active_popup ==
        GUI_TOPBAR_POPUP_START)
    {
        gui_window_composite(
            &start_menu_window);
    }
    else if (active_popup ==
             GUI_TOPBAR_POPUP_POWER)
    {
        gui_window_composite(
            &power_menu_window);
    }
}


/*
 * ============================================================
 * Pointer geometry
 * ============================================================
 */

static bool gui_topbar_point_in_window(
    const gui_window_t *window,
    int32_t x,
    int32_t y)
{
    if (window == NULL ||
        !window->visible)
    {
        return false;
    }

    gui_rect_t bounds =
        gui_window_bounds(
            window);

    int64_t right =
        (int64_t)
            bounds.x +
        (int64_t)
            bounds.width;

    int64_t bottom =
        (int64_t)
            bounds.y +
        (int64_t)
            bounds.height;

    return
        (int64_t)x >=
            (int64_t)
                bounds.x &&
        (int64_t)y >=
            (int64_t)
                bounds.y &&
        (int64_t)x <
            right &&
        (int64_t)y <
            bottom;
}


static gui_widget_t *gui_topbar_hit_window(
    gui_window_t *window,
    gui_widget_t *root,
    int32_t screen_x,
    int32_t screen_y)
{
    if (window == NULL ||
        root == NULL ||
        !window->visible)
    {
        return NULL;
    }

    if (!gui_topbar_point_in_window(
            window,
            screen_x,
            screen_y))
    {
        return NULL;
    }

    int32_t local_x =
        screen_x -
        window->x;

    int32_t local_y =
        screen_y -
        window->y;

    return
        gui_widget_hit_test(
            root,
            local_x,
            local_y);
}


/*
 * ============================================================
 * Button lookup
 * ============================================================
 */

static gui_button_t *gui_topbar_button_from_widget(
    gui_widget_t *widget)
{
    if (widget == NULL)
        return NULL;

    if (widget ==
        gui_button_widget(
            &meaty_button))
    {
        return
            &meaty_button;
    }

    if (widget ==
        gui_button_widget(
            &power_button))
    {
        return
            &power_button;
    }

    if (widget ==
        gui_button_widget(
            &explorer_button))
    {
        return
            &explorer_button;
    }

    if (widget ==
        gui_button_widget(
            &terminal_button))
    {
        return
            &terminal_button;
    }

    if (widget ==
        gui_button_widget(
            &settings_button))
    {
        return
            &settings_button;
    }

    if (widget ==
        gui_button_widget(
            &restart_button))
    {
        return
            &restart_button;
    }

    if (widget ==
        gui_button_widget(
            &shutdown_button))
    {
        return
            &shutdown_button;
    }

    return NULL;
}


/*
 * ============================================================
 * Hit testing
 * ============================================================
 */

static gui_widget_t *gui_topbar_hit_test(
    int32_t x,
    int32_t y)
{
    gui_widget_t *hit =
        NULL;

    /*
     * Active popup is visually above topbar.
     */
    if (active_popup ==
        GUI_TOPBAR_POPUP_START)
    {
        hit =
            gui_topbar_hit_window(
                &start_menu_window,
                &start_menu_root,
                x,
                y);

        if (hit != NULL)
            return hit;
    }
    else if (active_popup ==
             GUI_TOPBAR_POPUP_POWER)
    {
        hit =
            gui_topbar_hit_window(
                &power_menu_window,
                &power_menu_root,
                x,
                y);

        if (hit != NULL)
            return hit;
    }

    return
        gui_topbar_hit_window(
            &topbar_window,
            &topbar_root,
            x,
            y);
}


/*
 * ============================================================
 * Pointer dispatcher
 * ============================================================
 */

bool gui_topbar_handle_pointer(
    gui_input_event_type_t type,
    int32_t x,
    int32_t y,
    mouse_button_t button)
{
    if (!topbar_initialized)
        return false;

    gui_widget_t *hit =
        gui_topbar_hit_test(
            x,
            y);

    gui_button_t *button_hit =
        gui_topbar_button_from_widget(
            hit);

    bool over_shell =
        hit != NULL;


    /*
     * --------------------------------------------------------
     * Outside click closes popup.
     * --------------------------------------------------------
     */
    if (type ==
            GUI_INPUT_EVENT_MOUSE_BUTTON_DOWN &&
        button ==
            MOUSE_BUTTON_LEFT &&
        active_popup !=
            GUI_TOPBAR_POPUP_NONE &&
        hit == NULL)
    {
        gui_topbar_close_popup();

        if (hovered_button != NULL)
        {
            gui_button_set_hovered(
                hovered_button,
                false);

            hovered_button =
                NULL;
        }

        if (pressed_button != NULL)
        {
            gui_button_set_pressed(
                pressed_button,
                false);

            pressed_button =
                NULL;
        }

        (void)
            gui_topbar_render_widgets();

        gui_desktop_render();

        /*
         * Let this same click continue to normal-window focusing.
         */
        return false;
    }


    /*
     * --------------------------------------------------------
     * Hover
     * --------------------------------------------------------
     */
    if (type ==
        GUI_INPUT_EVENT_MOUSE_MOVE)
    {
        if (hovered_button !=
            button_hit)
        {
            if (hovered_button != NULL)
            {
                gui_button_set_hovered(
                    hovered_button,
                    false);
            }

            hovered_button =
                button_hit;

            if (hovered_button != NULL)
            {
                gui_button_set_hovered(
                    hovered_button,
                    true);
            }

            (void)
                gui_topbar_render_widgets();

            gui_desktop_render();
        }

        return over_shell;
    }


    if (button !=
        MOUSE_BUTTON_LEFT)
    {
        return over_shell;
    }


    /*
     * --------------------------------------------------------
     * Press
     * --------------------------------------------------------
     */
    if (type ==
        GUI_INPUT_EVENT_MOUSE_BUTTON_DOWN)
    {
        pressed_button =
            button_hit;

        if (pressed_button != NULL)
        {
            gui_button_set_pressed(
                pressed_button,
                true);

            (void)
                gui_topbar_render_widgets();

            gui_desktop_render();

            return true;
        }

        return over_shell;
    }


    /*
     * --------------------------------------------------------
     * Release / click
     * --------------------------------------------------------
     */
    if (type ==
        GUI_INPUT_EVENT_MOUSE_BUTTON_UP)
    {
        gui_button_t *released =
            pressed_button;

        if (released == NULL)
        {
            return over_shell;
        }

        gui_button_set_pressed(
            released,
            false);

        pressed_button =
            NULL;

        if (released ==
                button_hit &&
            released->widget.enabled)
        {
            gui_button_click(
                released);
        }

        (void)
            gui_topbar_render_widgets();

        gui_desktop_render();

        return true;
    }

    return over_shell;
}