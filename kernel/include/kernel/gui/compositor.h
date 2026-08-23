#ifndef KERNEL_GUI_COMPOSITOR_H
#define KERNEL_GUI_COMPOSITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/gui/surface.h>


#define GUI_COMPOSITOR_MAX_DAMAGE_RECTS 32u


bool gui_compositor_initialize(void);

bool gui_compositor_is_initialized(void);


/*
 * Screen-sized off-screen composition target.
 *
 * Callers may draw here, but framebuffer presentation remains
 * compositor-owned.
 */
gui_surface_t *
gui_compositor_surface(void);


/*
 * Mark part of the screen as requiring presentation.
 *
 * The compositor clips damage to screen bounds and merges
 * overlapping/touching regions where possible.
 */
void gui_compositor_damage(
    gui_rect_t rect);

void gui_compositor_damage_all(void);


/*
 * Transfer all current dirty regions from the backbuffer
 * into the hardware framebuffer.
 */
void gui_compositor_present(void);


size_t
gui_compositor_damage_count(void);


#endif