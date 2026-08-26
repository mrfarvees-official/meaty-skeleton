#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/gui/components.h>
#include <kernel/gui/compositor.h>
#include <kernel/gui/desktop.h>
#include <kernel/gui/image.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/taskbar.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/widget.h>
#include <kernel/gui/window.h>


#define GUI_TASKBAR_WIDTH_PERCENT \
    75u

#define GUI_TASKBAR_ICON_SIZE \
    48u

#define GUI_TASKBAR_ICON_PADDING_Y \
    8u

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


#define GUI_TASKBAR_START_WIDTH \
    132u

#define GUI_TASKBAR_CONTROL_MARGIN \
    8u

#define GUI_TASKBAR_CONTROL_HEIGHT \
    48u

#define GUI_TASKBAR_POWER_WIDTH \
    48u


#define GUI_START_MENU_WIDTH \
    280u

#define GUI_START_MENU_HEIGHT \
    246u

#define GUI_POWER_MENU_WIDTH \
    220u

#define GUI_POWER_MENU_HEIGHT \
    142u

#define GUI_POPUP_GAP \
    10u


#define GUI_SYSTEM_POWER_ICON \
    "/icons/system/power.png"


typedef enum gui_taskbar_popup
{
    GUI_TASKBAR_POPUP_NONE = 0,

    GUI_TASKBAR_POPUP_START,
    GUI_TASKBAR_POPUP_POWER

} gui_taskbar_popup_t;


/*
 * ------------------------------------------------------------
 * Windows
 * ------------------------------------------------------------
 */

static bool
    taskbar_initialized;

static gui_window_t
    taskbar_window;

static gui_window_t
    start_menu_window;

static gui_window_t
    power_menu_window;


/*
 * ------------------------------------------------------------
 * Dock widgets
 * ------------------------------------------------------------
 */

static gui_widget_t
    taskbar_root;

static gui_panel_t
    taskbar_panel;

static gui_button_t
    start_button;

static gui_button_t
    power_button;


/*
 * ------------------------------------------------------------
 * Start menu
 * ------------------------------------------------------------
 */

static gui_widget_t
    start_menu_root;

static gui_panel_t
    start_menu_panel;

static gui_label_t
    start_menu_title;

static gui_button_t
    start_explorer_button;

static gui_button_t
    start_terminal_button;

static gui_button_t
    start_settings_button;

static gui_label_t
    start_menu_status;


/*
 * ------------------------------------------------------------
 * Power menu
 * ------------------------------------------------------------
 */

static gui_widget_t
    power_menu_root;

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
 * Interaction state
 * ------------------------------------------------------------
 */

static gui_taskbar_popup_t
    active_popup;

static gui_button_t *
    hovered_button;

static gui_button_t *
    pressed_button;

static const gui_image_t *
    power_icon;


/*
 * ------------------------------------------------------------
 * Geometry
 * ------------------------------------------------------------
 */

static bool gui_taskbar_point_in_window(
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
        bounds.width;

    int64_t bottom =
        (int64_t)bounds.y +
        bounds.height;

    return
        (int64_t)x >= bounds.x &&
        (int64_t)y >= bounds.y &&
        (int64_t)x < right &&
        (int64_t)y < bottom;
}


