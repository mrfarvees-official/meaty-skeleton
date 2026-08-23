#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/framebuffer.h>
#include <kernel/multiboot.h>
#include <kernel/paging.h>

typedef struct framebuffer_state
{
    bool available;

    volatile uint8_t *pixels;

    uintptr_t physical_address;
    uintptr_t physical_page_base;
    uintptr_t physical_offset;

    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;

    uint8_t red_position;
    uint8_t green_position;
    uint8_t blue_position;

    size_t mapped_pages;
    size_t mapped_capacity;

} framebuffer_state_t;

static framebuffer_state_t framebuffer_state;

/*
 * Small temporary 5x7 font.
 *
 * G1 uses this only for the framebuffer acceptance message.
 * FiraCode Nerd Font + anti-aliasing belongs to the next text-rendering
 * milestone, after framebuffer correctness is established.
 */

static const uint8_t glyph_M[7] =
    {
        0x11,
        0x1B,
        0x15,
        0x15,
        0x11,
        0x11,
        0x11};

static const uint8_t glyph_e[7] =
    {
        0x00,
        0x0E,
        0x11,
        0x1F,
        0x10,
        0x0F,
        0x00};

static const uint8_t glyph_a[7] =
    {
        0x00,
        0x0E,
        0x01,
        0x0F,
        0x11,
        0x0F,
        0x00};

static const uint8_t glyph_t[7] =
    {
        0x04,
        0x04,
        0x1F,
        0x04,
        0x04,
        0x05,
        0x02};

static const uint8_t glyph_y[7] =
    {
        0x00,
        0x11,
        0x11,
        0x0F,
        0x01,
        0x0E,
        0x00};

static const uint8_t glyph_g[7] =
    {
        0x00,
        0x0F,
        0x11,
        0x0F,
        0x01,
        0x0E,
        0x00};

static const uint8_t glyph_r[7] =
    {
        0x00,
        0x16,
        0x19,
        0x10,
        0x10,
        0x10,
        0x00};

static const uint8_t glyph_p[7] =
    {
        0x00,
        0x1E,
        0x11,
        0x1E,
        0x10,
        0x10,
        0x10};

static const uint8_t glyph_h[7] =
    {
        0x10,
        0x10,
        0x16,
        0x19,
        0x11,
        0x11,
        0x11};

static const uint8_t glyph_i[7] =
    {
        0x04,
        0x00,
        0x0C,
        0x04,
        0x04,
        0x04,
        0x0E};

static const uint8_t glyph_c[7] =
    {
        0x00,
        0x0F,
        0x10,
        0x10,
        0x10,
        0x0F,
        0x00};

static const uint8_t glyph_s[7] =
    {
        0x00,
        0x0F,
        0x10,
        0x0E,
        0x01,
        0x1E,
        0x00};

static const uint8_t glyph_O[7] =
    {
        0x0E,
        0x11,
        0x11,
        0x11,
        0x11,
        0x11,
        0x0E};

static const uint8_t glyph_K[7] =
    {
        0x11,
        0x12,
        0x14,
        0x18,
        0x14,
        0x12,
        0x11};

static const uint8_t *font_glyph(char character)
{
    switch (character)
    {
    case 'M':
        return glyph_M;

    case 'e':
        return glyph_e;

    case 'a':
        return glyph_a;

    case 't':
        return glyph_t;

    case 'y':
        return glyph_y;

    case 'g':
        return glyph_g;

    case 'r':
        return glyph_r;

    case 'p':
        return glyph_p;

    case 'h':
        return glyph_h;

    case 'i':
        return glyph_i;

    case 'c':
        return glyph_c;

    case 's':
        return glyph_s;

    case 'O':
        return glyph_O;

    case 'K':
        return glyph_K;

    case ' ':
    default:
        return NULL;
    }
}

static uint32_t framebuffer_pack_rgb(
    uint32_t rgb)
{
    uint32_t red =
        (rgb >> 16) & 0xFFu;

    uint32_t green =
        (rgb >> 8) & 0xFFu;

    uint32_t blue =
        rgb & 0xFFu;

    return (red << framebuffer_state.red_position) |
           (green << framebuffer_state.green_position) |
           (blue << framebuffer_state.blue_position);
}

