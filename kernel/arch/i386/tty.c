#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/framebuffer.h>
#include <kernel/tty.h>

#include "vga.h"
#include "io.h"


/*
 * ------------------------------------------------------------
 * VGA backend
 * ------------------------------------------------------------
 */

#define VGA_CRTC_COMMAND 0x3D4u
#define VGA_CRTC_DATA    0x3D5u

#define VGA_CURSOR_START 0x0Au
#define VGA_CURSOR_END   0x0Bu
#define VGA_CURSOR_HIGH  0x0Eu
#define VGA_CURSOR_LOW   0x0Fu

#define VGA_WIDTH  80u
#define VGA_HEIGHT 25u

static uint16_t *const VGA_MEMORY =
    (uint16_t *)0xB8000;


/*
 * ------------------------------------------------------------
 * Framebuffer terminal geometry
 * ------------------------------------------------------------
 *
 * Temporary G2 font:
 *
 * glyph bitmap: 5 x 7
 * rendered cell: 8 x 16
 *
 * Each source font row is doubled vertically.
 *
 * At 1024x768 this produces:
 *
 *     128 columns
 *      48 rows
 */

#define FB_CELL_WIDTH  8u
#define FB_CELL_HEIGHT 16u

#define FB_TERMINAL_MAX_COLUMNS 256u
#define FB_TERMINAL_MAX_ROWS     80u

#define TAB_WIDTH 4u


typedef enum terminal_backend
{
    TERMINAL_BACKEND_VGA = 0,
    TERMINAL_BACKEND_FRAMEBUFFER

} terminal_backend_t;


typedef struct terminal_cell
{
    uint8_t character;
    uint8_t color;

} terminal_cell_t;


static terminal_backend_t terminal_backend =
    TERMINAL_BACKEND_VGA;

static size_t terminal_row;
static size_t terminal_column;

static uint8_t terminal_color;

static uint16_t *terminal_vga_buffer =
    VGA_MEMORY;


static terminal_cell_t
    framebuffer_cells[
        FB_TERMINAL_MAX_ROWS]
                     [
        FB_TERMINAL_MAX_COLUMNS];

static size_t framebuffer_columns;
static size_t framebuffer_rows;

static bool framebuffer_cursor_visible;


/*
 * ------------------------------------------------------------
 * Temporary ASCII 5x7 font
 * ------------------------------------------------------------
 *
 * ASCII 32..126.
 *
 * Each entry contains five vertical columns.
 * Bit 0 is the top pixel.
 */