static gui_widget_t *gui_taskbar_hit_window(
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

    if (!gui_taskbar_point_in_window(
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
 * ------------------------------------------------------------
 * Button lookup
 * ------------------------------------------------------------
 */

static gui_button_t *gui_taskbar_button_from_widget(
    gui_widget_t *widget)
{
    if (widget == NULL)
        return NULL;

    if (widget ==
        gui_button_widget(
            &start_button))
    {
        return
            &start_button;
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
            &start_explorer_button))
    {
        return
            &start_explorer_button;
    }

    if (widget ==
        gui_button_widget(
            &start_terminal_button))
    {
        return
            &start_terminal_button;
    }

    if (widget ==
        gui_button_widget(
            &start_settings_button))
    {
        return
            &start_settings_button;
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

static void gui_taskbar_set_popup(
    gui_taskbar_popup_t popup)
{
    active_popup =
        popup;

    gui_window_set_visible(
        &start_menu_window,
        popup ==
            GUI_TASKBAR_POPUP_START);

    gui_window_set_visible(
        &power_menu_window,
        popup ==
            GUI_TASKBAR_POPUP_POWER);
}


static void gui_taskbar_close_popup(void)
{
    gui_taskbar_set_popup(
        GUI_TASKBAR_POPUP_NONE);
}


/*
 * ------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------
 */

static void gui_taskbar_start_clicked(
    gui_button_t *button,
    void *context)
{
    (void)button;
    (void)context;

    if (active_popup ==
        GUI_TASKBAR_POPUP_START)
    {
        gui_taskbar_close_popup();
    }
    else
    {
        gui_taskbar_set_popup(
            GUI_TASKBAR_POPUP_START);
    }
}


static void gui_taskbar_power_clicked(
    gui_button_t *button,
    void *context)
{
    (void)button;
    (void)context;

    if (active_popup ==
        GUI_TASKBAR_POPUP_POWER)
    {
        gui_taskbar_close_popup();
    }
    else
    {
        gui_taskbar_set_popup(
            GUI_TASKBAR_POPUP_POWER);
    }
}


/*
 * Temporary menu items.
 *
 * They intentionally do not launch applications yet.
 */
static void gui_taskbar_start_item_clicked(
    gui_button_t *button,
    void *context)
{
    (void)button;
    (void)context;

    gui_taskbar_close_popup();
}


/*
 * ------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------
 */

static bool gui_taskbar_render_window(
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


static bool gui_taskbar_render_all(void)
{
    if (!gui_taskbar_render_window(
            &taskbar_window,
            &taskbar_root))
    {
        return false;
    }

    if (!gui_taskbar_render_window(
            &start_menu_window,
            &start_menu_root))
    {
        return false;
    }

    if (!gui_taskbar_render_window(
            &power_menu_window,
            &power_menu_root))
    {
        return false;
    }

    return true;
}


/*
 * ------------------------------------------------------------
 * Dock widgets
 * ------------------------------------------------------------
 */

static bool gui_taskbar_build_dock_widgets(
    uint32_t surface_width,
    uint32_t surface_height)
{
    const gui_theme_t *theme =
        gui_theme_default();

    if (theme == NULL)
        return false;

    gui_rect_t root_bounds =
    {
        .x = 0,
        .y = 0,
        .width = surface_width,
        .height = surface_height
    };

    gui_widget_initialize(
        &taskbar_root,
        root_bounds,
        NULL,
        NULL);

    gui_rect_t panel_bounds =
    {
        .x =
            GUI_TASKBAR_SHADOW_PADDING_X,

        .y =
            GUI_TASKBAR_SHADOW_PADDING_TOP,

        .width =
            surface_width -
            GUI_TASKBAR_SHADOW_PADDING_X *
            2u,

        .height =
            GUI_TASKBAR_PANEL_HEIGHT
    };

    gui_panel_initialize(
        &taskbar_panel,
        panel_bounds);

    taskbar_panel.corner_radius =
        theme->taskbar_corner_radius;

    taskbar_panel.gradient_top =
        theme->taskbar_gradient_top;

    taskbar_panel.gradient_bottom =
        theme->taskbar_gradient_bottom;

    taskbar_panel.border =
        theme->taskbar_border;

    taskbar_panel.border_thickness =
        theme->taskbar_border_thickness;

    taskbar_panel.shadow_enabled =
        true;

    taskbar_panel.shadow =
        theme->taskbar_shadow;

    taskbar_panel.shadow_offset_x =
        theme->taskbar_shadow_offset_x;

    taskbar_panel.shadow_offset_y =
        theme->taskbar_shadow_offset_y;

    taskbar_panel.shadow_spread =
        theme->taskbar_shadow_spread;

    taskbar_panel.shadow_blur =
        theme->taskbar_shadow_blur;

    if (!gui_widget_add_child(
            &taskbar_root,
            gui_panel_widget(
                &taskbar_panel)))
    {
        return false;
    }

    gui_rect_t start_bounds =
    {
        .x =
            GUI_TASKBAR_CONTROL_MARGIN,

        .y =
            GUI_TASKBAR_ICON_PADDING_Y,

        .width =
            GUI_TASKBAR_START_WIDTH,

        .height =
            GUI_TASKBAR_CONTROL_HEIGHT
    };

    gui_button_initialize(
        &start_button,
        start_bounds,
        "Meaty OS");

    start_button.font_pixel_height =
        17u;

    start_button.text_color =
        theme->taskbar_text;

    gui_button_set_click_handler(
        &start_button,
        gui_taskbar_start_clicked,
        NULL);

    if (!gui_widget_add_child(
            gui_panel_widget(
                &taskbar_panel),
            gui_button_widget(
                &start_button)))
    {
        return false;
    }

    gui_rect_t power_bounds =
    {
        .x =
            (int32_t)
            (panel_bounds.width -
             GUI_TASKBAR_CONTROL_MARGIN -
             GUI_TASKBAR_POWER_WIDTH),

        .y =
            GUI_TASKBAR_ICON_PADDING_Y,

        .width =
            GUI_TASKBAR_POWER_WIDTH,

        .height =
            GUI_TASKBAR_CONTROL_HEIGHT
    };

    gui_button_initialize(
        &power_button,
        power_bounds,
        NULL);

    power_button.horizontal_padding =
        8u;

    gui_button_set_icon(
        &power_button,
        power_icon);

    /*
     * Asset fallback:
     * if /icons/system/power.png is unavailable,
     * retain a visible textual power control.
     */
    if (power_icon == NULL)
    {
        power_button.text =
            "P";

        power_button.horizontal_padding =
            17u;
    }

    gui_button_set_click_handler(
        &power_button,
        gui_taskbar_power_clicked,
        NULL);

    if (!gui_widget_add_child(
            gui_panel_widget(
                &taskbar_panel),
            gui_button_widget(
                &power_button)))
    {
        return false;
    }

    return true;
}


/*
 * ------------------------------------------------------------
 * Start menu widgets
 * ------------------------------------------------------------
 */

static bool gui_taskbar_build_start_menu_widgets(void)
{
    const gui_theme_t *theme =
        gui_theme_default();

    if (theme == NULL)
        return false;

    gui_rect_t root_bounds =
    {
        .x = 0,
        .y = 0,
        .width = GUI_START_MENU_WIDTH,
        .height = GUI_START_MENU_HEIGHT
    };

    gui_widget_initialize(
        &start_menu_root,
        root_bounds,
        NULL,
        NULL);

    gui_rect_t panel_bounds =
    {
        .x = 8,
        .y = 8,
        .width = GUI_START_MENU_WIDTH - 16u,
        .height = GUI_START_MENU_HEIGHT - 16u
    };

    gui_panel_initialize(
        &start_menu_panel,
        panel_bounds);

    start_menu_panel.corner_radius =
        16u;

    start_menu_panel.shadow_enabled =
        true;

    start_menu_panel.shadow =
        theme->taskbar_shadow;

    if (!gui_widget_add_child(
            &start_menu_root,
            gui_panel_widget(
                &start_menu_panel)))
    {
        return false;
    }

    gui_rect_t title_bounds =
    {
        .x = 18,
        .y = 14,
        .width = panel_bounds.width - 36u,
        .height = 36u
    };

    gui_label_initialize(
        &start_menu_title,
        title_bounds,
        "Meaty OS");

    start_menu_title.pixel_height =
        19u;

    start_menu_title.color =
        theme->taskbar_text;

    if (!gui_widget_add_child(
            gui_panel_widget(
                &start_menu_panel),
            gui_label_widget(
                &start_menu_title)))
    {
        return false;
    }

    gui_rect_t item =
    {
        .x = 12,
        .y = 58,
        .width = panel_bounds.width - 24u,
        .height = 42u
    };

    gui_button_initialize(
        &start_explorer_button,
        item,
        "Explorer");

    gui_button_set_click_handler(
        &start_explorer_button,
        gui_taskbar_start_item_clicked,
        NULL);

    gui_widget_add_child(
        gui_panel_widget(
            &start_menu_panel),
        gui_button_widget(
            &start_explorer_button));

    item.y += 44;

    gui_button_initialize(
        &start_terminal_button,
        item,
        "Terminal");

    gui_button_set_click_handler(
        &start_terminal_button,
        gui_taskbar_start_item_clicked,
        NULL);

    gui_widget_add_child(
        gui_panel_widget(
            &start_menu_panel),
        gui_button_widget(
            &start_terminal_button));

    item.y += 44;

    gui_button_initialize(
        &start_settings_button,
        item,
        "Settings");

    gui_button_set_click_handler(
        &start_settings_button,
        gui_taskbar_start_item_clicked,
        NULL);

    gui_widget_add_child(
        gui_panel_widget(
            &start_menu_panel),
        gui_button_widget(
            &start_settings_button));

    gui_rect_t status_bounds =
    {
        .x = 18,
        .y = 194,
        .width = panel_bounds.width - 36u,
        .height = 24u
    };

    gui_label_initialize(
        &start_menu_status,
        status_bounds,
        "Applications coming next");

    start_menu_status.pixel_height =
        13u;

    start_menu_status.color =
        theme->topbar_text_secondary;

    gui_widget_add_child(
        gui_panel_widget(
            &start_menu_panel),
        gui_label_widget(
            &start_menu_status));

    return true;
}


/*
 * ------------------------------------------------------------
 * Power menu widgets
 * ------------------------------------------------------------
 */

static bool gui_taskbar_build_power_menu_widgets(void)
{
    const gui_theme_t *theme =
        gui_theme_default();

    if (theme == NULL)
        return false;

    gui_rect_t root_bounds =
    {
        .x = 0,
        .y = 0,
        .width = GUI_POWER_MENU_WIDTH,
        .height = GUI_POWER_MENU_HEIGHT
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
        .width = GUI_POWER_MENU_WIDTH - 16u,
        .height = GUI_POWER_MENU_HEIGHT - 16u
    };

    gui_panel_initialize(
        &power_menu_panel,
        panel_bounds);

    power_menu_panel.corner_radius =
        16u;

    power_menu_panel.shadow_enabled =
        true;

    power_menu_panel.shadow =
        theme->taskbar_shadow;

    gui_widget_add_child(
        &power_menu_root,
        gui_panel_widget(
            &power_menu_panel));

    gui_rect_t title_bounds =
    {
        .x = 16,
        .y = 10,
        .width = panel_bounds.width - 32u,
        .height = 30u
    };

    gui_label_initialize(
        &power_menu_title,
        title_bounds,
        "Power");

    power_menu_title.pixel_height =
        17u;

    power_menu_title.color =
        theme->taskbar_text;

    gui_widget_add_child(
        gui_panel_widget(
            &power_menu_panel),
        gui_label_widget(
            &power_menu_title));

    gui_rect_t restart_bounds =
    {
        .x = 10,
        .y = 44,
        .width = panel_bounds.width - 20u,
        .height = 34u
    };

    gui_button_initialize(
        &restart_button,
        restart_bounds,
        "Restart");

    /*
     * Deliberately disabled until generic system_power_restart()
     * exists.
     */
    gui_widget_set_enabled(
        gui_button_widget(
            &restart_button),
        false);

    gui_widget_add_child(
        gui_panel_widget(
            &power_menu_panel),
        gui_button_widget(
            &restart_button));

    gui_rect_t shutdown_bounds =
    {
        .x = 10,
        .y = 82,
        .width = panel_bounds.width - 20u,
        .height = 34u
    };

    gui_button_initialize(
        &shutdown_button,
        shutdown_bounds,
        "Shut Down");

    /*
     * Deliberately disabled until ACPI-backed system shutdown exists.
     */
    gui_widget_set_enabled(
        gui_button_widget(
            &shutdown_button),
        false);

    gui_widget_add_child(
        gui_panel_widget(
            &power_menu_panel),
        gui_button_widget(
            &shutdown_button));

    return true;
}


/*
 * ------------------------------------------------------------
 * Public initialization
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

    power_icon =
        NULL;

    (void)gui_image_get(
        GUI_SYSTEM_POWER_ICON,
        &power_icon);

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

    /*
     * Start menu is anchored above the left side of the visible dock.
     */
    int32_t start_menu_x =
        (int32_t)panel_x;

    int32_t start_menu_y =
        (int32_t)visible_panel_y -
        GUI_START_MENU_HEIGHT -
        GUI_POPUP_GAP;

    if (!gui_window_create(
            &start_menu_window,
            start_menu_x,
            start_menu_y,
            GUI_START_MENU_WIDTH,
            GUI_START_MENU_HEIGHT,
            GUI_Z_POPUP))
    {
        gui_window_destroy(
            &taskbar_window);

        return false;
    }

    /*
     * Power menu is anchored above the right side of the visible dock.
     */
    int32_t power_menu_x =
        (int32_t)
        (panel_x +
         panel_width -
         GUI_POWER_MENU_WIDTH);

    int32_t power_menu_y =
        (int32_t)visible_panel_y -
        GUI_POWER_MENU_HEIGHT -
        GUI_POPUP_GAP;

    if (!gui_window_create(
            &power_menu_window,
            power_menu_x,
            power_menu_y,
            GUI_POWER_MENU_WIDTH,
            GUI_POWER_MENU_HEIGHT,
            GUI_Z_POPUP))
    {
        gui_window_destroy(
            &start_menu_window);

        gui_window_destroy(
            &taskbar_window);

        return false;
    }

    gui_window_set_visible(
        &start_menu_window,
        false);

    gui_window_set_visible(
        &power_menu_window,
        false);

    if (!gui_taskbar_build_dock_widgets(
            surface_width,
            surface_height) ||
        !gui_taskbar_build_start_menu_widgets() ||
        !gui_taskbar_build_power_menu_widgets())
    {
        gui_window_destroy(
            &power_menu_window);

        gui_window_destroy(
            &start_menu_window);

        gui_window_destroy(
            &taskbar_window);

        return false;
    }

    if (!gui_taskbar_render_all())
    {
        gui_window_destroy(
            &power_menu_window);

        gui_window_destroy(
            &start_menu_window);

        gui_window_destroy(
            &taskbar_window);

        return false;
    }

    active_popup =
        GUI_TASKBAR_POPUP_NONE;

    hovered_button =
        NULL;

    pressed_button =
        NULL;

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

    /*
     * Taskbar first.
     */
    gui_window_composite(
        &taskbar_window);

    /*
     * Popup afterwards so GUI_Z_POPUP visually sits above dock chrome.
     */
    if (active_popup ==
        GUI_TASKBAR_POPUP_START)
    {
        gui_window_composite(
            &start_menu_window);
    }
    else if (active_popup ==
             GUI_TASKBAR_POPUP_POWER)
    {
        gui_window_composite(
            &power_menu_window);
    }
}


/*
 * ------------------------------------------------------------
 * Pointer hit testing
 * ------------------------------------------------------------
 */

static gui_widget_t *gui_taskbar_hit_test(
    int32_t x,
    int32_t y)
{
    gui_widget_t *hit =
        NULL;

    /*
     * Active popup gets first refusal.
     */
    if (active_popup ==
        GUI_TASKBAR_POPUP_START)
    {
        hit =
            gui_taskbar_hit_window(
                &start_menu_window,
                &start_menu_root,
                x,
                y);

        if (hit != NULL)
            return hit;
    }
    else if (active_popup ==
             GUI_TASKBAR_POPUP_POWER)
    {
        hit =
            gui_taskbar_hit_window(
                &power_menu_window,
                &power_menu_root,
                x,
                y);

        if (hit != NULL)
            return hit;
    }

    return
        gui_taskbar_hit_window(
            &taskbar_window,
            &taskbar_root,
            x,
            y);
}


/*
 * ------------------------------------------------------------
 * Pointer dispatch
 * ------------------------------------------------------------
 */

bool gui_taskbar_handle_pointer(
    gui_input_event_type_t type,
    int32_t x,
    int32_t y,
    mouse_button_t button)
{
    if (!taskbar_initialized)
        return false;

    gui_widget_t *hit =
        gui_taskbar_hit_test(
            x,
            y);

    gui_button_t *button_hit =
        gui_taskbar_button_from_widget(
            hit);

    bool over_shell =
        hit != NULL;

    /*
     * Clicking outside closes menus but does not consume the click,
     * allowing the normal window beneath it to receive focus.
     */
    if (type ==
            GUI_INPUT_EVENT_MOUSE_BUTTON_DOWN &&
        button ==
            MOUSE_BUTTON_LEFT &&
        active_popup !=
            GUI_TASKBAR_POPUP_NONE &&
        hit == NULL)
    {
        gui_taskbar_close_popup();

        (void)gui_taskbar_render_all();

        gui_desktop_render();

        return false;
    }

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

            (void)gui_taskbar_render_all();

            gui_desktop_render();
        }

        return over_shell;
    }

    if (button !=
        MOUSE_BUTTON_LEFT)
    {
        return over_shell;
    }

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

            (void)gui_taskbar_render_all();

            gui_desktop_render();

            return true;
        }

        return over_shell;
    }

    if (type ==
        GUI_INPUT_EVENT_MOUSE_BUTTON_UP)
    {
        gui_button_t *released =
            pressed_button;

        if (released != NULL)
        {
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

            (void)gui_taskbar_render_all();

            gui_desktop_render();

            return true;
        }

        return over_shell;
    }

    return over_shell;
}