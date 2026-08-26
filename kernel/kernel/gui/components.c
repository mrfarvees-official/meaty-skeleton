#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/gui/components.h>
#include <kernel/gui/font.h>
#include <kernel/gui/image.h>
#include <kernel/gui/painter.h>
#include <kernel/gui/surface.h>
#include <kernel/gui/theme.h>
#include <kernel/gui/widget.h>


/*
 * ============================================================
 * TEXT MEASUREMENT
 * ============================================================
 */

static int32_t gui_component_measure_text(
    gui_font_t *font,
    uint32_t pixel_height,
    const char *text)
{
    if (font == NULL ||
        text == NULL)
    {
        return 0;
    }

    int32_t width = 0;

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
 * ============================================================
 * PANEL
 * ============================================================
 */

static bool gui_panel_render(
    gui_widget_t *widget,
    gui_surface_t *destination,
    gui_rect_t bounds,
    gui_rect_t clip)
{
    (void)clip;

    if (widget == NULL ||
        destination == NULL)
    {
        return false;
    }

    gui_panel_t *panel =
        (gui_panel_t *)
            widget->context;

    if (panel == NULL)
        return false;

    if (panel->shadow_enabled)
    {
        gui_painter_draw_rounded_shadow(
            destination,
            bounds,
            panel->corner_radius,
            panel->shadow_offset_x,
            panel->shadow_offset_y,
            panel->shadow_spread,
            panel->shadow_blur,
            panel->shadow);
    }

    gui_painter_fill_rounded_vertical_gradient(
        destination,
        bounds,
        panel->corner_radius,
        panel->gradient_top,
        panel->gradient_bottom);

    if (panel->border_thickness != 0u)
    {
        gui_painter_stroke_rounded_rect(
            destination,
            bounds,
            panel->corner_radius,
            panel->border_thickness,
            panel->border);
    }

    return true;
}


static const gui_widget_operations_t
    gui_panel_operations =
{
    .render =
        gui_panel_render
};


void gui_panel_initialize(
    gui_panel_t *panel,
    gui_rect_t bounds)
{
    if (panel == NULL)
        return;

    const gui_theme_t *theme =
        gui_theme_default();

    gui_widget_initialize(
        &panel->widget,
        bounds,
        &gui_panel_operations,
        panel);

    panel->corner_radius =
        12u;

    panel->gradient_top =
        theme != NULL
            ? theme->taskbar_gradient_top
            : GUI_RGB(48u, 48u, 48u);

    panel->gradient_bottom =
        theme != NULL
            ? theme->taskbar_gradient_bottom
            : GUI_RGB(32u, 32u, 32u);

    panel->border =
        theme != NULL
            ? theme->taskbar_border
            : GUI_RGBA(255u, 255u, 255u, 48u);

    panel->border_thickness =
        1u;

    panel->shadow_enabled =
        false;

    panel->shadow =
        GUI_RGBA(0u, 0u, 0u, 64u);

    panel->shadow_offset_x =
        0;

    panel->shadow_offset_y =
        4;

    panel->shadow_spread =
        1u;

    panel->shadow_blur =
        8u;
}


gui_widget_t *gui_panel_widget(
    gui_panel_t *panel)
{
    if (panel == NULL)
        return NULL;

    return
        &panel->widget;
}


/*
 * ============================================================
 * LABEL
 * ============================================================
 */

static bool gui_label_render(
    gui_widget_t *widget,
    gui_surface_t *destination,
    gui_rect_t bounds,
    gui_rect_t clip)
{
    (void)clip;

    if (widget == NULL ||
        destination == NULL)
    {
        return false;
    }

    gui_label_t *label =
        (gui_label_t *)
            widget->context;

    if (label == NULL ||
        label->text == NULL ||
        label->font == NULL)
    {
        return true;
    }

    int32_t text_width =
        gui_component_measure_text(
            label->font,
            label->pixel_height,
            label->text);

    int32_t x =
        bounds.x;

    switch (label->alignment)
    {
    case GUI_TEXT_ALIGN_CENTER:

        x =
            bounds.x +
            ((int32_t)bounds.width -
             text_width) /
            2;

        break;

    case GUI_TEXT_ALIGN_RIGHT:

        x =
            bounds.x +
            (int32_t)bounds.width -
            text_width;

        break;

    case GUI_TEXT_ALIGN_LEFT:
    default:
        break;
    }

    int32_t y =
        bounds.y +
        ((int32_t)bounds.height -
         (int32_t)label->pixel_height) /
        2;

    return
        gui_font_draw_text(
            destination,
            label->font,
            x,
            y,
            label->pixel_height,
            label->text,
            label->color);
}


static const gui_widget_operations_t
    gui_label_operations =
{
    .render =
        gui_label_render
};


void gui_label_initialize(
    gui_label_t *label,
    gui_rect_t bounds,
    const char *text)
{
    if (label == NULL)
        return;

    const gui_theme_t *theme =
        gui_theme_default();

    gui_widget_initialize(
        &label->widget,
        bounds,
        &gui_label_operations,
        label);

    label->text =
        text;

    label->font =
        gui_font_default();

    label->pixel_height =
        16u;

    label->color =
        theme != NULL
            ? theme->text_primary
            : GUI_RGB(255u, 255u, 255u);

    label->alignment =
        GUI_TEXT_ALIGN_LEFT;
}


void gui_label_set_text(
    gui_label_t *label,
    const char *text)
{
    if (label == NULL)
        return;

    label->text =
        text;
}


gui_widget_t *gui_label_widget(
    gui_label_t *label)
{
    if (label == NULL)
        return NULL;

    return
        &label->widget;
}


/*
 * ============================================================
 * IMAGE
 * ============================================================
 */

static bool gui_image_widget_render(
    gui_widget_t *widget,
    gui_surface_t *destination,
    gui_rect_t bounds,
    gui_rect_t clip)
{
    (void)clip;

    if (widget == NULL ||
        destination == NULL)
    {
        return false;
    }

    gui_image_widget_t *image_widget =
        (gui_image_widget_t *)
            widget->context;

    if (image_widget == NULL ||
        image_widget->image == NULL)
    {
        return true;
    }

    gui_image_draw_scaled(
        destination,
        image_widget->image,
        bounds);

    return true;
}


static const gui_widget_operations_t
    gui_image_widget_operations =
{
    .render =
        gui_image_widget_render
};


void gui_image_widget_initialize(
    gui_image_widget_t *image_widget,
    gui_rect_t bounds,
    const gui_image_t *image)
{
    if (image_widget == NULL)
        return;

    gui_widget_initialize(
        &image_widget->widget,
        bounds,
        &gui_image_widget_operations,
        image_widget);

    image_widget->image =
        image;
}


void gui_image_widget_set_image(
    gui_image_widget_t *image_widget,
    const gui_image_t *image)
{
    if (image_widget == NULL)
        return;

    image_widget->image =
        image;
}


gui_widget_t *gui_image_widget_base(
    gui_image_widget_t *image_widget)
{
    if (image_widget == NULL)
        return NULL;

    return
        &image_widget->widget;
}


/*
 * ============================================================
 * BUTTON
 * ============================================================
 */

static bool gui_button_render(
    gui_widget_t *widget,
    gui_surface_t *destination,
    gui_rect_t bounds,
    gui_rect_t clip)
{
    (void)clip;

    if (widget == NULL ||
        destination == NULL)
    {
        return false;
    }

    gui_button_t *button =
        (gui_button_t *)
            widget->context;

    if (button == NULL)
        return false;

    gui_color_t background;

    if (!widget->enabled)
    {
        background =
            button->background_disabled;
    }
    else if (button->pressed)
    {
        background =
            button->background_pressed;
    }
    else if (button->hovered)
    {
        background =
            button->background_hover;
    }
    else
    {
        background =
            button->background_normal;
    }

    if (gui_color_alpha(
            background) != 0u)
    {
        gui_surface_fill_rounded_rect_blend(
            destination,
            bounds,
            button->corner_radius,
            background);
    }

    if (button->border_thickness != 0u &&
        gui_color_alpha(
            button->border) != 0u)
    {
        gui_painter_stroke_rounded_rect(
            destination,
            bounds,
            button->corner_radius,
            button->border_thickness,
            button->border);
    }

    int32_t content_x =
        bounds.x +
        (int32_t)
            button->horizontal_padding;

    uint32_t icon_size =
        0u;

    if (button->icon != NULL)
    {
        icon_size =
            bounds.height > 16u
                ? bounds.height - 16u
                : bounds.height;

        gui_rect_t icon_rect;

        icon_rect.x =
            content_x;

        icon_rect.y =
            bounds.y +
            ((int32_t)bounds.height -
             (int32_t)icon_size) /
            2;

        icon_rect.width =
            icon_size;

        icon_rect.height =
            icon_size;

        gui_image_draw_scaled(
            destination,
            button->icon,
            icon_rect);

        content_x +=
            (int32_t)icon_size +
            8;
    }

    if (button->text == NULL ||
        button->font == NULL)
    {
        return true;
    }

    gui_color_t text_color =
        widget->enabled
            ? button->text_color
            : button->disabled_text_color;

    int32_t text_y =
        bounds.y +
        ((int32_t)bounds.height -
         (int32_t)
             button->font_pixel_height) /
        2;

    return
        gui_font_draw_text(
            destination,
            button->font,
            content_x,
            text_y,
            button->font_pixel_height,
            button->text,
            text_color);
}


static const gui_widget_operations_t
    gui_button_operations =
{
    .render =
        gui_button_render
};


void gui_button_initialize(
    gui_button_t *button,
    gui_rect_t bounds,
    const char *text)
{
    if (button == NULL)
        return;

    const gui_theme_t *theme =
        gui_theme_default();

    gui_widget_initialize(
        &button->widget,
        bounds,
        &gui_button_operations,
        button);

    button->text =
        text;

    button->icon =
        NULL;

    button->font =
        gui_font_default();

    button->font_pixel_height =
        16u;

    button->text_color =
        theme != NULL
            ? theme->taskbar_text
            : GUI_RGB(245u, 245u, 245u);

    button->disabled_text_color =
        GUI_RGBA(
            210u,
            215u,
            225u,
            100u);

    button->background_normal =
        GUI_TRANSPARENT;

    button->background_hover =
        GUI_RGBA(
            255u,
            255u,
            255u,
            24u);

    button->background_pressed =
        GUI_RGBA(
            255u,
            255u,
            255u,
            42u);

    button->background_disabled =
        GUI_RGBA(
            255u,
            255u,
            255u,
            8u);

    button->border =
        GUI_TRANSPARENT;

    button->border_thickness =
        0u;

    button->corner_radius =
        10u;

    button->horizontal_padding =
        12u;

    button->hovered =
        false;

    button->pressed =
        false;

    button->on_click =
        NULL;

    button->click_context =
        NULL;
}


void gui_button_set_icon(
    gui_button_t *button,
    const gui_image_t *icon)
{
    if (button == NULL)
        return;

    button->icon =
        icon;
}


void gui_button_set_click_handler(
    gui_button_t *button,
    gui_button_click_fn handler,
    void *context)
{
    if (button == NULL)
        return;

    button->on_click =
        handler;

    button->click_context =
        context;
}


void gui_button_set_hovered(
    gui_button_t *button,
    bool hovered)
{
    if (button == NULL)
        return;

    button->hovered =
        hovered;
}


void gui_button_set_pressed(
    gui_button_t *button,
    bool pressed)
{
    if (button == NULL)
        return;

    button->pressed =
        pressed;
}


void gui_button_click(
    gui_button_t *button)
{
    if (button == NULL ||
        !button->widget.enabled ||
        button->on_click == NULL)
    {
        return;
    }

    button->on_click(
        button,
        button->click_context);
}


gui_widget_t *gui_button_widget(
    gui_button_t *button)
{
    if (button == NULL)
        return NULL;

    return
        &button->widget;
}