static void framebuffer_unmap_pages(
    size_t page_count)
{
    for (size_t index = 0;
         index < page_count;
         ++index)
    {
        uintptr_t virtual_address =
            FRAMEBUFFER_VIRTUAL_BASE +
            index * PAGE_SIZE;

        paging_unmap_page(
            virtual_address,
            false);
    }
}

bool framebuffer_read_multiboot(
    uint32_t multiboot_info_address,
    framebuffer_boot_info_t *info)
{
    if (multiboot_info_address == 0 ||
        info == NULL)
    {
        return false;
    }

    const struct multiboot_info *mbi =
        (const struct multiboot_info *)(uintptr_t)
            multiboot_info_address;

    if ((mbi->flags &
         MULTIBOOT_INFO_FRAMEBUFFER) == 0)
    {
        return false;
    }

    memset(
        info,
        0,
        sizeof(*info));

    info->physical_address =
        mbi->framebuffer_addr;

    info->pitch =
        mbi->framebuffer_pitch;

    info->width =
        mbi->framebuffer_width;

    info->height =
        mbi->framebuffer_height;

    info->bpp =
        mbi->framebuffer_bpp;

    info->type =
        mbi->framebuffer_type;

    /*
     * G1 only interprets direct RGB information.
     */
    if (info->type !=
        MULTIBOOT_FRAMEBUFFER_TYPE_RGB)
    {
        return true;
    }

    const uint8_t *color =
        mbi->framebuffer_color_data;

    /*
     * First try the Multiboot specification layout:
     *
     *   color[0] R position
     *   color[1] R size
     *   color[2] G position
     *   color[3] G size
     *   color[4] B position
     *   color[5] B size
     */
    uint8_t red_position =
        color[0];

    uint8_t red_size =
        color[1];

    uint8_t green_position =
        color[2];

    uint8_t green_size =
        color[3];

    uint8_t blue_position =
        color[4];

    uint8_t blue_size =
        color[5];

    /*
     * A direct-RGB channel cannot have a zero-sized mask.
     *
     * Current GRUB/QEMU has been observed returning:
     *
     *   00 00 10 08 08 08 00 08
     *
     * That represents two alignment bytes followed by:
     *
     *   R position = 16
     *   R size     = 8
     *   G position = 8
     *   G size     = 8
     *   B position = 0
     *   B size     = 8
     *
     * If the specification-layout interpretation is clearly invalid,
     * retry at color[2].
     */
    if (red_size == 0u)
    {
        red_position =
            color[2];

        red_size =
            color[3];

        green_position =
            color[4];

        green_size =
            color[5];

        blue_position =
            color[6];

        blue_size =
            color[7];
    }

    info->red_position =
        red_position;

    info->red_mask_size =
        red_size;

    info->green_position =
        green_position;

    info->green_mask_size =
        green_size;

    info->blue_position =
        blue_position;

    info->blue_mask_size =
        blue_size;

    return true;
}

bool framebuffer_initialize(
    const framebuffer_boot_info_t *info)
{
    memset(
        &framebuffer_state,
        0,
        sizeof(framebuffer_state));

    if (info == NULL)
        return false;

    if (info->type !=
        MULTIBOOT_FRAMEBUFFER_TYPE_RGB)
    {
        return false;
    }

    if (info->bpp != 32u)
        return false;

    if (info->width == 0u ||
        info->height == 0u ||
        info->pitch == 0u)
    {
        return false;
    }

    if (info->red_mask_size != 8u ||
        info->green_mask_size != 8u ||
        info->blue_mask_size != 8u)
    {
        return false;
    }

    if (info->red_position > 24u ||
        info->green_position > 24u ||
        info->blue_position > 24u)
    {
        return false;
    }

    if (info->physical_address >
        0xFFFFFFFFULL)
    {
        return false;
    }

    uint64_t minimum_pitch =
        (uint64_t)info->width * 4u;

    if ((uint64_t)info->pitch <
        minimum_pitch)
    {
        return false;
    }

    uint64_t framebuffer_bytes =
        (uint64_t)info->pitch *
        (uint64_t)info->height;

    if (framebuffer_bytes == 0u ||
        framebuffer_bytes >
            0xFFFFFFFFULL)
    {
        return false;
    }

    uintptr_t physical_address =
        (uintptr_t)
            info->physical_address;

    uintptr_t physical_page_base =
        physical_address &
        ~(uintptr_t)(PAGE_SIZE - 1u);

    uintptr_t physical_offset =
        physical_address -
        physical_page_base;

    framebuffer_state.physical_address =
        physical_address;

    framebuffer_state.physical_page_base =
        physical_page_base;

    framebuffer_state.physical_offset =
        physical_offset;

    framebuffer_state.pitch =
        info->pitch;

    framebuffer_state.width =
        info->width;

    framebuffer_state.height =
        info->height;

    framebuffer_state.bpp =
        info->bpp;

    framebuffer_state.red_position =
        info->red_position;

    framebuffer_state.green_position =
        info->green_position;

    framebuffer_state.blue_position =
        info->blue_position;

    /*
     * Start by mapping the boot mode.
     *
     * display_initialize() will expand this to the complete VGA VRAM
     * size before runtime mode changes are permitted.
     */
    size_t initial_capacity =
        (size_t)framebuffer_bytes;

    if (!framebuffer_map_capacity(
            initial_capacity))
    {
        memset(
            &framebuffer_state,
            0,
            sizeof(framebuffer_state));

        return false;
    }

    framebuffer_state.available =
        true;

    return true;
}

