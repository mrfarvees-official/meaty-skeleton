#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/image.h>
#include <kernel/gui/surface.h>
#include <kernel/heap.h>
#include <kernel/vfs.h>


/*
 * ------------------------------------------------------------
 * stb_image freestanding adaptation
 * ------------------------------------------------------------
 *
 * Keep the third-party decoder completely private to this
 * translation unit.
 *
 * Initial image milestone:
 *
 *     PNG only
 *     memory input only
 *     8-bit RGBA output
 */

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS

/*
 * Protect the kernel from absurd/corrupt dimensions before stb
 * attempts enormous allocations.
 */
#define STBI_MAX_DIMENSIONS 8192

#define STBI_MALLOC(size) \
    kmalloc((size_t)(size))

#define STBI_REALLOC(pointer, new_size) \
    krealloc((pointer), (size_t)(new_size))

#define STBI_FREE(pointer) \
    kfree((pointer))

#define STBI_ASSERT(expression) \
    ((expression) ? (void)0 : __builtin_trap())

#include <stb_image.h>


/*
 * ------------------------------------------------------------
 * Limits
 * ------------------------------------------------------------
 */

#define GUI_IMAGE_MAX_FILE_SIZE \
    (32u * 1024u * 1024u)


/*
 * ------------------------------------------------------------
 * Image object/cache
 * ------------------------------------------------------------
 */

struct gui_image
{
    char *path;

    gui_surface_t surface;

    struct gui_image *next;
};


static gui_image_t *image_cache;


/*
 * ------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------
 */

static char *gui_image_copy_string(
    const char *value)
{
    if (value == NULL)
        return NULL;

    size_t length =
        strlen(value);

    if (length == SIZE_MAX)
        return NULL;

    char *copy =
        kmalloc(length + 1u);

    if (copy == NULL)
        return NULL;

    memcpy(
        copy,
        value,
        length + 1u);

    return copy;
}


static gui_image_t *gui_image_find_cached(
    const char *path)
{
    if (path == NULL)
        return NULL;

    for (gui_image_t *image = image_cache;
         image != NULL;
         image = image->next)
    {
        if (image->path != NULL &&
            strcmp(
                image->path,
                path) == 0)
        {
            return image;
        }
    }

    return NULL;
}


static bool gui_image_read_complete_file(
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

    /*
     * stb_image takes the input length as int.
     */
    if (file_size == 0u ||
        file_size > GUI_IMAGE_MAX_FILE_SIZE ||
        file_size > (size_t)INT_MAX)
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

        total +=
            bytes_read;
    }

    vfs_close(file);

    *data_result = data;
    *size_result = file_size;

    return true;
}


static bool gui_image_decode_png(
    const uint8_t *file_data,
    size_t file_size,
    gui_surface_t *surface_result)
{
    if (file_data == NULL ||
        file_size == 0u ||
        surface_result == NULL ||
        file_size > (size_t)INT_MAX)
    {
        return false;
    }

    memset(
        surface_result,
        0,
        sizeof(*surface_result));

    int width = 0;
    int height = 0;
    int source_channels = 0;

    /*
     * Always request RGBA.
     *
     * stb_image returns bytes in:
     *
     *     R G B A
     *
     * order.
     */
    stbi_uc *decoded =
        stbi_load_from_memory(
            file_data,
            (int)file_size,
            &width,
            &height,
            &source_channels,
            4);

    (void)source_channels;

    if (decoded == NULL)
        return false;

    if (width <= 0 ||
        height <= 0)
    {
        stbi_image_free(decoded);
        return false;
    }

    uint32_t image_width =
        (uint32_t)width;

    uint32_t image_height =
        (uint32_t)height;

    if (image_width >
        UINT32_MAX /
            sizeof(gui_color_t))
    {
        stbi_image_free(decoded);
        return false;
    }

    size_t pitch =
        (size_t)image_width *
        sizeof(gui_color_t);

    if ((size_t)image_height >
        SIZE_MAX / pitch)
    {
        stbi_image_free(decoded);
        return false;
    }

    size_t pixel_count =
        (size_t)image_width *
        (size_t)image_height;

    /*
     * stb allocated exactly four bytes per requested RGBA pixel.
     *
     * gui_color_t is also four bytes, so we can convert in place
     * and transfer ownership of the allocation directly into the
     * gui_surface_t.
     *
     * This avoids allocating a second ~4 MiB framebuffer-sized
     * buffer while loading a 1360x768 wallpaper.
     */
    uint8_t *rgba =
        decoded;

    gui_color_t *pixels =
        (gui_color_t *)decoded;

    for (size_t index = 0u;
         index < pixel_count;
         ++index)
    {
        uint8_t red =
            rgba[index * 4u + 0u];

        uint8_t green =
            rgba[index * 4u + 1u];

        uint8_t blue =
            rgba[index * 4u + 2u];

        uint8_t alpha =
            rgba[index * 4u + 3u];

        pixels[index] =
            GUI_RGBA(
                red,
                green,
                blue,
                alpha);
    }

    surface_result->width =
        image_width;

    surface_result->height =
        image_height;

    surface_result->pitch =
        (uint32_t)pitch;

    surface_result->pixels =
        pixels;

    /*
     * Allocation came from STBI_MALLOC -> kmalloc(), so ordinary
     * gui_surface_destroy() can eventually kfree it safely.
     */
    surface_result->owns_pixels =
        true;

    return true;
}