static const uint8_t font5x7[95][5] =
{
    /* 32 space */
    {0x00,0x00,0x00,0x00,0x00},

    /* 33 ! */
    {0x00,0x00,0x5F,0x00,0x00},

    /* 34 " */
    {0x00,0x07,0x00,0x07,0x00},

    /* 35 # */
    {0x14,0x7F,0x14,0x7F,0x14},

    /* 36 $ */
    {0x24,0x2A,0x7F,0x2A,0x12},

    /* 37 % */
    {0x23,0x13,0x08,0x64,0x62},

    /* 38 & */
    {0x36,0x49,0x55,0x22,0x50},

    /* 39 ' */
    {0x00,0x05,0x03,0x00,0x00},

    /* 40 ( */
    {0x00,0x1C,0x22,0x41,0x00},

    /* 41 ) */
    {0x00,0x41,0x22,0x1C,0x00},

    /* 42 * */
    {0x14,0x08,0x3E,0x08,0x14},

    /* 43 + */
    {0x08,0x08,0x3E,0x08,0x08},

    /* 44 , */
    {0x00,0x50,0x30,0x00,0x00},

    /* 45 - */
    {0x08,0x08,0x08,0x08,0x08},

    /* 46 . */
    {0x00,0x60,0x60,0x00,0x00},

    /* 47 / */
    {0x20,0x10,0x08,0x04,0x02},

    /* 48 0 */
    {0x3E,0x51,0x49,0x45,0x3E},

    /* 49 1 */
    {0x00,0x42,0x7F,0x40,0x00},

    /* 50 2 */
    {0x42,0x61,0x51,0x49,0x46},

    /* 51 3 */
    {0x21,0x41,0x45,0x4B,0x31},

    /* 52 4 */
    {0x18,0x14,0x12,0x7F,0x10},

    /* 53 5 */
    {0x27,0x45,0x45,0x45,0x39},

    /* 54 6 */
    {0x3C,0x4A,0x49,0x49,0x30},

    /* 55 7 */
    {0x01,0x71,0x09,0x05,0x03},

    /* 56 8 */
    {0x36,0x49,0x49,0x49,0x36},

    /* 57 9 */
    {0x06,0x49,0x49,0x29,0x1E},

    /* 58 : */
    {0x00,0x36,0x36,0x00,0x00},

    /* 59 ; */
    {0x00,0x56,0x36,0x00,0x00},

    /* 60 < */
    {0x08,0x14,0x22,0x41,0x00},

    /* 61 = */
    {0x14,0x14,0x14,0x14,0x14},

    /* 62 > */
    {0x00,0x41,0x22,0x14,0x08},

    /* 63 ? */
    {0x02,0x01,0x51,0x09,0x06},

    /* 64 @ */
    {0x32,0x49,0x79,0x41,0x3E},

    /* 65 A */
    {0x7E,0x11,0x11,0x11,0x7E},

    /* 66 B */
    {0x7F,0x49,0x49,0x49,0x36},

    /* 67 C */
    {0x3E,0x41,0x41,0x41,0x22},

    /* 68 D */
    {0x7F,0x41,0x41,0x22,0x1C},

    /* 69 E */
    {0x7F,0x49,0x49,0x49,0x41},

    /* 70 F */
    {0x7F,0x09,0x09,0x09,0x01},

    /* 71 G */
    {0x3E,0x41,0x49,0x49,0x7A},

    /* 72 H */
    {0x7F,0x08,0x08,0x08,0x7F},

    /* 73 I */
    {0x00,0x41,0x7F,0x41,0x00},

    /* 74 J */
    {0x20,0x40,0x41,0x3F,0x01},

    /* 75 K */
    {0x7F,0x08,0x14,0x22,0x41},

    /* 76 L */
    {0x7F,0x40,0x40,0x40,0x40},

    /* 77 M */
    {0x7F,0x02,0x0C,0x02,0x7F},

    /* 78 N */
    {0x7F,0x04,0x08,0x10,0x7F},

    /* 79 O */
    {0x3E,0x41,0x41,0x41,0x3E},

    /* 80 P */
    {0x7F,0x09,0x09,0x09,0x06},

    /* 81 Q */
    {0x3E,0x41,0x51,0x21,0x5E},

    /* 82 R */
    {0x7F,0x09,0x19,0x29,0x46},

    /* 83 S */
    {0x46,0x49,0x49,0x49,0x31},

    /* 84 T */
    {0x01,0x01,0x7F,0x01,0x01},

    /* 85 U */
    {0x3F,0x40,0x40,0x40,0x3F},

    /* 86 V */
    {0x1F,0x20,0x40,0x20,0x1F},

    /* 87 W */
    {0x3F,0x40,0x38,0x40,0x3F},

    /* 88 X */
    {0x63,0x14,0x08,0x14,0x63},

    /* 89 Y */
    {0x07,0x08,0x70,0x08,0x07},

    /* 90 Z */
    {0x61,0x51,0x49,0x45,0x43},

    /* 91 [ */
    {0x00,0x7F,0x41,0x41,0x00},

    /* 92 \ */
    {0x02,0x04,0x08,0x10,0x20},

    /* 93 ] */
    {0x00,0x41,0x41,0x7F,0x00},

    /* 94 ^ */
    {0x04,0x02,0x01,0x02,0x04},

    /* 95 _ */
    {0x40,0x40,0x40,0x40,0x40},

    /* 96 ` */
    {0x00,0x01,0x02,0x04,0x00},

    /* 97 a */
    {0x20,0x54,0x54,0x54,0x78},

    /* 98 b */
    {0x7F,0x48,0x44,0x44,0x38},

    /* 99 c */
    {0x38,0x44,0x44,0x44,0x20},

    /* 100 d */
    {0x38,0x44,0x44,0x48,0x7F},

    /* 101 e */
    {0x38,0x54,0x54,0x54,0x18},

    /* 102 f */
    {0x08,0x7E,0x09,0x01,0x02},

    /* 103 g */
    {0x0C,0x52,0x52,0x52,0x3E},

    /* 104 h */
    {0x7F,0x08,0x04,0x04,0x78},

    /* 105 i */
    {0x00,0x44,0x7D,0x40,0x00},

    /* 106 j */
    {0x20,0x40,0x44,0x3D,0x00},

    /* 107 k */
    {0x7F,0x10,0x28,0x44,0x00},

    /* 108 l */
    {0x00,0x41,0x7F,0x40,0x00},

    /* 109 m */
    {0x7C,0x04,0x18,0x04,0x78},

    /* 110 n */
    {0x7C,0x08,0x04,0x04,0x78},

    /* 111 o */
    {0x38,0x44,0x44,0x44,0x38},

    /* 112 p */
    {0x7C,0x14,0x14,0x14,0x08},

    /* 113 q */
    {0x08,0x14,0x14,0x18,0x7C},

    /* 114 r */
    {0x7C,0x08,0x04,0x04,0x08},

    /* 115 s */
    {0x48,0x54,0x54,0x54,0x20},

    /* 116 t */
    {0x04,0x3F,0x44,0x40,0x20},

    /* 117 u */
    {0x3C,0x40,0x40,0x20,0x7C},

    /* 118 v */
    {0x1C,0x20,0x40,0x20,0x1C},

    /* 119 w */
    {0x3C,0x40,0x30,0x40,0x3C},

    /* 120 x */
    {0x44,0x28,0x10,0x28,0x44},

    /* 121 y */
    {0x0C,0x50,0x50,0x50,0x3C},

    /* 122 z */
    {0x44,0x64,0x54,0x4C,0x44},

    /* 123 { */
    {0x00,0x08,0x36,0x41,0x00},

    /* 124 | */
    {0x00,0x00,0x7F,0x00,0x00},

    /* 125 } */
    {0x00,0x41,0x36,0x08,0x00},

    /* 126 ~ */
    {0x08,0x04,0x08,0x10,0x08}
};


