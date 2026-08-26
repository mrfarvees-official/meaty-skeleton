#ifndef KERNEL_GUI_WIDGET_H
#define KERNEL_GUI_WIDGET_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/gui/surface.h>


typedef struct gui_widget gui_widget_t;


/*
 * Widget render callback.
 *
 * bounds:
 *     absolute coordinates inside destination
 *
 * clip:
 *     visible portion inherited from the complete widget tree
 *
 * A container widget may have no render callback at all.
 */
typedef bool (*gui_widget_render_fn)(
    gui_widget_t *widget,
    gui_surface_t *destination,
    gui_rect_t bounds,
    gui_rect_t clip);


/*
 * Optional component-owned opaque state.
 *
 * Example:
 *
 *     gui_button_t
 *         contains gui_widget_t widget
 *
 * Most components can recover themselves through widget->context.
 */
typedef struct gui_widget_operations
{
    gui_widget_render_fn render;

} gui_widget_operations_t;


/*
 * ------------------------------------------------------------
 * Base widget
 * ------------------------------------------------------------
 *
 * Widgets do not allocate memory.
 *
 * Components embed this structure:
 *
 *     typedef struct gui_button
 *     {
 *         gui_widget_t widget;
 *         ...
 *     } gui_button_t;
 *
 * Coordinates are LOCAL to the parent.
 *
 * Root widgets use destination-surface coordinates directly.
 */
struct gui_widget
{
    gui_rect_t bounds;

    bool visible;
    bool enabled;

    gui_widget_t *parent;

    gui_widget_t *first_child;
    gui_widget_t *last_child;

    gui_widget_t *previous_sibling;
    gui_widget_t *next_sibling;

    const gui_widget_operations_t *operations;

    void *context;
};


/*
 * ------------------------------------------------------------
 * Lifetime / configuration
 * ------------------------------------------------------------
 */

void gui_widget_initialize(
    gui_widget_t *widget,
    gui_rect_t bounds,
    const gui_widget_operations_t *operations,
    void *context);

void gui_widget_reset(
    gui_widget_t *widget);


void gui_widget_set_bounds(
    gui_widget_t *widget,
    gui_rect_t bounds);

gui_rect_t gui_widget_bounds(
    const gui_widget_t *widget);


void gui_widget_set_visible(
    gui_widget_t *widget,
    bool visible);

bool gui_widget_is_visible(
    const gui_widget_t *widget);


void gui_widget_set_enabled(
    gui_widget_t *widget,
    bool enabled);

bool gui_widget_is_enabled(
    const gui_widget_t *widget);


/*
 * ------------------------------------------------------------
 * Tree ownership
 * ------------------------------------------------------------
 *
 * add_child() does not allocate.
 *
 * If child already belongs to another parent it is detached first.
 *
 * Cycles are rejected.
 */
bool gui_widget_add_child(
    gui_widget_t *parent,
    gui_widget_t *child);

void gui_widget_remove_child(
    gui_widget_t *parent,
    gui_widget_t *child);

void gui_widget_detach(
    gui_widget_t *widget);


/*
 * ------------------------------------------------------------
 * Geometry
 * ------------------------------------------------------------
 */

/*
 * Convert local widget bounds to destination-surface coordinates.
 */
gui_rect_t gui_widget_absolute_bounds(
    const gui_widget_t *widget);


/*
 * Point test against this widget only.
 *
 * Coordinates are destination-surface coordinates.
 */
bool gui_widget_contains_point(
    const gui_widget_t *widget,
    int32_t x,
    int32_t y);


/*
 * ------------------------------------------------------------
 * Hit testing
 * ------------------------------------------------------------
 *
 * Searches children from last to first.
 *
 * Therefore later children behave as visually/front-most children.
 *
 * Invisible and disabled widgets are ignored.
 */
gui_widget_t *gui_widget_hit_test(
    gui_widget_t *root,
    int32_t x,
    int32_t y);


/*
 * ------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------
 *
 * Traversal order:
 *
 *     parent
 *     first child
 *     ...
 *     last child
 *
 * Later children therefore render above earlier children.
 *
 * Children are clipped to their parent's visible region.
 */
bool gui_widget_render_tree(
    gui_widget_t *root,
    gui_surface_t *destination);


#endif