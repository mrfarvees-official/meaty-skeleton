#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/gui/surface.h>
#include <kernel/gui/widget.h>


/*
 * ------------------------------------------------------------
 * Geometry helpers
 * ------------------------------------------------------------
 */

static bool gui_widget_point_in_rect(
    gui_rect_t rect,
    int32_t x,
    int32_t y)
{
    if (rect.width == 0u ||
        rect.height == 0u)
    {
        return false;
    }

    int64_t left =
        (int64_t)rect.x;

    int64_t top =
        (int64_t)rect.y;

    int64_t right =
        left +
        (int64_t)rect.width;

    int64_t bottom =
        top +
        (int64_t)rect.height;

    return
        (int64_t)x >= left &&
        (int64_t)y >= top &&
        (int64_t)x < right &&
        (int64_t)y < bottom;
}


static gui_rect_t gui_widget_surface_bounds(
    const gui_surface_t *surface)
{
    gui_rect_t bounds;

    bounds.x = 0;
    bounds.y = 0;

    bounds.width = 0u;
    bounds.height = 0u;

    if (surface == NULL)
        return bounds;

    bounds.width =
        surface->width;

    bounds.height =
        surface->height;

    return bounds;
}


/*
 * ------------------------------------------------------------
 * Lifetime / configuration
 * ------------------------------------------------------------
 */

void gui_widget_initialize(
    gui_widget_t *widget,
    gui_rect_t bounds,
    const gui_widget_operations_t *operations,
    void *context)
{
    if (widget == NULL)
        return;

    memset(
        widget,
        0,
        sizeof(*widget));

    widget->bounds =
        bounds;

    widget->visible =
        true;

    widget->enabled =
        true;

    widget->operations =
        operations;

    widget->context =
        context;
}


void gui_widget_reset(
    gui_widget_t *widget)
{
    if (widget == NULL)
        return;

    gui_widget_detach(
        widget);

    /*
     * Detach every child without destroying the child object.
     *
     * Widget lifetime remains owned by the embedding component.
     */
    gui_widget_t *child =
        widget->first_child;

    while (child != NULL)
    {
        gui_widget_t *next =
            child->next_sibling;

        child->parent =
            NULL;

        child->previous_sibling =
            NULL;

        child->next_sibling =
            NULL;

        child =
            next;
    }

    memset(
        widget,
        0,
        sizeof(*widget));
}


void gui_widget_set_bounds(
    gui_widget_t *widget,
    gui_rect_t bounds)
{
    if (widget == NULL)
        return;

    widget->bounds =
        bounds;
}


gui_rect_t gui_widget_bounds(
    const gui_widget_t *widget)
{
    gui_rect_t result;

    result.x = 0;
    result.y = 0;

    result.width = 0u;
    result.height = 0u;

    if (widget == NULL)
        return result;

    return
        widget->bounds;
}


void gui_widget_set_visible(
    gui_widget_t *widget,
    bool visible)
{
    if (widget == NULL)
        return;

    widget->visible =
        visible;
}


bool gui_widget_is_visible(
    const gui_widget_t *widget)
{
    return
        widget != NULL &&
        widget->visible;
}


void gui_widget_set_enabled(
    gui_widget_t *widget,
    bool enabled)
{
    if (widget == NULL)
        return;

    widget->enabled =
        enabled;
}


bool gui_widget_is_enabled(
    const gui_widget_t *widget)
{
    return
        widget != NULL &&
        widget->enabled;
}


/*
 * ------------------------------------------------------------
 * Tree helpers
 * ------------------------------------------------------------
 */

static bool gui_widget_is_ancestor(
    const gui_widget_t *possible_ancestor,
    const gui_widget_t *widget)
{
    if (possible_ancestor == NULL ||
        widget == NULL)
    {
        return false;
    }

    const gui_widget_t *current =
        widget->parent;

    while (current != NULL)
    {
        if (current ==
            possible_ancestor)
        {
            return true;
        }

        current =
            current->parent;
    }

    return false;
}