bool framebuffer_is_available(void)
{
    return framebuffer_state.available;
}

uint32_t framebuffer_get_width(void)
{
    return framebuffer_state.width;
}

uint32_t framebuffer_get_height(void)
{
    return framebuffer_state.height;
}

uint32_t framebuffer_get_pitch(void)
{
    return framebuffer_state.pitch;
}

void framebuffer_put_pixel(
    uint32_t x,
    uint32_t y,
    uint32_t rgb)
{
    if (!framebuffer_state.available)
        return;

    if (x >= framebuffer_state.width ||
        y >= framebuffer_state.height)
    {
        return;
    }

    uintptr_t pixel_offset =
        (uintptr_t)y *
            framebuffer_state.pitch +
        (uintptr_t)x * 4u;

    volatile uint32_t *pixel =
        (volatile uint32_t *)(framebuffer_state.pixels +
                              pixel_offset);

    *pixel =
        framebuffer_pack_rgb(rgb);
}

void framebuffer_clear(
    uint32_t rgb)
{
    if (!framebuffer_state.available)
        return;

    uint32_t packed =
        framebuffer_pack_rgb(rgb);

    for (uint32_t y = 0;
         y < framebuffer_state.height;
         ++y)
    {
        volatile uint32_t *row =
            (volatile uint32_t *)(framebuffer_state.pixels +
                                  (uintptr_t)y *
                                      framebuffer_state.pitch);

        for (uint32_t x = 0;
             x < framebuffer_state.width;
             ++x)
        {
            row[x] = packed;
        }
    }
}

void framebuffer_fill_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t rgb)
{
    if (!framebuffer_state.available)
        return;

    if (width == 0 ||
        height == 0)
    {
        return;
    }

    if (x >= framebuffer_state.width ||
        y >= framebuffer_state.height)
    {
        return;
    }

    uint64_t end_x =
        (uint64_t)x +
        width;

    uint64_t end_y =
        (uint64_t)y +
        height;

    if (end_x >
        framebuffer_state.width)
    {
        end_x =
            framebuffer_state.width;
    }

    if (end_y >
        framebuffer_state.height)
    {
        end_y =
            framebuffer_state.height;
    }

    uint32_t packed =
        framebuffer_pack_rgb(rgb);

    for (uint32_t current_y = y;
         current_y < (uint32_t)end_y;
         ++current_y)
    {
        volatile uint32_t *row =
            (volatile uint32_t *)(framebuffer_state.pixels +
                                  (uintptr_t)current_y *
                                      framebuffer_state.pitch);

        for (uint32_t current_x = x;
             current_x < (uint32_t)end_x;
             ++current_x)
        {
            row[current_x] =
                packed;
        }
    }
}

