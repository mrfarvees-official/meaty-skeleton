#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/font.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/components.h>
#include <kernel/gui/topbar.h>
#include <kernel/gui/window.h>
#include <kernel/system_time.h>
#include <kernel/sleep_queue.h>
#include <kernel/task.h>
#include <kernel/timer.h>

#define GUI_TOPBAR_HORIZONTAL_PADDING \
    18

#define GUI_TOPBAR_FONT_PIXEL_HEIGHT \
    16u

#define GUI_TOPBAR_CLOCK_BUFFER_SIZE \
    40u

#define SYSTEM_TIME_DEFAULT_UTC_OFFSET_MINUTES \
    (5 * 60 + 30)

typedef enum gui_topbar_popup
{
    GUI_TOPBAR_POPUP_NONE = 0,
    GUI_TOPBAR_POPUP_START,
    GUI_TOPBAR_POPUP_POWER

} gui_topbar_popup_t;


static gui_topbar_popup_t
    active_popup;

static gui_button_t *
    hovered_button;

static gui_button_t *
    pressed_button;

static bool topbar_initialized;

static gui_window_t topbar_window;

static task_t *topbar_clock_task;

static gui_window_t topbar_window;
static gui_window_t start_menu_window;
static gui_window_t power_menu_window;

static gui_widget_t topbar_root;
static gui_widget_t start_menu_root;
static gui_widget_t power_menu_root;

static gui_button_t meaty_button;
static gui_button_t power_button;

static gui_button_t explorer_button;
static gui_button_t terminal_button;
static gui_button_t settings_button;

static gui_button_t restart_button;
static gui_button_t shutdown_button;

