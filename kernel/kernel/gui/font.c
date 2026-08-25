#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/font.h>
#include <kernel/gui/surface.h>
#include <kernel/heap.h>
#include <kernel/vfs.h>

/*
 * ------------------------------------------------------------
 * stb_truetype freestanding adaptation
 * ------------------------------------------------------------
 *
 * Keep every stb-specific detail inside this translation unit.
 */

static int gui_font_stb_floor(float value)
{
    int integer = (int)value;

    if ((float)integer > value)
        --integer;

    return integer;
}

static int gui_font_stb_ceil(float value)
{
    int integer = (int)value;

    if ((float)integer < value)
        ++integer;

    return integer;
}

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION

#define STBTT_ifloor(x) \
    gui_font_stb_floor((float)(x))

#define STBTT_iceil(x) \
    gui_font_stb_ceil((float)(x))

/*
 * Basic TrueType glyph rasterization does not require the SDF
 * API, but stb_truetype also compiles those private routines.
 *
 * GCC builtins avoid requiring Meaty libc to provide math.h.
 * Unused static stb routines are eliminated at -O2.
 */
#define STBTT_sqrt(x) \
    __builtin_sqrtf((float)(x))

#define STBTT_pow(x, y) \
    __builtin_powf((float)(x), (float)(y))

#define STBTT_fmod(x, y) \
    __builtin_fmodf((float)(x), (float)(y))

#define STBTT_cos(x) \
    __builtin_cosf((float)(x))

#define STBTT_acos(x) \
    __builtin_acosf((float)(x))

#define STBTT_fabs(x) \
    __builtin_fabsf((float)(x))

#define STBTT_malloc(size, userdata) \
    ((void)(userdata), kmalloc((size_t)(size)))

#define STBTT_free(pointer, userdata) \
    ((void)(userdata), kfree((pointer)))

#define STBTT_assert(expression) \
    ((expression) ? (void)0 : __builtin_trap())

#define STBTT_strlen(value) \
    strlen(value)

#define STBTT_memcpy \
    memcpy

#define STBTT_memset \
    memset

#include <stb_truetype.h>


/*
 * ------------------------------------------------------------
 * Font limits
 * ------------------------------------------------------------
 */

#define GUI_FONT_MAX_FILE_SIZE \
    (16u * 1024u * 1024u)

#define GUI_DEFAULT_FONT_PATH \
    "/fonts/Consolas.ttf"


/*
 * ------------------------------------------------------------
 * Glyph cache
 * ------------------------------------------------------------
 *
 * Cache ownership is per font.
 *
 * Therefore the complete cache key is effectively:
 *
 *     gui_font_t identity
 *     codepoint
 *     pixel_height
 *
 * Multiple fonts and multiple sizes require no redesign.
 */

typedef struct gui_cached_glyph
{
    uint32_t codepoint;
    uint32_t pixel_height;

    gui_font_glyph_metrics_t metrics;

    /*
     * 8-bit coverage bitmap.
     *
     * One byte per pixel:
     *
     *     0   = transparent
     *     255 = fully covered
     */
    uint8_t *bitmap;

    struct gui_cached_glyph *next;
} gui_cached_glyph_t;


/*
 * ------------------------------------------------------------
 * Font object
 * ------------------------------------------------------------
 */

struct gui_font
{
    uint8_t *file_data;
    size_t file_size;

    stbtt_fontinfo info;

    gui_cached_glyph_t *glyph_cache;
};


/*
 * ------------------------------------------------------------
 * Global system font state
 * ------------------------------------------------------------
 */

static gui_font_t *default_font;


/*
 * ------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------
 */

static int32_t gui_font_round_float(float value)
{
    if (value >= 0.0f)
        return (int32_t)(value + 0.5f);

    return (int32_t)(value - 0.5f);
}