/*
 * ------------------------------------------------------------
 * VGA palette -> RGB
 * ------------------------------------------------------------
 *
 * terminal_color remains compatible with the existing VGA color
 * encoding used by logger.c:
 *
 * lower nibble = foreground
 * upper nibble = background
 */

static const uint32_t terminal_rgb_palette[16] =
{
    0x000000u,
    0x0000AAu,
    0x00AA00u,
    0x00AAAAu,
    0xAA0000u,
    0xAA00AAu,
    0xAA5500u,
    0xAAAAAAu,

    0x555555u,
    0x5555FFu,
    0x55FF55u,
    0x55FFFFu,
    0xFF5555u,
    0xFF55FFu,
    0xFFFF55u,
    0xFFFFFFu
};


static uint32_t terminal_foreground_rgb(
    uint8_t color)
{
    return terminal_rgb_palette[
        color & 0x0Fu];
}


static uint32_t terminal_background_rgb(
    uint8_t color)
{
    return terminal_rgb_palette[
        (color >> 4) &
        0x0Fu];
}


/*
 * ------------------------------------------------------------
 * VGA cursor
 * ------------------------------------------------------------
 */

static void terminal_vga_cursor_enable(void)
{
    outb(
        VGA_CRTC_COMMAND,
        VGA_CURSOR_START);

    uint8_t start =
        inb(
            VGA_CRTC_DATA);

    outb(
        VGA_CRTC_DATA,
        (uint8_t)(
            (start & 0xC0u) |
            13u));

    outb(
        VGA_CRTC_COMMAND,
        VGA_CURSOR_END);

    uint8_t end =
        inb(
            VGA_CRTC_DATA);

    outb(
        VGA_CRTC_DATA,
        (uint8_t)(
            (end & 0xE0u) |
            15u));
}


static void terminal_vga_cursor_update(void)
{
    size_t position =
        terminal_row *
            VGA_WIDTH +
        terminal_column;

    outb(
        VGA_CRTC_COMMAND,
        VGA_CURSOR_LOW);

    outb(
        VGA_CRTC_DATA,
        (uint8_t)(
            position &
            0xFFu));

    outb(
        VGA_CRTC_COMMAND,
        VGA_CURSOR_HIGH);

    outb(
        VGA_CRTC_DATA,
        (uint8_t)(
            (position >> 8) &
            0xFFu));
}


/*
 * ------------------------------------------------------------
 * Framebuffer rendering
 * ------------------------------------------------------------
 */