static bool gui_image_load_uncached(
    const char *path,
    gui_image_t **result)
{
    if (path == NULL ||
        result == NULL)
    {
        return false;
    }

    *result = NULL;

    uint8_t *file_data = NULL;
    size_t file_size = 0u;

    if (!gui_image_read_complete_file(
            path,
            &file_data,
            &file_size))
    {
        return false;
    }

    gui_surface_t decoded_surface;

    memset(
        &decoded_surface,
        0,
        sizeof(decoded_surface));

    bool decoded =
        gui_image_decode_png(
            file_data,
            file_size,
            &decoded_surface);

    /*
     * Decoder no longer references compressed PNG bytes.
     */
    kfree(file_data);

    if (!decoded)
        return false;

    gui_image_t *image =
        kmalloc(sizeof(*image));

    if (image == NULL)
    {
        gui_surface_destroy(
            &decoded_surface);

        return false;
    }

    memset(
        image,
        0,
        sizeof(*image));

    image->path =
        gui_image_copy_string(path);

    if (image->path == NULL)
    {
        gui_surface_destroy(
            &decoded_surface);

        kfree(image);

        return false;
    }

    image->surface =
        decoded_surface;

    *result =
        image;

    return true;
}


/*
 * ------------------------------------------------------------
 * Public cache API
 * ------------------------------------------------------------
 */

bool gui_image_get(
    const char *path,
    const gui_image_t **result)
{
    if (path == NULL ||
        result == NULL)
    {
        return false;
    }

    *result = NULL;

    gui_image_t *cached =
        gui_image_find_cached(path);

    if (cached != NULL)
    {
        *result = cached;
        return true;
    }

    /*
     * Initial GUI ownership is BSP/single-threaded.
     *
     * When asynchronous application-driven image loading arrives,
     * this cache should gain its own synchronization.
     */
    gui_image_t *image = NULL;

    if (!gui_image_load_uncached(
            path,
            &image))
    {
        return false;
    }

    image->next =
        image_cache;

    image_cache =
        image;

    *result =
        image;

    return true;
}


uint32_t gui_image_width(
    const gui_image_t *image)
{
    if (image == NULL)
        return 0u;

    return
        image->surface.width;
}


uint32_t gui_image_height(
    const gui_image_t *image)
{
    if (image == NULL)
        return 0u;

    return
        image->surface.height;
}


/*
 * ------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------
 */