/*
 * ------------------------------------------------------------
 * Calendar helpers
 * ------------------------------------------------------------
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
            4u, 6u, 2u, 4u};

    uint32_t calculation_year =
        year;

    if (month < 3u)
        --calculation_year;

    return (uint8_t)((calculation_year +
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
        (char)('0' + ((value / 10u) % 10u));

    *destination++ =
        (char)('0' + (value % 10u));

    return destination;
}

static char *gui_topbar_append_year(
    char *destination,
    uint16_t year)
{
    *destination++ =
        (char)('0' + ((year / 1000u) % 10u));

    *destination++ =
        (char)('0' + ((year / 100u) % 10u));

    *destination++ =
        (char)('0' + ((year / 10u) % 10u));

    *destination++ =
        (char)('0' + (year % 10u));

    return destination;
}

static bool gui_topbar_format_datetime(
    const rtc_datetime_t *datetime,
    char *buffer,
    size_t capacity)
{
    if (datetime == NULL ||
        buffer == NULL ||
        capacity < GUI_TOPBAR_CLOCK_BUFFER_SIZE)
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
        (datetime->hour % 12u);

    if (hour_12 == 0u)
        hour_12 = 12u;

    bool afternoon =
        datetime->hour >= 12u;

    char *cursor =
        buffer;

    cursor =
        gui_topbar_append_text(
            cursor,
            weekday_names[weekday]);

    *cursor++ = ' ';

    cursor =
        gui_topbar_append_text(
            cursor,
            month_names[
                datetime->month - 1u]);

    *cursor++ = ' ';

    if (datetime->day >= 10u)
    {
        *cursor++ =
            (char)
            ('0' +
             datetime->day / 10u);
    }

    *cursor++ =
        (char)
        ('0' +
         datetime->day % 10u);

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
             hour_12 / 10u);
    }

    *cursor++ =
        (char)
        ('0' +
         hour_12 % 10u);

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
 * ------------------------------------------------------------
 * Text width
 * ------------------------------------------------------------
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
            (uint8_t)*text++;

        if (codepoint >= 0x80u)
            codepoint = '?';

        gui_font_glyph_metrics_t metrics;

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
 * ------------------------------------------------------------
 * Surface rendering
 * ------------------------------------------------------------
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

    gui_surface_clear(
        surface,
        GUI_TRANSPARENT);

    gui_rect_t bar_rect;

    bar_rect.x = 0;
    bar_rect.y = 0;

    bar_rect.width =
        surface->width;

    bar_rect.height =
        surface->height;

    gui_surface_fill_rect(
        surface,
        bar_rect,
        theme->topbar_background);

    /*
     * Fine separator along the lower edge.
     */
    if (surface->height > 0u)
    {
        gui_rect_t border;

        border.x = 0;

        border.y =
            (int32_t)(surface->height - 1u);

        border.width =
            surface->width;

        border.height =
            1u;

        gui_surface_fill_rect(
            surface,
            border,
            theme->topbar_border);
    }

    int32_t text_y =
        ((int32_t)surface->height -
         (int32_t)GUI_TOPBAR_FONT_PIXEL_HEIGHT) /
        2;

    if (!gui_font_draw_text(
            surface,
            font,
            GUI_TOPBAR_HORIZONTAL_PADDING,
            text_y,
            GUI_TOPBAR_FONT_PIXEL_HEIGHT,
            "Meaty OS",
            theme->topbar_text))
    {
        return false;
    }

    rtc_datetime_t datetime;

    char datetime_text[GUI_TOPBAR_CLOCK_BUFFER_SIZE];

    if (system_time_local_datetime(
            &datetime) &&
        gui_topbar_format_datetime(
            &datetime,
            datetime_text,
            sizeof(datetime_text)))
    {
        int32_t text_width =
            gui_topbar_measure_text(
                font,
                GUI_TOPBAR_FONT_PIXEL_HEIGHT,
                datetime_text);

        int32_t text_x =
            (int32_t)surface->width -
            GUI_TOPBAR_HORIZONTAL_PADDING -
            text_width;

        if (text_x <
            GUI_TOPBAR_HORIZONTAL_PADDING)
        {
            text_x =
                GUI_TOPBAR_HORIZONTAL_PADDING;
        }

        if (!gui_font_draw_text(
                surface,
                font,
                text_x,
                text_y,
                GUI_TOPBAR_FONT_PIXEL_HEIGHT,
                datetime_text,
                theme->topbar_text))
        {
            return false;
        }
    }
    else
    {
        const char *unavailable =
            "RTC unavailable";

        int32_t text_width =
            gui_topbar_measure_text(
                font,
                GUI_TOPBAR_FONT_PIXEL_HEIGHT,
                unavailable);

        int32_t text_x =
            (int32_t)surface->width -
            GUI_TOPBAR_HORIZONTAL_PADDING -
            text_width;

        if (!gui_font_draw_text(
                surface,
                font,
                text_x,
                text_y,
                GUI_TOPBAR_FONT_PIXEL_HEIGHT,
                unavailable,
                theme->topbar_text_secondary))
        {
            return false;
        }
    }

    return true;
}

/*
 * ------------------------------------------------------------
 * Clock refresh task
 * ------------------------------------------------------------
 */

static void gui_topbar_clock_thread(
    void *argument)
{
    (void)argument;

    for (;;)
    {
        /*
         * Dynamic GUI work deliberately occurs from normal task
         * context, never from IRQ0.
         */
        if (topbar_initialized &&
            gui_topbar_refresh())
        {
            gui_desktop_render();
        }

        /*
         * The topbar only needs second-level clock updates.
         *
         * Sleep instead of remaining continuously runnable and
         * polling timer_uptime_ms() on every scheduler pass.
         */
        task_sleep(1000u);
    }
}

/*
 * ------------------------------------------------------------
 * Initialization
 * ------------------------------------------------------------
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
     * Set initialized before rendering because refresh() validates
     * this ownership state.
     */
    topbar_initialized =
        true;

    if (!gui_topbar_refresh())
    {
        topbar_initialized =
            false;

        gui_window_destroy(
            &topbar_window);

        return false;
    }

    topbar_clock_task =
        task_create_kernel(
            gui_topbar_clock_thread,
            NULL);

    if (topbar_clock_task == NULL)
    {
        topbar_initialized =
            false;

        gui_window_destroy(
            &topbar_window);

        return false;
    }

    return true;
}