static void terminal_framebuffer_render_cell(
    size_t column,
    size_t row)
{
    if (column >=
            framebuffer_columns ||
        row >=
            framebuffer_rows)
    {
        return;
    }

    terminal_cell_t cell =
        framebuffer_cells[row][column];

    uint32_t foreground =
        terminal_foreground_rgb(
            cell.color);

    uint32_t background =
        terminal_background_rgb(
            cell.color);

    uint32_t pixel_x =
        (uint32_t)(
            column *
            FB_CELL_WIDTH);

    uint32_t pixel_y =
        (uint32_t)(
            row *
            FB_CELL_HEIGHT);

    /*
     * Always repaint the entire cell first.
     *
     * This makes erase, backspace and cursor removal deterministic.
     */
    framebuffer_fill_rect(
        pixel_x,
        pixel_y,
        FB_CELL_WIDTH,
        FB_CELL_HEIGHT,
        background);

    unsigned char character =
        cell.character;

    if (character < 32u ||
        character > 126u)
    {
        character = '?';
    }

    const uint8_t *glyph =
        font5x7[
            character - 32u];

    /*
     * 5x7 -> 5x14.
     *
     * Horizontal source pixels remain 1 framebuffer pixel wide.
     * Vertical pixels are doubled.
     *
     * This intentionally stays simple and crisp for G2.
     */
    for (uint32_t glyph_x = 0;
         glyph_x < 5u;
         ++glyph_x)
    {
        uint8_t bits =
            glyph[glyph_x];

        for (uint32_t glyph_y = 0;
             glyph_y < 7u;
             ++glyph_y)
        {
            if ((bits &
                 (1u << glyph_y)) == 0)
            {
                continue;
            }

            framebuffer_fill_rect(
                pixel_x +
                    1u +
                    glyph_x,
                pixel_y +
                    1u +
                    glyph_y * 2u,
                1u,
                2u,
                foreground);
        }
    }
}


static void terminal_framebuffer_cursor_hide(void)
{
    if (!framebuffer_cursor_visible)
        return;

    if (terminal_column <
            framebuffer_columns &&
        terminal_row <
            framebuffer_rows)
    {
        terminal_framebuffer_render_cell(
            terminal_column,
            terminal_row);
    }

    framebuffer_cursor_visible =
        false;
}


static void terminal_framebuffer_cursor_show(void)
{
    if (terminal_backend !=
        TERMINAL_BACKEND_FRAMEBUFFER)
    {
        return;
    }

    if (terminal_column >=
            framebuffer_columns ||
        terminal_row >=
            framebuffer_rows)
    {
        return;
    }

    terminal_cell_t cell =
        framebuffer_cells[
            terminal_row]
                         [
            terminal_column];

    uint32_t foreground =
        terminal_foreground_rgb(
            cell.color);

    uint32_t pixel_x =
        (uint32_t)(
            terminal_column *
            FB_CELL_WIDTH);

    uint32_t pixel_y =
        (uint32_t)(
            terminal_row *
            FB_CELL_HEIGHT);

    /*
     * One-pixel underline cursor.
     */
    framebuffer_fill_rect(
        pixel_x + 1u,
        pixel_y +
            FB_CELL_HEIGHT -
            1u,
        FB_CELL_WIDTH - 2u,
        1u,
        foreground);

    framebuffer_cursor_visible =
        true;
}


static void terminal_framebuffer_render_all(void)
{
    for (size_t row = 0;
         row < framebuffer_rows;
         ++row)
    {
        for (size_t column = 0;
             column < framebuffer_columns;
             ++column)
        {
            terminal_framebuffer_render_cell(
                column,
                row);
        }
    }
}


static void terminal_framebuffer_clear(void)
{
    terminal_color =
        vga_entry_color(
            VGA_COLOR_LIGHT_GREY,
            VGA_COLOR_BLACK);

    terminal_row = 0u;
    terminal_column = 0u;

    framebuffer_cursor_visible =
        false;

    for (size_t row = 0;
         row < framebuffer_rows;
         ++row)
    {
        for (size_t column = 0;
             column < framebuffer_columns;
             ++column)
        {
            framebuffer_cells[row][column]
                .character = ' ';

            framebuffer_cells[row][column]
                .color =
                    terminal_color;
        }
    }

    framebuffer_clear(
        terminal_background_rgb(
            terminal_color));

    terminal_framebuffer_cursor_show();
}