static void framebuffer_draw_character(
    uint32_t x,
    uint32_t y,
    char character,
    uint32_t rgb,
    uint32_t scale)
{
    if (scale == 0)
        return;

    const uint8_t *glyph =
        font_glyph(character);

    if (glyph == NULL)
        return;

    for (uint32_t row = 0;
         row < 7u;
         ++row)
    {
        uint8_t bits =
            glyph[row];

        for (uint32_t column = 0;
             column < 5u;
             ++column)
        {
            uint8_t mask =
                (uint8_t)(1u << (4u - column));

            if ((bits & mask) == 0)
                continue;

            framebuffer_fill_rect(
                x + column * scale,
                y + row * scale,
                scale,
                scale,
                rgb);
        }
    }
}

void framebuffer_draw_string(
    uint32_t x,
    uint32_t y,
    const char *text,
    uint32_t rgb,
    uint32_t scale)
{
    if (!framebuffer_state.available ||
        text == NULL ||
        scale == 0)
    {
        return;
    }

    uint32_t cursor_x =
        x;

    while (*text != '\0')
    {
        framebuffer_draw_character(
            cursor_x,
            y,
            *text,
            rgb,
            scale);

        cursor_x +=
            6u * scale;

        ++text;
    }
}

void framebuffer_draw_boot_test(void)
{
    if (!framebuffer_state.available)
        return;

    /*
     * Dark background.
     */
    framebuffer_clear(
        0x101820u);

    uint32_t bar_height =
        framebuffer_state.height /
        12u;

    if (bar_height < 24u)
        bar_height = 24u;

    uint32_t third =
        framebuffer_state.width /
        3u;

    /*
     * Simple RGB verification bars.
     */
    framebuffer_fill_rect(
        0,
        0,
        third,
        bar_height,
        0xE53935u);

    framebuffer_fill_rect(
        third,
        0,
        third,
        bar_height,
        0x43A047u);

    framebuffer_fill_rect(
        third * 2u,
        0,
        framebuffer_state.width -
            third * 2u,
        bar_height,
        0x1E88E5u);

    /*
     * Central test panel.
     */
    uint32_t panel_width =
        framebuffer_state.width *
        3u / 4u;

    uint32_t panel_height =
        framebuffer_state.height /
        4u;

    uint32_t panel_x =
        (framebuffer_state.width -
         panel_width) /
        2u;

    uint32_t panel_y =
        (framebuffer_state.height -
         panel_height) /
        2u;

    framebuffer_fill_rect(
        panel_x,
        panel_y,
        panel_width,
        panel_height,
        0x1B2638u);

    /*
     * Accent line.
     */
    framebuffer_fill_rect(
        panel_x,
        panel_y,
        panel_width,
        4u,
        0x58A6FFu);

    /*
     * "Meaty graphics OK" contains 17 characters.
     */
    uint32_t scale =
        framebuffer_state.width >=
                1024u
            ? 3u
            : 2u;

    uint32_t text_width =
        17u * 6u * scale;

    uint32_t text_x =
        framebuffer_state.width >
                text_width
            ? (framebuffer_state.width -
               text_width) /
                  2u
            : panel_x + 8u;

    uint32_t text_y =
        panel_y +
        panel_height / 2u -
        (7u * scale) / 2u;

    framebuffer_draw_string(
        text_x,
        text_y,
        "Meaty graphics OK",
        0xF0F6FCu,
        scale);
}