static bool gui_font_read_complete_file(
    const char *path,
    uint8_t **data_result,
    size_t *size_result)
{
    if (path == NULL ||
        data_result == NULL ||
        size_result == NULL)
    {
        return false;
    }

    *data_result = NULL;
    *size_result = 0u;

    file_t *file = NULL;

    if (vfs_open(
            path,
            VFS_OPEN_READ,
            &file) != 0)
    {
        return false;
    }

    size_t file_size = 0u;

    if (vfs_seek(
            file,
            0,
            VFS_SEEK_END,
            &file_size) != 0)
    {
        vfs_close(file);
        return false;
    }

    if (file_size == 0u ||
        file_size > GUI_FONT_MAX_FILE_SIZE)
    {
        vfs_close(file);
        return false;
    }

    size_t ignored_offset = 0u;

    if (vfs_seek(
            file,
            0,
            VFS_SEEK_SET,
            &ignored_offset) != 0)
    {
        vfs_close(file);
        return false;
    }

    uint8_t *data =
        kmalloc(file_size);

    if (data == NULL)
    {
        vfs_close(file);
        return false;
    }

    size_t total = 0u;

    while (total < file_size)
    {
        size_t bytes_read = 0u;

        if (vfs_read(
                file,
                data + total,
                file_size - total,
                &bytes_read) != 0)
        {
            kfree(data);
            vfs_close(file);
            return false;
        }

        if (bytes_read == 0u)
        {
            kfree(data);
            vfs_close(file);
            return false;
        }

        total += bytes_read;
    }

    vfs_close(file);

    *data_result = data;
    *size_result = file_size;

    return true;
}


static gui_cached_glyph_t *
gui_font_find_cached_glyph(
    gui_font_t *font,
    uint32_t codepoint,
    uint32_t pixel_height)
{
    if (font == NULL)
        return NULL;

    gui_cached_glyph_t *glyph =
        font->glyph_cache;

    while (glyph != NULL)
    {
        if (glyph->codepoint == codepoint &&
            glyph->pixel_height == pixel_height)
        {
            return glyph;
        }

        glyph = glyph->next;
    }

    return NULL;
}


static bool gui_font_rasterize_glyph(
    gui_font_t *font,
    uint32_t codepoint,
    uint32_t pixel_height,
    gui_cached_glyph_t **result)
{
    if (font == NULL ||
        result == NULL ||
        pixel_height == 0u)
    {
        return false;
    }

    *result = NULL;

    gui_cached_glyph_t *existing =
        gui_font_find_cached_glyph(
            font,
            codepoint,
            pixel_height);

    if (existing != NULL)
    {
        *result = existing;
        return true;
    }

    float scale =
        stbtt_ScaleForPixelHeight(
            &font->info,
            (float)pixel_height);

    if (scale <= 0.0f)
        return false;

    int advance_units = 0;
    int left_side_bearing_units = 0;

    stbtt_GetCodepointHMetrics(
        &font->info,
        (int)codepoint,
        &advance_units,
        &left_side_bearing_units);

    /*
     * left_side_bearing_units is deliberately not used directly.
     *
     * x0 from the rasterized bitmap box is the actual offset needed
     * to position the generated bitmap correctly relative to the pen.
     */
    (void)left_side_bearing_units;

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    stbtt_GetCodepointBitmapBox(
        &font->info,
        (int)codepoint,
        scale,
        scale,
        &x0,
        &y0,
        &x1,
        &y1);

    if (x1 < x0 ||
        y1 < y0)
    {
        return false;
    }

    uint32_t width =
        (uint32_t)(x1 - x0);

    uint32_t height =
        (uint32_t)(y1 - y0);

    gui_cached_glyph_t *glyph =
        kmalloc(sizeof(*glyph));

    if (glyph == NULL)
        return false;

    memset(
        glyph,
        0,
        sizeof(*glyph));

    glyph->codepoint =
        codepoint;

    glyph->pixel_height =
        pixel_height;

    glyph->metrics.width =
        width;

    glyph->metrics.height =
        height;

    glyph->metrics.bearing_x =
        x0;

    glyph->metrics.bearing_y =
        -y0;

    glyph->metrics.advance =
        gui_font_round_float(
            (float)advance_units *
            scale);

    if (width != 0u &&
        height != 0u)
    {
        if ((size_t)height >
            SIZE_MAX / (size_t)width)
        {
            kfree(glyph);
            return false;
        }

        size_t bitmap_size =
            (size_t)width *
            (size_t)height;

        glyph->bitmap =
            kmalloc(bitmap_size);

        if (glyph->bitmap == NULL)
        {
            kfree(glyph);
            return false;
        }

        memset(
            glyph->bitmap,
            0,
            bitmap_size);

        stbtt_MakeCodepointBitmap(
            &font->info,
            glyph->bitmap,
            (int)width,
            (int)height,
            (int)width,
            scale,
            scale,
            (int)codepoint);
    }

    /*
     * Publish only after the object is completely initialized.
     */
    glyph->next =
        font->glyph_cache;

    font->glyph_cache =
        glyph;

    *result = glyph;

    return true;
}