static void terminal_framebuffer_scroll(void)
{
    terminal_framebuffer_cursor_hide();

    for (size_t row = 1u;
         row < framebuffer_rows;
         ++row)
    {
        for (size_t column = 0u;
             column < framebuffer_columns;
             ++column)
        {
            framebuffer_cells[
                row - 1u]
                             [
                column] =
                    framebuffer_cells[
                        row]
                                     [
                        column];
        }
    }

    size_t final_row =
        framebuffer_rows - 1u;

    for (size_t column = 0u;
         column < framebuffer_columns;
         ++column)
    {
        framebuffer_cells[
            final_row]
                         [
            column]
            .character = ' ';

        framebuffer_cells[
            final_row]
                         [
            column]
            .color =
                terminal_color;
    }

    terminal_row =
        final_row;

    /*
     * Scrolling is currently implemented as a complete terminal redraw.
     *
     * This is intentionally correct-first.
     *
     * Dirty regions / framebuffer block copies come later.
     */
    terminal_framebuffer_render_all();
}


/*
 * ------------------------------------------------------------
 * VGA rendering
 * ------------------------------------------------------------
 */

static void terminal_vga_clear_row(
    size_t row)
{
    for (size_t column = 0;
         column < VGA_WIDTH;
         ++column)
    {
        size_t index =
            row *
                VGA_WIDTH +
            column;

        terminal_vga_buffer[index] =
            vga_entry(
                ' ',
                terminal_color);
    }
}


static void terminal_vga_clear(void)
{
    terminal_color =
        vga_entry_color(
            VGA_COLOR_LIGHT_GREY,
            VGA_COLOR_BLACK);

    terminal_row = 0u;
    terminal_column = 0u;

    for (size_t row = 0;
         row < VGA_HEIGHT;
         ++row)
    {
        terminal_vga_clear_row(
            row);
    }

    terminal_vga_cursor_update();
}


static void terminal_vga_scroll(void)
{
    for (size_t row = 1;
         row < VGA_HEIGHT;
         ++row)
    {
        for (size_t column = 0;
             column < VGA_WIDTH;
             ++column)
        {
            size_t source =
                row *
                    VGA_WIDTH +
                column;

            size_t destination =
                (row - 1u) *
                    VGA_WIDTH +
                column;

            terminal_vga_buffer[
                destination] =
                    terminal_vga_buffer[
                        source];
        }
    }

    terminal_vga_clear_row(
        VGA_HEIGHT - 1u);

    terminal_row =
        VGA_HEIGHT - 1u;
}


/*
 * ------------------------------------------------------------
 * Common terminal helpers
 * ------------------------------------------------------------
 */

static size_t terminal_width(void)
{
    if (terminal_backend ==
        TERMINAL_BACKEND_FRAMEBUFFER)
    {
        return framebuffer_columns;
    }

    return VGA_WIDTH;
}


static size_t terminal_height(void)
{
    if (terminal_backend ==
        TERMINAL_BACKEND_FRAMEBUFFER)
    {
        return framebuffer_rows;
    }

    return VGA_HEIGHT;
}


static void terminal_scroll(void)
{
    if (terminal_backend ==
        TERMINAL_BACKEND_FRAMEBUFFER)
    {
        terminal_framebuffer_scroll();
        return;
    }

    terminal_vga_scroll();
}


static void terminal_advance_row(void)
{
    terminal_column = 0u;
    ++terminal_row;

    if (terminal_row >=
        terminal_height())
    {
        terminal_scroll();
    }
}


static void terminal_putentryat(
    unsigned char character,
    uint8_t color,
    size_t x,
    size_t y)
{
    if (x >= terminal_width() ||
        y >= terminal_height())
    {
        return;
    }

    if (terminal_backend ==
        TERMINAL_BACKEND_FRAMEBUFFER)
    {
        framebuffer_cells[y][x]
            .character =
                character;

        framebuffer_cells[y][x]
            .color =
                color;

        terminal_framebuffer_render_cell(
            x,
            y);

        return;
    }

    size_t index =
        y *
            VGA_WIDTH +
        x;

    terminal_vga_buffer[index] =
        vga_entry(
            character,
            color);
}


/*
 * ------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------
 */