bool framebuffer_early_test(
    const framebuffer_boot_info_t *info)
{
    if (info == NULL)
        return false;

    if (info->type !=
        MULTIBOOT_FRAMEBUFFER_TYPE_RGB)
    {
        return false;
    }

    if (info->bpp != 32u)
        return false;

    if (info->width == 0u ||
        info->height == 0u ||
        info->pitch == 0u)
    {
        return false;
    }

    if (info->red_mask_size != 8u ||
        info->green_mask_size != 8u ||
        info->blue_mask_size != 8u)
    {
        return false;
    }

    if (info->red_position > 24u ||
        info->green_position > 24u ||
        info->blue_position > 24u)
    {
        return false;
    }

    uint32_t red_mask =
        0xFFu << info->red_position;

    uint32_t green_mask =
        0xFFu << info->green_position;

    uint32_t blue_mask =
        0xFFu << info->blue_position;

    if ((red_mask & green_mask) != 0u ||
        (red_mask & blue_mask) != 0u ||
        (green_mask & blue_mask) != 0u)
    {
        return false;
    }

    if (info->physical_address >
        0xFFFFFFFFULL)
    {
        return false;
    }

    uint64_t minimum_pitch =
        (uint64_t)info->width *
        4u;

    if ((uint64_t)info->pitch <
        minimum_pitch)
    {
        return false;
    }

    volatile uint8_t *base =
        (volatile uint8_t *)(uintptr_t)
            info->physical_address;

    /*
     * Bright cyan/blue test strip.
     *
     * RGB source:
     *
     * R = 0x20
     * G = 0x80
     * B = 0xE0
     *
     * Pack it according to the channel positions GRUB supplied.
     */
    uint32_t color =
        (0x20u << info->red_position) |

        (0x80u << info->green_position) |

        (0xE0u << info->blue_position);

    uint32_t rows =
        info->height < 80u
            ? info->height
            : 80u;

    for (uint32_t y = 0;
         y < rows;
         ++y)
    {
        volatile uint32_t *row =
            (volatile uint32_t *)(base +
                                  (uintptr_t)y *
                                      info->pitch);

        for (uint32_t x = 0;
             x < info->width;
             ++x)
        {
            row[x] =
                color;
        }
    }

    return true;
}

bool framebuffer_map_capacity(
    size_t capacity_bytes)
{
    if (capacity_bytes == 0u)
        return false;

    uintptr_t physical_offset =
        framebuffer_state.physical_offset;

    uint64_t required_bytes =
        (uint64_t)physical_offset +
        (uint64_t)capacity_bytes;

    if (required_bytes >
        (uint64_t)(
            FRAMEBUFFER_VIRTUAL_LIMIT -
            FRAMEBUFFER_VIRTUAL_BASE))
    {
        return false;
    }

    uint64_t rounded =
        required_bytes +
        (PAGE_SIZE - 1u);

    if (rounded <
        required_bytes)
    {
        return false;
    }

    rounded &=
        ~(uint64_t)(PAGE_SIZE - 1u);

    size_t required_pages =
        (size_t)(
            rounded /
            PAGE_SIZE);

    /*
     * Existing pages remain mapped.
     *
     * Only extend the mapping.
     */
    for (size_t index =
             framebuffer_state.mapped_pages;
         index < required_pages;
         ++index)
    {
        uintptr_t virtual_address =
            FRAMEBUFFER_VIRTUAL_BASE +
            index * PAGE_SIZE;

        uintptr_t physical_address =
            framebuffer_state
                .physical_page_base +
            index * PAGE_SIZE;

        if (paging_is_mapped(
                virtual_address))
        {
            return false;
        }

        if (!paging_map_page(
                virtual_address,
                physical_address,
                PAGE_WRITABLE |
                PAGE_CACHE_DISABLE))
        {
            return false;
        }

        framebuffer_state.mapped_pages =
            index + 1u;
    }

    framebuffer_state.pixels =
        (volatile uint8_t *)
        (uintptr_t)(
            FRAMEBUFFER_VIRTUAL_BASE +
            physical_offset);

    framebuffer_state.mapped_capacity =
        capacity_bytes;

    return true;
}

bool framebuffer_set_geometry(
    uint32_t width,
    uint32_t height,
    uint32_t pitch,
    uint32_t bpp)
{
    if (!framebuffer_state.available)
        return false;

    if (bpp != 32u)
        return false;

    if (width == 0u ||
        height == 0u ||
        pitch == 0u)
    {
        return false;
    }

    uint64_t minimum_pitch =
        (uint64_t)width * 4u;

    if ((uint64_t)pitch <
        minimum_pitch)
    {
        return false;
    }

    uint64_t required_bytes =
        (uint64_t)pitch *
        (uint64_t)height;

    if (required_bytes >
        framebuffer_state
            .mapped_capacity)
    {
        return false;
    }

    framebuffer_state.width =
        width;

    framebuffer_state.height =
        height;

    framebuffer_state.pitch =
        pitch;

    framebuffer_state.bpp =
        bpp;

    return true;
}

uint32_t framebuffer_get_bpp(void)
{
    return framebuffer_state.bpp;
}

uintptr_t framebuffer_get_physical_address(void)
{
    return framebuffer_state.physical_address;
}

size_t framebuffer_get_mapped_capacity(void)
{
    return framebuffer_state.mapped_capacity;
}

