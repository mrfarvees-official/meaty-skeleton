#ifndef KERNEL_GUI_DESKTOP_H
#define KERNEL_GUI_DESKTOP_H

#include <stdbool.h>


typedef enum gui_wallpaper_mode
{
    /*
     * Ignore aspect ratio and exactly match the screen.
     */
    GUI_WALLPAPER_STRETCH = 0,

    /*
     * Preserve aspect ratio.
     *
     * Entire image remains visible. Empty screen area uses the
     * desktop fallback gradient.
     */
    GUI_WALLPAPER_FIT,

    /*
     * Preserve aspect ratio and cover the entire screen.
     *
     * Excess image area is center-cropped.
     *
     * This is the default modern-desktop behavior.
     */
    GUI_WALLPAPER_FILL,

    /*
     * Native image size, centered on screen.
     */
    GUI_WALLPAPER_CENTER

} gui_wallpaper_mode_t;


/*
 * Initialize compositor-owned desktop shell content.
 */
bool gui_desktop_initialize(void);


/*
 * Reconstruct and present the complete desktop scene.
 */
void gui_desktop_render(void);


/*
 * ------------------------------------------------------------
 * Wallpaper
 * ------------------------------------------------------------
 *
 * Load/select a filesystem-backed wallpaper.
 *
 * The image subsystem owns the returned cached image.
 *
 * On success the desktop immediately uses the new wallpaper.
 *
 * Before desktop initialization the selection is stored and will
 * be used during initial rendering.
 */
bool gui_desktop_set_wallpaper(
    const char *path,
    gui_wallpaper_mode_t mode);


/*
 * Current selected wallpaper path.
 *
 * Returns NULL when no wallpaper has successfully been selected.
 */
const char *gui_desktop_wallpaper_path(void);


/*
 * Current rendering mode.
 */
gui_wallpaper_mode_t gui_desktop_wallpaper_mode(void);


#endif