bool gui_widget_add_child(
    gui_widget_t *parent,
    gui_widget_t *child)
{
    if (parent == NULL ||
        child == NULL ||
        parent == child)
    {
        return false;
    }

    /*
     * child cannot become parent of one of its own ancestors.
     */
    if (gui_widget_is_ancestor(
            child,
            parent))
    {
        return false;
    }

    if (child->parent ==
        parent)
    {
        return true;
    }

    gui_widget_detach(
        child);

    child->parent =
        parent;

    child->previous_sibling =
        parent->last_child;

    child->next_sibling =
        NULL;

    if (parent->last_child !=
        NULL)
    {
        parent->last_child->next_sibling =
            child;
    }
    else
    {
        parent->first_child =
            child;
    }

    parent->last_child =
        child;

    return true;
}


void gui_widget_remove_child(
    gui_widget_t *parent,
    gui_widget_t *child)
{
    if (parent == NULL ||
        child == NULL ||
        child->parent !=
            parent)
    {
        return;
    }

    gui_widget_detach(
        child);
}


void gui_widget_detach(
    gui_widget_t *widget)
{
    if (widget == NULL ||
        widget->parent == NULL)
    {
        return;
    }

    gui_widget_t *parent =
        widget->parent;

    if (widget->previous_sibling !=
        NULL)
    {
        widget->previous_sibling->next_sibling =
            widget->next_sibling;
    }
    else
    {
        parent->first_child =
            widget->next_sibling;
    }

    if (widget->next_sibling !=
        NULL)
    {
        widget->next_sibling->previous_sibling =
            widget->previous_sibling;
    }
    else
    {
        parent->last_child =
            widget->previous_sibling;
    }

    widget->parent =
        NULL;

    widget->previous_sibling =
        NULL;

    widget->next_sibling =
        NULL;
}


/*
 * ------------------------------------------------------------
 * Coordinate conversion
 * ------------------------------------------------------------
 */

gui_rect_t gui_widget_absolute_bounds(
    const gui_widget_t *widget)
{
    gui_rect_t result;

    result.x = 0;
    result.y = 0;

    result.width = 0u;
    result.height = 0u;

    if (widget == NULL)
        return result;

    int64_t absolute_x =
        widget->bounds.x;

    int64_t absolute_y =
        widget->bounds.y;

    const gui_widget_t *parent =
        widget->parent;

    while (parent != NULL)
    {
        absolute_x +=
            parent->bounds.x;

        absolute_y +=
            parent->bounds.y;

        parent =
            parent->parent;
    }

    /*
     * GUI coordinates are int32_t throughout the current renderer.
     *
     * Widget trees should never approach these limits during normal
     * operation. Clamp only to prevent signed overflow from malformed
     * geometry.
     */
    if (absolute_x <
        INT32_MIN)
    {
        absolute_x =
            INT32_MIN;
    }
    else if (absolute_x >
             INT32_MAX)
    {
        absolute_x =
            INT32_MAX;
    }

    if (absolute_y <
        INT32_MIN)
    {
        absolute_y =
            INT32_MIN;
    }
    else if (absolute_y >
             INT32_MAX)
    {
        absolute_y =
            INT32_MAX;
    }

    result.x =
        (int32_t)absolute_x;

    result.y =
        (int32_t)absolute_y;

    result.width =
        widget->bounds.width;

    result.height =
        widget->bounds.height;

    return result;
}


bool gui_widget_contains_point(
    const gui_widget_t *widget,
    int32_t x,
    int32_t y)
{
    if (widget == NULL)
        return false;

    return
        gui_widget_point_in_rect(
            gui_widget_absolute_bounds(
                widget),
            x,
            y);
}


/*
 * ------------------------------------------------------------
 * Visible clipping
 * ------------------------------------------------------------
 */