void terminal_initialize(void)
{
    terminal_backend =
        TERMINAL_BACKEND_VGA;

    terminal_vga_buffer =
        VGA_MEMORY;

    terminal_color =
        vga_entry_color(
            VGA_COLOR_LIGHT_GREY,
            VGA_COLOR_BLACK);

    terminal_row = 0u;
    terminal_column = 0u;

    for (size_t row = 0;
         row < VGA_HEIGHT;
         ++row)
    {
        terminal_vga_clear_row(
            row);
    }

    terminal_vga_cursor_enable();
    terminal_vga_cursor_update();
}


bool terminal_enable_framebuffer(void)
{
    if (!framebuffer_is_available())
        return false;

    uint32_t width =
        framebuffer_get_width();

    uint32_t height =
        framebuffer_get_height();

    if (width <
            FB_CELL_WIDTH ||
        height <
            FB_CELL_HEIGHT)
    {
        return false;
    }

    framebuffer_columns =
        width /
        FB_CELL_WIDTH;

    framebuffer_rows =
        height /
        FB_CELL_HEIGHT;

    if (framebuffer_columns >
        FB_TERMINAL_MAX_COLUMNS)
    {
        framebuffer_columns =
            FB_TERMINAL_MAX_COLUMNS;
    }

    if (framebuffer_rows >
        FB_TERMINAL_MAX_ROWS)
    {
        framebuffer_rows =
            FB_TERMINAL_MAX_ROWS;
    }

    if (framebuffer_columns == 0u ||
        framebuffer_rows == 0u)
    {
        return false;
    }

    terminal_backend =
        TERMINAL_BACKEND_FRAMEBUFFER;

    terminal_framebuffer_clear();

    return true;
}


void terminal_setcolor(
    uint8_t color)
{
    terminal_color =
        color;
}


uint8_t terminal_getcolor(void)
{
    return terminal_color;
}


void terminal_putchar(
    char character)
{
    if (terminal_backend ==
        TERMINAL_BACKEND_FRAMEBUFFER)
    {
        terminal_framebuffer_cursor_hide();
    }

    switch (character)
    {
        case '\a':
        {
            /*
             * Bell intentionally remains silent.
             */
            break;
        }

        case '\b':
        {
            if (terminal_column > 0u)
            {
                --terminal_column;
            }
            else if (terminal_row > 0u)
            {
                --terminal_row;

                terminal_column =
                    terminal_width() -
                    1u;
            }
            else
            {
                break;
            }

            terminal_putentryat(
                ' ',
                terminal_color,
                terminal_column,
                terminal_row);

            break;
        }

        case '\t':
        {
            size_t next_tab_stop =
                (terminal_column +
                 TAB_WIDTH) &
                ~(TAB_WIDTH - 1u);

            if (next_tab_stop >
                terminal_width())
            {
                next_tab_stop =
                    terminal_width();
            }

            while (terminal_column <
                   next_tab_stop)
            {
                terminal_putentryat(
                    ' ',
                    terminal_color,
                    terminal_column,
                    terminal_row);

                ++terminal_column;

                if (terminal_column >=
                    terminal_width())
                {
                    terminal_advance_row();
                    break;
                }
            }

            break;
        }

        case '\n':
        {
            terminal_advance_row();
            break;
        }

        case '\v':
        {
            ++terminal_row;

            if (terminal_row >=
                terminal_height())
            {
                terminal_scroll();
            }

            break;
        }

        case '\f':
        {
            /*
             * Preserve the existing clear command semantics.
             */
            if (terminal_backend ==
                TERMINAL_BACKEND_FRAMEBUFFER)
            {
                terminal_framebuffer_clear();
            }
            else
            {
                terminal_vga_clear();
            }

            break;
        }

        case '\r':
        {
            terminal_column = 0u;
            break;
        }

        default:
        {
            terminal_putentryat(
                (unsigned char)
                    character,
                terminal_color,
                terminal_column,
                terminal_row);

            ++terminal_column;

            if (terminal_column >=
                terminal_width())
            {
                terminal_advance_row();
            }

            break;
        }
    }

    if (terminal_backend ==
        TERMINAL_BACKEND_FRAMEBUFFER)
    {
        terminal_framebuffer_cursor_show();
    }
    else
    {
        terminal_vga_cursor_update();
    }
}


void terminal_write(
    const char *data,
    size_t size)
{
    if (data == NULL)
        return;

    for (size_t index = 0;
         index < size;
         ++index)
    {
        terminal_putchar(
            data[index]);
    }
}


void terminal_writestring(
    const char *data)
{
    if (data == NULL)
        return;

    terminal_write(
        data,
        strlen(data));
}