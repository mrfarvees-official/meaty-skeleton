#ifndef KERNEL_GUI_FONT_H
#define KERNEL_GUI_FONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/gui/surface.h>

typedef struct gui_font gui_font_t;

typedef struct gui_font_glyph_metrics
{
    uint32_t width;
    uint32_t height;

    /**
     * Horizontal offset from the current pen position to the
     * left side of the rasterized glyph.
     */
    int32_t bearing_x;

    /**
     * Distance from the baseline upward to the top of the
     * rasterized glyph.
     */
    int32_t bearing_y;

    /**
     * Horizontal pen movement after drawing the glyph.
     */
    int32_t advance;
} gui_font_glyph_metrics_t;

typedef struct gui_font_line_metrics
{
    int32_t ascent;
    int32_t descent;
    int32_t line_gap;
    int32_t line_height;
} gui_font_line_metrics_t;

/*
 * Load and parse one TrueType font from the VFS.
 *
 * The complete font file remains resident in memory for the
 * lifetime of the gui_font_t because the TrueType parser keeps
 * references into that memory.
 */
bool gui_font_load_ttf(
    const char *path,
    gui_font_t **result);

void gui_font_destory(
    gui_font_t *font);

/*
 * Retrieve metrics for one Unicode codepoint at a particular
 * pixel height.
 *
 * The initial text drawing frontend only consumes ASCII, but
 * the cache and rasterizer are codepoint-based from the start.
 */
bool gui_font_get_glyph_metrics(
    gui_font_t *font,
    uint32_t codepoint,
    uint32_t pixel_height,
    gui_font_glyph_metrics_t *metrics);

/*
 * Retrieve scaled vertical metrics.
 */
bool gui_font_get_line_metrics(
    gui_font_t *font,
    uint32_t pixel_height,
    gui_font_line_metrics_t *metrics);

/*
 * Draw text into an off-screen GUI surface.
 *
 * x/y describe the top-left corner of the first line.
 *
 * Initial milestone:
 *     ASCII input
 *     '\n' supported
 *     '\t' treated as four spaces
 *     bytes >= 0x80 render as '?'
 *
 * The underlying font/cache API is codepoint based, so a UTF-8
 * decoder can be added later without replacing the subsystem.
 */
bool gui_font_draw_text(
    gui_surface_t *surface,
    gui_font_t *font,
    int32_t x,
    int32_t y,
    uint32_t pixel_height,
    const char *text,
    gui_color_t color);

/*
 * System font bootstrap.
 *
 * The first configured system font is:
 *
 *     /fonts/Consolas.ttf
 */
bool gui_font_system_initialize(void);

gui_font_t *gui_font_default(void);

#endif