void gui_topbar_composite(void)
{
    if (!topbar_initialized)
        return;

    gui_window_composite(
        &topbar_window);
}

/*
 * ------------------------------------------------------------
 * Pointer geometry
 * ------------------------------------------------------------
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
        (int64_t)bounds.x +
        (int64_t)bounds.width;

    int64_t bottom =
        (int64_t)bounds.y +
        (int64_t)bounds.height;

    return
        (int64_t)x >=
            (int64_t)bounds.x &&
        (int64_t)y >=
            (int64_t)bounds.y &&
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

    /*
     * Widget coordinates are local to the window surface.
     */
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
 * ------------------------------------------------------------
 * Widget -> button mapping
 * ------------------------------------------------------------
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
 * ------------------------------------------------------------
 * Popup state
 * ------------------------------------------------------------
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
 * ------------------------------------------------------------
 * Surface refresh
 * ------------------------------------------------------------
 *
 * The topbar clock has its own refresh path, so here we only need
 * to rebuild widget-owned shell surfaces after pointer-state changes.
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


static bool gui_topbar_render_widgets(void)
{
    /*
     * If your main topbar still draws the clock separately inside
     * gui_topbar_refresh(), do not clear/redraw topbar_window here.
     *
     * Instead gui_topbar_refresh() should render the background,
     * clock, and topbar_root in one pass.
     */

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
 * ------------------------------------------------------------
 * Topbar hit-test
 * ------------------------------------------------------------
 */

static gui_widget_t *gui_topbar_hit_test(
    int32_t x,
    int32_t y)
{
    gui_widget_t *hit =
        NULL;

    /*
     * Popups sit above the system bar and therefore get first
     * refusal.
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

    /*
     * Then the actual topbar.
     */
    return
        gui_topbar_hit_window(
            &topbar_window,
            &topbar_root,
            x,
            y);
}


/*
 * ------------------------------------------------------------
 * Public pointer dispatcher
 * ------------------------------------------------------------
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

    /*
     * hit != NULL means the pointer is currently over a topbar or
     * popup widget.
     */
    bool over_shell =
        hit != NULL;

    /*
     * --------------------------------------------------------
     * Outside click closes popup.
     * --------------------------------------------------------
     *
     * Do not consume the event after closing.
     *
     * This lets the same click continue down to normal-window
     * focusing.
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

        /*
         * Cancel stale interaction state.
         */
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

        (void)gui_topbar_render_widgets();

        gui_desktop_render();

        return false;
    }

    /*
     * --------------------------------------------------------
     * Pointer movement / hover
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

            (void)gui_topbar_render_widgets();

            gui_desktop_render();
        }

        return over_shell;
    }

    /*
     * Current GUI controls only respond to the primary button.
     */
    if (button !=
        MOUSE_BUTTON_LEFT)
    {
        return over_shell;
    }

    /*
     * --------------------------------------------------------
     * Button down
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

            (void)gui_topbar_render_widgets();

            gui_desktop_render();

            return true;
        }

        /*
         * Clicking non-button topbar material still belongs to the
         * shell and should not focus an application underneath it.
         */
        return over_shell;
    }

    /*
     * --------------------------------------------------------
     * Button up / click
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

        /*
         * A click occurs only if:
         *
         *     down occurred on the button
         *     up occurred on the same button
         *     button remains enabled
         */
        if (released ==
                button_hit &&
            released->widget.enabled)
        {
            gui_button_click(
                released);
        }

        (void)gui_topbar_render_widgets();

        gui_desktop_render();

        return true;
    }

    return over_shell;
}

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