static uint8_t gui_font_color_red(
    gui_color_t color)
{
    return
        (uint8_t)
        ((color >> 16) & 0xFFu);
}


static uint8_t gui_font_color_green(
    gui_color_t color)
{
    return
        (uint8_t)
        ((color >> 8) & 0xFFu);
}


static uint8_t gui_font_color_blue(
    gui_color_t color)
{
    return
        (uint8_t)
        (color & 0xFFu);
}


static uint8_t gui_font_blend_channel(
    uint8_t destination,
    uint8_t source,
    uint8_t alpha)
{
    uint32_t inverse =
        255u - alpha;

    uint32_t value =
        (uint32_t)source *
            alpha +
        (uint32_t)destination *
            inverse +
        127u;

    return
        (uint8_t)
        (value / 255u);
}


static void gui_font_blend_glyph(
    gui_surface_t *surface,
    const gui_cached_glyph_t *glyph,
    int32_t x,
    int32_t y,
    gui_color_t color)
{
    if (surface == NULL ||
        surface->pixels == NULL ||
        glyph == NULL ||
        glyph->bitmap == NULL)
    {
        return;
    }

    uint32_t color_alpha =
        gui_color_alpha(color);

    for (uint32_t glyph_y = 0u;
         glyph_y < glyph->metrics.height;
         ++glyph_y)
    {
        int64_t destination_y =
            (int64_t)y +
            (int64_t)glyph_y;

        if (destination_y < 0 ||
            destination_y >=
                (int64_t)surface->height)
        {
            continue;
        }

        for (uint32_t glyph_x = 0u;
             glyph_x < glyph->metrics.width;
             ++glyph_x)
        {
            int64_t destination_x =
                (int64_t)x +
                (int64_t)glyph_x;

            if (destination_x < 0 ||
                destination_x >=
                    (int64_t)surface->width)
            {
                continue;
            }

            uint8_t coverage =
                glyph->bitmap[
                    (size_t)glyph_y *
                        glyph->metrics.width +
                    glyph_x];

            if (coverage == 0u)
                continue;

            uint32_t effective_alpha =
                (color_alpha *
                 (uint32_t)coverage +
                 127u) /
                255u;

            if (effective_alpha == 0u)
                continue;

            gui_color_t glyph_color =
                GUI_RGBA(
                    gui_color_red(color),
                    gui_color_green(color),
                    gui_color_blue(color),
                    effective_alpha);

            gui_surface_blend_pixel(
                surface,
                (int32_t)destination_x,
                (int32_t)destination_y,
                glyph_color);
        }
    }
}


/*
 * ------------------------------------------------------------
 * Public font API
 * ------------------------------------------------------------
 */

bool gui_font_load_ttf(
    const char *path,
    gui_font_t **result)
{
    if (path == NULL ||
        result == NULL)
    {
        return false;
    }

    *result = NULL;

    uint8_t *file_data = NULL;
    size_t file_size = 0u;

    if (!gui_font_read_complete_file(
            path,
            &file_data,
            &file_size))
    {
        return false;
    }

    /*
     * A normal sfnt/TrueType header requires at least 12 bytes.
     * stb_truetype expects the memory containing the font to
     * remain valid after initialization.
     */
    if (file_size < 12u)
    {
        kfree(file_data);
        return false;
    }

    int offset =
        stbtt_GetFontOffsetForIndex(
            file_data,
            0);

    if (offset < 0 ||
        (size_t)offset >= file_size)
    {
        kfree(file_data);
        return false;
    }

    gui_font_t *font =
        kmalloc(sizeof(*font));

    if (font == NULL)
    {
        kfree(file_data);
        return false;
    }

    memset(
        font,
        0,
        sizeof(*font));

    font->file_data =
        file_data;

    font->file_size =
        file_size;

    if (!stbtt_InitFont(
            &font->info,
            file_data,
            offset))
    {
        kfree(file_data);
        kfree(font);
        return false;
    }

    *result = font;

    return true;
}


void gui_font_destroy(
    gui_font_t *font)
{
    if (font == NULL)
        return;

    gui_cached_glyph_t *glyph =
        font->glyph_cache;

    while (glyph != NULL)
    {
        gui_cached_glyph_t *next =
            glyph->next;

        if (glyph->bitmap != NULL)
            kfree(glyph->bitmap);

        kfree(glyph);

        glyph = next;
    }

    if (font->file_data != NULL)
        kfree(font->file_data);

    memset(
        font,
        0,
        sizeof(*font));

    kfree(font);
}