static bool gui_widget_visible_bounds(
    const gui_widget_t *widget,
    gui_rect_t initial_clip,
    gui_rect_t *result)
{
    if (widget == NULL ||
        result == NULL ||
        !widget->visible)
    {
        return false;
    }

    gui_rect_t visible =
        initial_clip;

    const gui_widget_t *current =
        widget;

    /*
     * Intersect against the widget itself and every parent.
     */
    while (current != NULL)
    {
        gui_rect_t bounds =
            gui_widget_absolute_bounds(
                current);

        gui_rect_t clipped;

        if (!gui_rect_intersect(
                visible,
                bounds,
                &clipped))
        {
            return false;
        }

        visible =
            clipped;

        current =
            current->parent;
    }

    *result =
        visible;

    return true;
}


/*
 * ------------------------------------------------------------
 * Hit testing
 * ------------------------------------------------------------
 */

static gui_widget_t *gui_widget_hit_test_internal(
    gui_widget_t *widget,
    gui_rect_t clip,
    int32_t x,
    int32_t y)
{
    if (widget == NULL ||
        !widget->visible ||
        !widget->enabled)
    {
        return NULL;
    }

    gui_rect_t bounds =
        gui_widget_absolute_bounds(
            widget);

    gui_rect_t visible;

    if (!gui_rect_intersect(
            clip,
            bounds,
            &visible))
    {
        return NULL;
    }

    if (!gui_widget_point_in_rect(
            visible,
            x,
            y))
    {
        return NULL;
    }

    /*
     * Last child renders last and therefore sits visually on top.
     *
     * Hit testing follows the reverse order.
     */
    gui_widget_t *child =
        widget->last_child;

    while (child != NULL)
    {
        gui_widget_t *hit =
            gui_widget_hit_test_internal(
                child,
                visible,
                x,
                y);

        if (hit != NULL)
            return hit;

        child =
            child->previous_sibling;
    }

    return widget;
}


gui_widget_t *gui_widget_hit_test(
    gui_widget_t *root,
    int32_t x,
    int32_t y)
{
    if (root == NULL ||
        !root->visible ||
        !root->enabled)
    {
        return NULL;
    }

    gui_rect_t root_bounds =
        gui_widget_absolute_bounds(
            root);

    return
        gui_widget_hit_test_internal(
            root,
            root_bounds,
            x,
            y);
}


/*
 * ------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------
 */

static bool gui_widget_render_internal(
    gui_widget_t *widget,
    gui_surface_t *destination,
    gui_rect_t parent_clip)
{
    if (widget == NULL ||
        destination == NULL ||
        destination->pixels == NULL)
    {
        return false;
    }

    if (!widget->visible)
        return true;

    gui_rect_t bounds =
        gui_widget_absolute_bounds(
            widget);

    gui_rect_t clip;

    if (!gui_rect_intersect(
            parent_clip,
            bounds,
            &clip))
    {
        /*
         * Completely outside its parent/surface.
         *
         * This is not a rendering failure.
         */
        return true;
    }

    if (widget->operations != NULL &&
        widget->operations->render != NULL)
    {
        if (!widget->operations->render(
                widget,
                destination,
                bounds,
                clip))
        {
            return false;
        }
    }

    gui_widget_t *child =
        widget->first_child;

    while (child != NULL)
    {
        if (!gui_widget_render_internal(
                child,
                destination,
                clip))
        {
            return false;
        }

        child =
            child->next_sibling;
    }

    return true;
}


bool gui_widget_render_tree(
    gui_widget_t *root,
    gui_surface_t *destination)
{
    if (root == NULL ||
        destination == NULL ||
        destination->pixels == NULL)
    {
        return false;
    }

    gui_rect_t surface_bounds =
        gui_widget_surface_bounds(
            destination);

    if (gui_rect_is_empty(
            surface_bounds))
    {
        return false;
    }

    return
        gui_widget_render_internal(
            root,
            destination,
            surface_bounds);
}