void gui_image_draw(
    gui_surface_t *destination,
    const gui_image_t *image,
    int32_t x,
    int32_t y)
{
    if (destination == NULL ||
        destination->pixels == NULL ||
        image == NULL ||
        image->surface.pixels == NULL)
    {
        return;
    }

    const gui_surface_t *source =
        &image->surface;

    for (uint32_t source_y = 0u;
         source_y < source->height;
         ++source_y)
    {
        int64_t destination_y =
            (int64_t)y +
            (int64_t)source_y;

        if (destination_y < 0 ||
            destination_y >=
                (int64_t)destination->height)
        {
            continue;
        }

        const uint8_t *source_row_bytes =
            (const uint8_t *)source->pixels +
            (size_t)source_y *
                source->pitch;

        const gui_color_t *source_row =
            (const gui_color_t *)
                source_row_bytes;

        for (uint32_t source_x = 0u;
             source_x < source->width;
             ++source_x)
        {
            int64_t destination_x =
                (int64_t)x +
                (int64_t)source_x;

            if (destination_x < 0 ||
                destination_x >=
                    (int64_t)destination->width)
            {
                continue;
            }

            gui_color_t color =
                source_row[source_x];

            uint8_t alpha =
                gui_color_alpha(color);

            if (alpha == 0u)
                continue;

            if (alpha == 255u)
            {
                gui_surface_put_pixel(
                    destination,
                    (int32_t)destination_x,
                    (int32_t)destination_y,
                    color);
            }
            else
            {
                gui_surface_blend_pixel(
                    destination,
                    (int32_t)destination_x,
                    (int32_t)destination_y,
                    color);
            }
        }
    }
}

void gui_image_draw_scaled(
    gui_surface_t *destination,
    const gui_image_t *image,
    gui_rect_t destination_rect)
{
    if (destination == NULL ||
        destination->pixels == NULL ||
        image == NULL ||
        image->surface.pixels == NULL ||
        destination_rect.width == 0u ||
        destination_rect.height == 0u)
    {
        return;
    }

    const gui_surface_t *source =
        &image->surface;

    if (source->width == 0u ||
        source->height == 0u)
    {
        return;
    }

    /*
     * Use 64-bit coordinates because a wallpaper FILL rectangle
     * may intentionally extend beyond the screen and may start at
     * a negative coordinate.
     */
    int64_t destination_left =
        destination_rect.x;

    int64_t destination_top =
        destination_rect.y;

    int64_t destination_right =
        destination_left +
        (int64_t)destination_rect.width;

    int64_t destination_bottom =
        destination_top +
        (int64_t)destination_rect.height;

    /*
     * Clip against the destination surface.
     */
    int64_t clipped_left =
        destination_left;

    int64_t clipped_top =
        destination_top;

    int64_t clipped_right =
        destination_right;

    int64_t clipped_bottom =
        destination_bottom;

    if (clipped_left < 0)
        clipped_left = 0;

    if (clipped_top < 0)
        clipped_top = 0;

    if (clipped_right >
        (int64_t)destination->width)
    {
        clipped_right =
            destination->width;
    }

    if (clipped_bottom >
        (int64_t)destination->height)
    {
        clipped_bottom =
            destination->height;
    }

    if (clipped_left >= clipped_right ||
        clipped_top >= clipped_bottom)
    {
        return;
    }

    for (int64_t destination_y = clipped_top;
         destination_y < clipped_bottom;
         ++destination_y)
    {
        uint64_t local_y =
            (uint64_t)
            (destination_y -
             destination_top);

        uint32_t source_y =
            (uint32_t)
            ((local_y *
              (uint64_t)source->height) /
             destination_rect.height);

        if (source_y >= source->height)
            source_y = source->height - 1u;

        const uint8_t *source_row_bytes =
            (const uint8_t *)source->pixels +
            (size_t)source_y *
                source->pitch;

        const gui_color_t *source_row =
            (const gui_color_t *)
                source_row_bytes;

        for (int64_t destination_x = clipped_left;
             destination_x < clipped_right;
             ++destination_x)
        {
            uint64_t local_x =
                (uint64_t)
                (destination_x -
                 destination_left);

            uint32_t source_x =
                (uint32_t)
                ((local_x *
                  (uint64_t)source->width) /
                 destination_rect.width);

            if (source_x >= source->width)
                source_x = source->width - 1u;

            gui_color_t color =
                source_row[source_x];

            uint8_t alpha =
                gui_color_alpha(color);

            if (alpha == 0u)
                continue;

            if (alpha == 255u)
            {
                gui_surface_put_pixel(
                    destination,
                    (int32_t)destination_x,
                    (int32_t)destination_y,
                    color);
            }
            else
            {
                gui_surface_blend_pixel(
                    destination,
                    (int32_t)destination_x,
                    (int32_t)destination_y,
                    color);
            }
        }
    }
}