bool gui_font_get_glyph_metrics(
    gui_font_t *font,
    uint32_t codepoint,
    uint32_t pixel_height,
    gui_font_glyph_metrics_t *metrics)
{
    if (font == NULL ||
        metrics == NULL ||
        pixel_height == 0u)
    {
        return false;
    }

    gui_cached_glyph_t *glyph = NULL;

    if (!gui_font_rasterize_glyph(
            font,
            codepoint,
            pixel_height,
            &glyph))
    {
        return false;
    }

    *metrics =
        glyph->metrics;

    return true;
}


bool gui_font_get_line_metrics(
    gui_font_t *font,
    uint32_t pixel_height,
    gui_font_line_metrics_t *metrics)
{
    if (font == NULL ||
        metrics == NULL ||
        pixel_height == 0u)
    {
        return false;
    }

    float scale =
        stbtt_ScaleForPixelHeight(
            &font->info,
            (float)pixel_height);

    if (scale <= 0.0f)
        return false;

    int ascent_units = 0;
    int descent_units = 0;
    int line_gap_units = 0;

    stbtt_GetFontVMetrics(
        &font->info,
        &ascent_units,
        &descent_units,
        &line_gap_units);

    metrics->ascent =
        gui_font_round_float(
            (float)ascent_units *
            scale);

    metrics->descent =
        gui_font_round_float(
            (float)descent_units *
            scale);

    metrics->line_gap =
        gui_font_round_float(
            (float)line_gap_units *
            scale);

    metrics->line_height =
        metrics->ascent -
        metrics->descent +
        metrics->line_gap;

    if (metrics->line_height <= 0)
        metrics->line_height =
            (int32_t)pixel_height;

    return true;
}


bool gui_font_draw_text(
    gui_surface_t *surface,
    gui_font_t *font,
    int32_t x,
    int32_t y,
    uint32_t pixel_height,
    const char *text,
    gui_color_t color)
{
    if (surface == NULL ||
        surface->pixels == NULL ||
        font == NULL ||
        text == NULL ||
        pixel_height == 0u)
    {
        return false;
    }

    gui_font_line_metrics_t line_metrics;

    if (!gui_font_get_line_metrics(
            font,
            pixel_height,
            &line_metrics))
    {
        return false;
    }

    int32_t line_start_x =
        x;

    int32_t pen_x =
        x;

    int32_t baseline_y =
        y +
        line_metrics.ascent;

    gui_cached_glyph_t *space_glyph = NULL;

    if (!gui_font_rasterize_glyph(
            font,
            (uint32_t)' ',
            pixel_height,
            &space_glyph))
    {
        return false;
    }

    const unsigned char *cursor =
        (const unsigned char *)text;

    while (*cursor != '\0')
    {
        unsigned char character =
            *cursor++;

        if (character == '\n')
        {
            pen_x =
                line_start_x;

            baseline_y +=
                line_metrics.line_height;

            continue;
        }

        if (character == '\t')
        {
            pen_x +=
                space_glyph->metrics.advance *
                4;

            continue;
        }

        uint32_t codepoint;

        if (character < 0x80u)
        {
            codepoint =
                character;
        }
        else
        {
            /*
             * ASCII-first milestone.
             *
             * The cache/rasterizer itself already accepts Unicode
             * codepoints. A UTF-8 decoder can replace this fallback
             * later without changing gui_font_t.
             */
            codepoint =
                (uint32_t)'?';
        }

        gui_cached_glyph_t *glyph = NULL;

        if (!gui_font_rasterize_glyph(
                font,
                codepoint,
                pixel_height,
                &glyph))
        {
            return false;
        }

        int32_t glyph_x =
            pen_x +
            glyph->metrics.bearing_x;

        int32_t glyph_y =
            baseline_y -
            glyph->metrics.bearing_y;

        gui_font_blend_glyph(
            surface,
            glyph,
            glyph_x,
            glyph_y,
            color);

        pen_x +=
            glyph->metrics.advance;
    }

    return true;
}


/*
 * ------------------------------------------------------------
 * System default font
 * ------------------------------------------------------------
 */

bool gui_font_system_initialize(void)
{
    if (default_font != NULL)
        return true;

    gui_font_t *font = NULL;

    if (!gui_font_load_ttf(
            GUI_DEFAULT_FONT_PATH,
            &font))
    {
        return false;
    }

    default_font =
        font;

    return true;
}


gui_font_t *gui_font_default(void)
{
    return default_font;
}