#ifndef KERNEL_MOUSE_H
#define KERNEL_MOUSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef enum mouse_button
{
    MOUSE_BUTTON_LEFT = 0,
    MOUSE_BUTTON_RIGHT,
    MOUSE_BUTTON_MIDDLE,

    MOUSE_BUTTON_COUNT

} mouse_button_t;


typedef enum mouse_event_type
{
    MOUSE_EVENT_MOVE = 0,

    MOUSE_EVENT_BUTTON_DOWN,
    MOUSE_EVENT_BUTTON_UP

} mouse_event_type_t;


#define MOUSE_BUTTON_MASK_LEFT   (1u << 0)
#define MOUSE_BUTTON_MASK_RIGHT  (1u << 1)
#define MOUSE_BUTTON_MASK_MIDDLE (1u << 2)


typedef struct mouse_event
{
    mouse_event_type_t type;

    /*
     * Relative movement for this event.
     *
     * Positive X = right.
     * Positive Y = down, matching framebuffer coordinates.
     */
    int32_t dx;
    int32_t dy;

    /*
     * Valid for BUTTON_DOWN / BUTTON_UP.
     */
    mouse_button_t button;

    /*
     * Complete button state after the packet was processed.
     */
    uint8_t buttons;

} mouse_event_t;


/*
 * Initialize the PS/2 auxiliary device and IRQ12.
 *
 * Call after PIC/interrupt initialization and before global
 * interrupt_enable().
 */
bool mouse_initialize(void);

bool mouse_is_initialized(void);


/*
 * Non-blocking event reader.
 */
bool mouse_read_event(
    mouse_event_t *event);


/*
 * Number of queued events.
 */
size_t mouse_pending_events(void);


/*
 * Number of events lost because the queue was full.
 */
uint32_t mouse_dropped_events(void);


/*
 * Current physical button state.
 */
uint8_t mouse_get_buttons(void);

bool mouse_button_is_down(
    mouse_button_t button);


/*
 * Diagnostic counters.
 */
uint32_t mouse_packet_count(void);
uint32_t mouse_bad_packet_count(void);


#endif