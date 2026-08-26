#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/font.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/topbar.h>
#include <kernel/gui/window.h>
#include <kernel/system_time.h>
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

static bool topbar_initialized;

static gui_window_t topbar_window;

static task_t *topbar_clock_task;

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

    uint64_t previous_second =
        UINT64_MAX;

    for (;;)
    {
        uint64_t current_second =
            timer_uptime_ms() /
            1000u;

        if (current_second !=
            previous_second)
        {
            previous_second =
                current_second;

            if (topbar_initialized &&
                gui_topbar_refresh())
            {
                /*
                 * Reconstruct from normal task context.
                 *
                 * Never repaint the GUI from IRQ0.
                 */
                gui_desktop_render();
            }
        }

        task_yield();
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