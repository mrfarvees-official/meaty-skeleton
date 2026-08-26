#ifndef KERNEL_GUI_COMPONENTS_H
#define KERNEL_GUI_COMPONENTS_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/gui/font.h>
#include <kernel/gui/image.h>
#include <kernel/gui/widget.h>


typedef enum gui_text_align
{
    GUI_TEXT_ALIGN_LEFT = 0,
    GUI_TEXT_ALIGN_CENTER,
    GUI_TEXT_ALIGN_RIGHT

} gui_text_align_t;


/*
 * ============================================================
 * PANEL
 * ============================================================
 */

typedef struct gui_panel
{
    gui_widget_t widget;

    uint32_t corner_radius;

    gui_color_t gradient_top;
    gui_color_t gradient_bottom;

    gui_color_t border;
    uint32_t border_thickness;

    bool shadow_enabled;

    gui_color_t shadow;

    int32_t shadow_offset_x;
    int32_t shadow_offset_y;

    uint32_t shadow_spread;
    uint32_t shadow_blur;

} gui_panel_t;


void gui_panel_initialize(
    gui_panel_t *panel,
    gui_rect_t bounds);

gui_widget_t *gui_panel_widget(
    gui_panel_t *panel);


/*
 * ============================================================
 * LABEL
 * ============================================================
 */

typedef struct gui_label
{
    gui_widget_t widget;

    const char *text;

    gui_font_t *font;

    uint32_t pixel_height;

    gui_color_t color;

    gui_text_align_t alignment;

} gui_label_t;


void gui_label_initialize(
    gui_label_t *label,
    gui_rect_t bounds,
    const char *text);

void gui_label_set_text(
    gui_label_t *label,
    const char *text);

gui_widget_t *gui_label_widget(
    gui_label_t *label);


/*
 * ============================================================
 * IMAGE
 * ============================================================
 */

typedef struct gui_image_widget
{
    gui_widget_t widget;

    const gui_image_t *image;

} gui_image_widget_t;


void gui_image_widget_initialize(
    gui_image_widget_t *image_widget,
    gui_rect_t bounds,
    const gui_image_t *image);

void gui_image_widget_set_image(
    gui_image_widget_t *image_widget,
    const gui_image_t *image);

gui_widget_t *gui_image_widget_base(
    gui_image_widget_t *image_widget);


/*
 * ============================================================
 * BUTTON
 * ============================================================
 */

typedef struct gui_button gui_button_t;

typedef void (*gui_button_click_fn)(
    gui_button_t *button,
    void *context);


struct gui_button
{
    gui_widget_t widget;

    const char *text;

    const gui_image_t *icon;

    gui_font_t *font;

    uint32_t font_pixel_height;

    gui_color_t text_color;
    gui_color_t disabled_text_color;

    gui_color_t background_normal;
    gui_color_t background_hover;
    gui_color_t background_pressed;
    gui_color_t background_disabled;

    gui_color_t border;

    uint32_t border_thickness;
    uint32_t corner_radius;

    uint32_t horizontal_padding;

    bool hovered;
    bool pressed;

    gui_button_click_fn on_click;
    void *click_context;
};


void gui_button_initialize(
    gui_button_t *button,
    gui_rect_t bounds,
    const char *text);

void gui_button_set_icon(
    gui_button_t *button,
    const gui_image_t *icon);

void gui_button_set_click_handler(
    gui_button_t *button,
    gui_button_click_fn handler,
    void *context);

void gui_button_set_hovered(
    gui_button_t *button,
    bool hovered);

void gui_button_set_pressed(
    gui_button_t *button,
    bool pressed);

void gui_button_click(
    gui_button_t *button);

gui_widget_t *gui_button_widget(
    gui_button_t *button);


#endif