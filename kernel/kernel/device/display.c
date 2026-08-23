#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/display.h>
#include <kernel/framebuffer.h>

#include "../../arch/i386/io.h"


#define BGA_INDEX_PORT 0x01CEu
#define BGA_DATA_PORT  0x01CFu


#define BGA_INDEX_ID               0x00u
#define BGA_INDEX_XRES             0x01u
#define BGA_INDEX_YRES             0x02u
#define BGA_INDEX_BPP              0x03u
#define BGA_INDEX_ENABLE           0x04u
#define BGA_INDEX_BANK             0x05u
#define BGA_INDEX_VIRT_WIDTH       0x06u
#define BGA_INDEX_VIRT_HEIGHT      0x07u
#define BGA_INDEX_X_OFFSET         0x08u
#define BGA_INDEX_Y_OFFSET         0x09u
#define BGA_INDEX_VIDEO_MEMORY_64K 0x0Au


#define BGA_DISABLED     0x0000u
#define BGA_ENABLED      0x0001u
#define BGA_GETCAPS      0x0002u
#define BGA_LFB_ENABLED  0x0040u
#define BGA_NOCLEARMEM   0x0080u


#define BGA_ID0 0xB0C0u
#define BGA_ID1 0xB0C1u
#define BGA_ID2 0xB0C2u
#define BGA_ID3 0xB0C3u
#define BGA_ID4 0xB0C4u
#define BGA_ID5 0xB0C5u


typedef struct display_state
{
    bool available;

    display_capabilities_t capabilities;

} display_state_t;


static display_state_t display_state;


static void bga_write(
    uint16_t index,
    uint16_t value)
{
    outw(
        BGA_INDEX_PORT,
        index);

    outw(
        BGA_DATA_PORT,
        value);
}


static uint16_t bga_read(
    uint16_t index)
{
    outw(
        BGA_INDEX_PORT,
        index);

    return inw(
        BGA_DATA_PORT);
}


static bool bga_id_supported(
    uint16_t id)
{
    return
        id == BGA_ID0 ||
        id == BGA_ID1 ||
        id == BGA_ID2 ||
        id == BGA_ID3 ||
        id == BGA_ID4 ||
        id == BGA_ID5;
}


bool display_initialize(void)
{
    display_state.available =
        false;

    if (!framebuffer_is_available())
        return false;

    uint16_t id =
        bga_read(
            BGA_INDEX_ID);

    if (!bga_id_supported(id))
        return false;

    /*
     * Ask the device to return capability maxima rather than current
     * X/Y/BPP values.
     */
    uint16_t previous_enable =
        bga_read(
            BGA_INDEX_ENABLE);

    bga_write(
        BGA_INDEX_ENABLE,
        BGA_GETCAPS);

    uint32_t max_width =
        bga_read(
            BGA_INDEX_XRES);

    uint32_t max_height =
        bga_read(
            BGA_INDEX_YRES);

    uint32_t max_bpp =
        bga_read(
            BGA_INDEX_BPP);

    /*
     * Return to the previous enabled state.
     */
    bga_write(
        BGA_INDEX_ENABLE,
        previous_enable);

    uint32_t memory_blocks =
        bga_read(
            BGA_INDEX_VIDEO_MEMORY_64K);

    if (memory_blocks == 0u)
        return false;

    size_t video_memory_bytes =
        (size_t)memory_blocks *
        64u *
        1024u;

    if (max_width == 0u ||
        max_height == 0u ||
        max_bpp < 32u)
    {
        return false;
    }

    /*
     * Map all video RAM once.
     *
     * Runtime mode changes therefore do not require changing the
     * kernel virtual mapping.
     */
    if (!framebuffer_map_capacity(
            video_memory_bytes))
    {
        return false;
    }

    display_state.capabilities.max_width =
        max_width;

    display_state.capabilities.max_height =
        max_height;

    display_state.capabilities.max_bpp =
        max_bpp;

    display_state.capabilities.video_memory_bytes =
        video_memory_bytes;

    display_state.available =
        true;

    return true;
}


bool display_is_available(void)
{
    return display_state.available;
}


bool display_get_capabilities(
    display_capabilities_t *capabilities)
{
    if (!display_state.available ||
        capabilities == NULL)
    {
        return false;
    }

    *capabilities =
        display_state.capabilities;

    return true;
}


bool display_get_mode(
    display_mode_t *mode)
{
    if (!display_state.available ||
        mode == NULL)
    {
        return false;
    }

    uint32_t width =
        bga_read(
            BGA_INDEX_XRES);

    uint32_t height =
        bga_read(
            BGA_INDEX_YRES);

    uint32_t bpp =
        bga_read(
            BGA_INDEX_BPP);

    uint32_t virtual_width =
        bga_read(
            BGA_INDEX_VIRT_WIDTH);

    if (virtual_width <
        width)
    {
        virtual_width =
            width;
    }

    mode->width =
        width;

    mode->height =
        height;

    mode->bpp =
        bpp;

    mode->pitch =
        virtual_width *
        (bpp / 8u);

    return true;
}


bool display_set_mode(
    uint32_t width,
    uint32_t height,
    uint32_t bpp,
    display_mode_t *accepted_mode)
{
    if (!display_state.available)
        return false;

    if (width == 0u ||
        height == 0u)
    {
        return false;
    }

    /*
     * G1/G2 framebuffer renderer supports only 32 bpp.
     */
    if (bpp != 32u)
        return false;

    if (width >
            display_state
                .capabilities
                .max_width ||
        height >
            display_state
                .capabilities
                .max_height)
    {
        return false;
    }

    uint64_t requested_bytes =
        (uint64_t)width *
        (uint64_t)height *
        4u;

    if (requested_bytes >
        display_state
            .capabilities
            .video_memory_bytes)
    {
        return false;
    }

    /*
     * Bochs VBE registers XRES/YRES/BPP may only be changed while the
     * interface is disabled.
     */
    bga_write(
        BGA_INDEX_ENABLE,
        BGA_DISABLED);

    bga_write(
        BGA_INDEX_XRES,
        (uint16_t)width);

    bga_write(
        BGA_INDEX_YRES,
        (uint16_t)height);

    bga_write(
        BGA_INDEX_BPP,
        (uint16_t)bpp);

    /*
     * Let hardware choose a virtual width at least as wide as XRES.
     */
    bga_write(
        BGA_INDEX_VIRT_WIDTH,
        (uint16_t)width);

    bga_write(
        BGA_INDEX_X_OFFSET,
        0u);

    bga_write(
        BGA_INDEX_Y_OFFSET,
        0u);

    /*
     * Enable linear framebuffer.
     *
     * We intentionally allow the mode switch to clear video RAM.
     */
    bga_write(
        BGA_INDEX_ENABLE,
        BGA_ENABLED |
        BGA_LFB_ENABLED);

    display_mode_t actual;

    if (!display_get_mode(
            &actual))
    {
        return false;
    }

    if (actual.bpp != 32u)
        return false;

    uint64_t actual_bytes =
        (uint64_t)actual.pitch *
        (uint64_t)actual.height;

    if (actual_bytes >
        display_state
            .capabilities
            .video_memory_bytes)
    {
        return false;
    }

    if (!framebuffer_set_geometry(
            actual.width,
            actual.height,
            actual.pitch,
            actual.bpp))
    {
        return false;
    }

    if (accepted_mode != NULL)
    {
        *accepted_mode =
            actual;
    }

    return true;
}