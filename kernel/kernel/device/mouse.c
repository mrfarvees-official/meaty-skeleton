#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/mouse.h>

#include "../../arch/i386/interrupts.h"
#include "../../arch/i386/io.h"
#include "../../arch/i386/pic.h"


/*
 * ------------------------------------------------------------
 * i8042 / PS/2 controller
 * ------------------------------------------------------------
 */

#define I8042_DATA_PORT    0x60u
#define I8042_STATUS_PORT  0x64u
#define I8042_COMMAND_PORT 0x64u


/*
 * Status register.
 */
#define I8042_STATUS_OUTPUT_FULL 0x01u
#define I8042_STATUS_INPUT_FULL  0x02u
#define I8042_STATUS_AUX_DATA    0x20u


/*
 * Controller commands.
 */
#define I8042_COMMAND_READ_CONFIG       0x20u
#define I8042_COMMAND_WRITE_CONFIG      0x60u
#define I8042_COMMAND_ENABLE_AUXILIARY  0xA8u
#define I8042_COMMAND_WRITE_AUXILIARY   0xD4u


/*
 * Controller configuration byte.
 */
#define I8042_CONFIG_AUX_IRQ_ENABLE 0x02u
#define I8042_CONFIG_AUX_DISABLE    0x20u


/*
 * PS/2 mouse commands.
 */
#define MOUSE_COMMAND_SET_DEFAULTS      0xF6u
#define MOUSE_COMMAND_ENABLE_REPORTING  0xF4u


/*
 * PS/2 acknowledgement.
 */
#define MOUSE_RESPONSE_ACK 0xFAu


#define MOUSE_IRQ 12u


/*
 * Large enough for normal emulators/machines, but finite so a broken
 * controller cannot hang kernel boot forever.
 */
#define I8042_TIMEOUT 100000u


/*
 * ------------------------------------------------------------
 * Mouse packet format
 * ------------------------------------------------------------
 *
 * Byte 0:
 *
 * bit 0 left
 * bit 1 right
 * bit 2 middle
 * bit 3 always 1
 * bit 4 X sign
 * bit 5 Y sign
 * bit 6 X overflow
 * bit 7 Y overflow
 */

#define MOUSE_PACKET_ALWAYS_ONE 0x08u

#define MOUSE_PACKET_LEFT       0x01u
#define MOUSE_PACKET_RIGHT      0x02u
#define MOUSE_PACKET_MIDDLE     0x04u

#define MOUSE_PACKET_X_OVERFLOW 0x40u
#define MOUSE_PACKET_Y_OVERFLOW 0x80u


/*
 * ------------------------------------------------------------
 * Event ring
 * ------------------------------------------------------------
 */

#define MOUSE_EVENT_BUFFER_SIZE 128u


_Static_assert(
    (MOUSE_EVENT_BUFFER_SIZE &
     (MOUSE_EVENT_BUFFER_SIZE - 1u)) == 0u,
    "mouse event buffer size must be a power of two");


static mouse_event_t
    mouse_event_buffer[
        MOUSE_EVENT_BUFFER_SIZE];

static volatile size_t
    mouse_event_read_index;

static volatile size_t
    mouse_event_write_index;

static volatile uint32_t
    mouse_event_drop_count;


/*
 * ------------------------------------------------------------
 * Driver state
 * ------------------------------------------------------------
 */

static bool mouse_initialized;

static uint8_t mouse_packet[3];
static uint8_t mouse_packet_index;

static volatile uint8_t
    mouse_buttons;

static volatile uint32_t
    mouse_packets_received;

static volatile uint32_t
    mouse_bad_packets;


/*
 * ------------------------------------------------------------
 * Ring helpers
 * ------------------------------------------------------------
 */

static size_t mouse_event_next_index(
    size_t index)
{
    return
        (index + 1u) &
        (MOUSE_EVENT_BUFFER_SIZE - 1u);
}


static void mouse_event_push(
    const mouse_event_t *event)
{
    if (event == NULL)
        return;

    size_t write_index =
        mouse_event_write_index;

    size_t next =
        mouse_event_next_index(
            write_index);

    if (next ==
        mouse_event_read_index)
    {
        ++mouse_event_drop_count;
        return;
    }

    mouse_event_buffer[
        write_index] =
            *event;

    mouse_event_write_index =
        next;
}


/*
 * ------------------------------------------------------------
 * i8042 polling
 * ------------------------------------------------------------
 */

static bool i8042_wait_input_empty(void)
{
    for (uint32_t attempt = 0u;
         attempt < I8042_TIMEOUT;
         ++attempt)
    {
        uint8_t status =
            inb(
                I8042_STATUS_PORT);

        if ((status &
             I8042_STATUS_INPUT_FULL) == 0u)
        {
            return true;
        }

        __asm__ volatile("pause");
    }

    return false;
}


static bool i8042_wait_output_full(void)
{
    for (uint32_t attempt = 0u;
         attempt < I8042_TIMEOUT;
         ++attempt)
    {
        uint8_t status =
            inb(
                I8042_STATUS_PORT);

        if ((status &
             I8042_STATUS_OUTPUT_FULL) != 0u)
        {
            return true;
        }

        __asm__ volatile("pause");
    }

    return false;
}


/*
 * Wait specifically for an auxiliary-device byte.
 *
 * Initialization occurs before global interrupts are enabled, so we
 * can synchronously receive mouse command responses here.
 */
static bool i8042_read_auxiliary(
    uint8_t *value)
{
    if (value == NULL)
        return false;

    for (uint32_t attempt = 0u;
         attempt < I8042_TIMEOUT;
         ++attempt)
    {
        uint8_t status =
            inb(
                I8042_STATUS_PORT);

        if ((status &
             I8042_STATUS_OUTPUT_FULL) == 0u)
        {
            __asm__ volatile("pause");
            continue;
        }

        /*
         * A byte exists but it belongs to the keyboard.
         *
         * During mouse initialization keyboard input is not active
         * yet, so drain it instead of allowing stale controller data
         * to block the AUX response indefinitely.
         */
        if ((status &
             I8042_STATUS_AUX_DATA) == 0u)
        {
            (void)inb(
                I8042_DATA_PORT);

            continue;
        }

        *value =
            inb(
                I8042_DATA_PORT);

        return true;
    }

    return false;
}


static bool i8042_write_command(
    uint8_t command)
{
    if (!i8042_wait_input_empty())
        return false;

    outb(
        I8042_COMMAND_PORT,
        command);

    return true;
}


static bool i8042_write_data(
    uint8_t value)
{
    if (!i8042_wait_input_empty())
        return false;

    outb(
        I8042_DATA_PORT,
        value);

    return true;
}


/*
 * ------------------------------------------------------------
 * Controller configuration
 * ------------------------------------------------------------
 */

static void i8042_flush_output(void)
{
    /*
     * Drain anything already pending.
     *
     * This runs before normal keyboard/mouse IRQ operation begins.
     */
    for (uint32_t attempt = 0u;
         attempt < 256u;
         ++attempt)
    {
        uint8_t status =
            inb(
                I8042_STATUS_PORT);

        if ((status &
             I8042_STATUS_OUTPUT_FULL) == 0u)
        {
            break;
        }

        (void)inb(
            I8042_DATA_PORT);
    }
}


static bool i8042_enable_auxiliary_port(void)
{
    if (!i8042_write_command(
            I8042_COMMAND_ENABLE_AUXILIARY))
    {
        return false;
    }

    /*
     * Read controller configuration byte.
     */
    if (!i8042_write_command(
            I8042_COMMAND_READ_CONFIG))
    {
        return false;
    }

    if (!i8042_wait_output_full())
        return false;

    uint8_t configuration =
        inb(
            I8042_DATA_PORT);

    /*
     * Enable IRQ12 and make sure the auxiliary clock is enabled.
     */
    configuration |=
        I8042_CONFIG_AUX_IRQ_ENABLE;

    configuration &=
        (uint8_t)
        ~I8042_CONFIG_AUX_DISABLE;

    if (!i8042_write_command(
            I8042_COMMAND_WRITE_CONFIG))
    {
        return false;
    }

    if (!i8042_write_data(
            configuration))
    {
        return false;
    }

    return true;
}


/*
 * ------------------------------------------------------------
 * Mouse command transport
 * ------------------------------------------------------------
 */

static bool mouse_send_command(
    uint8_t command)
{
    /*
     * 0xD4 tells the controller that the next data byte is destined
     * for the auxiliary PS/2 device rather than the keyboard.
     */
    if (!i8042_write_command(
            I8042_COMMAND_WRITE_AUXILIARY))
    {
        return false;
    }

    if (!i8042_write_data(
            command))
    {
        return false;
    }

    uint8_t response = 0u;

    if (!i8042_read_auxiliary(
            &response))
    {
        return false;
    }

    return response ==
        MOUSE_RESPONSE_ACK;
}


/*
 * ------------------------------------------------------------
 * Packet decoder
 * ------------------------------------------------------------
 */

static void mouse_publish_button_change(
    mouse_button_t button,
    bool pressed,
    uint8_t new_buttons)
{
    mouse_event_t event;

    event.type =
        pressed
            ? MOUSE_EVENT_BUTTON_DOWN
            : MOUSE_EVENT_BUTTON_UP;

    event.dx = 0;
    event.dy = 0;

    event.button =
        button;

    event.buttons =
        new_buttons;

    mouse_event_push(
        &event);
}


static void mouse_decode_packet(void)
{
    uint8_t flags =
        mouse_packet[0];

    /*
     * Reject movement overflow.
     *
     * These packets do not contain a reliable full displacement.
     */
    if ((flags &
         (MOUSE_PACKET_X_OVERFLOW |
          MOUSE_PACKET_Y_OVERFLOW)) != 0u)
    {
        ++mouse_bad_packets;
        return;
    }

    int32_t dx =
        (int32_t)
        (int8_t)
        mouse_packet[1];

    /*
     * Native PS/2 positive Y means upward.
     *
     * Framebuffer coordinates grow downward, so invert it here.
     */
    int32_t dy =
        -(int32_t)
        (int8_t)
        mouse_packet[2];

    uint8_t new_buttons = 0u;

    if ((flags &
         MOUSE_PACKET_LEFT) != 0u)
    {
        new_buttons |=
            MOUSE_BUTTON_MASK_LEFT;
    }

    if ((flags &
         MOUSE_PACKET_RIGHT) != 0u)
    {
        new_buttons |=
            MOUSE_BUTTON_MASK_RIGHT;
    }

    if ((flags &
         MOUSE_PACKET_MIDDLE) != 0u)
    {
        new_buttons |=
            MOUSE_BUTTON_MASK_MIDDLE;
    }

    uint8_t old_buttons =
        mouse_buttons;

    mouse_buttons =
        new_buttons;

    ++mouse_packets_received;


    /*
     * Publish movement.
     */
    if (dx != 0 ||
        dy != 0)
    {
        mouse_event_t event;

        event.type =
            MOUSE_EVENT_MOVE;

        event.dx =
            dx;

        event.dy =
            dy;

        event.button =
            MOUSE_BUTTON_LEFT;

        event.buttons =
            new_buttons;

        mouse_event_push(
            &event);
    }


    /*
     * Publish physical button transitions separately.
     */

    uint8_t changed =
        old_buttons ^
        new_buttons;

    if ((changed &
         MOUSE_BUTTON_MASK_LEFT) != 0u)
    {
        mouse_publish_button_change(
            MOUSE_BUTTON_LEFT,
            (new_buttons &
             MOUSE_BUTTON_MASK_LEFT) != 0u,
            new_buttons);
    }

    if ((changed &
         MOUSE_BUTTON_MASK_RIGHT) != 0u)
    {
        mouse_publish_button_change(
            MOUSE_BUTTON_RIGHT,
            (new_buttons &
             MOUSE_BUTTON_MASK_RIGHT) != 0u,
            new_buttons);
    }

    if ((changed &
         MOUSE_BUTTON_MASK_MIDDLE) != 0u)
    {
        mouse_publish_button_change(
            MOUSE_BUTTON_MIDDLE,
            (new_buttons &
             MOUSE_BUTTON_MASK_MIDDLE) != 0u,
            new_buttons);
    }
}


/*
 * ------------------------------------------------------------
 * IRQ12
 * ------------------------------------------------------------
 */

static void mouse_interrupt_handler(
    struct interrupt_frame *frame)
{
    (void)frame;

    uint8_t status =
        inb(
            I8042_STATUS_PORT);

    /*
     * IRQ12 should correspond to auxiliary data, but verify both the
     * output-buffer-full and AUX bits before touching port 0x60.
     *
     * This avoids intentionally consuming keyboard bytes.
     */
    if ((status &
         I8042_STATUS_OUTPUT_FULL) == 0u)
    {
        return;
    }

    if ((status &
         I8042_STATUS_AUX_DATA) == 0u)
    {
        return;
    }

    uint8_t data =
        inb(
            I8042_DATA_PORT);


    /*
     * First packet byte must always have bit 3 set.
     *
     * This also gives us packet-stream resynchronization if a byte is
     * ever lost.
     */
    if (mouse_packet_index == 0u)
    {
        if ((data &
             MOUSE_PACKET_ALWAYS_ONE) == 0u)
        {
            ++mouse_bad_packets;
            return;
        }
    }

    mouse_packet[
        mouse_packet_index] =
            data;

    ++mouse_packet_index;

    if (mouse_packet_index < 3u)
        return;

    mouse_packet_index =
        0u;

    mouse_decode_packet();


    /*
     * No printf.
     * No framebuffer drawing.
     * No allocation.
     * No task blocking.
     *
     * Generic interrupt dispatch performs the PIC EOI.
     */
}


/*
 * ------------------------------------------------------------
 * Initialization
 * ------------------------------------------------------------
 */

bool mouse_initialize(void)
{
    if (mouse_initialized)
        return true;

    uint32_t flags =
        interrupt_save_disable();


    /*
     * Start with IRQ12 masked while the controller/device is
     * configured.
     */
    pic_mask(
        MOUSE_IRQ);


    mouse_event_read_index =
        0u;

    mouse_event_write_index =
        0u;

    mouse_event_drop_count =
        0u;

    mouse_packet_index =
        0u;

    mouse_buttons =
        0u;

    mouse_packets_received =
        0u;

    mouse_bad_packets =
        0u;


    /*
     * Ensure initialization starts with an empty controller output
     * buffer.
     */
    i8042_flush_output();


    if (!i8042_enable_auxiliary_port())
    {
        interrupt_restore(
            flags);

        return false;
    }


    /*
     * Install IRQ12 before enabling mouse reporting.
     */
    uint8_t vector =
        pic_irq_to_vector(
            MOUSE_IRQ);

    if (interrupt_register_handler(
            vector,
            mouse_interrupt_handler) != 0)
    {
        interrupt_restore(
            flags);

        return false;
    }


    /*
     * Restore standard PS/2 defaults.
     */
    if (!mouse_send_command(
            MOUSE_COMMAND_SET_DEFAULTS))
    {
        interrupt_unregister_handler(
            vector);

        interrupt_restore(
            flags);

        return false;
    }


    /*
     * Begin streaming movement/button packets.
     *
     * IF is still disabled here, so data will not actually enter the
     * IRQ handler until kernel_main enables interrupts later.
     */
    if (!mouse_send_command(
            MOUSE_COMMAND_ENABLE_REPORTING))
    {
        interrupt_unregister_handler(
            vector);

        interrupt_restore(
            flags);

        return false;
    }


    /*
     * pic_unmask() automatically unmasks master IRQ2 because IRQ12 is
     * on the slave PIC.
     */
    pic_unmask(
        MOUSE_IRQ);


    mouse_initialized =
        true;


    interrupt_restore(
        flags);

    return true;
}


/*
 * ------------------------------------------------------------
 * Public event API
 * ------------------------------------------------------------
 */

bool mouse_is_initialized(void)
{
    return mouse_initialized;
}


bool mouse_read_event(
    mouse_event_t *event)
{
    if (event == NULL)
        return false;

    uint32_t flags =
        interrupt_save_disable();

    if (mouse_event_read_index ==
        mouse_event_write_index)
    {
        interrupt_restore(
            flags);

        return false;
    }

    *event =
        mouse_event_buffer[
            mouse_event_read_index];

    mouse_event_read_index =
        mouse_event_next_index(
            mouse_event_read_index);

    interrupt_restore(
        flags);

    return true;
}


size_t mouse_pending_events(void)
{
    uint32_t flags =
        interrupt_save_disable();

    size_t read_index =
        mouse_event_read_index;

    size_t write_index =
        mouse_event_write_index;

    size_t count;

    if (write_index >=
        read_index)
    {
        count =
            write_index -
            read_index;
    }
    else
    {
        count =
            MOUSE_EVENT_BUFFER_SIZE -
            read_index +
            write_index;
    }

    interrupt_restore(
        flags);

    return count;
}


uint32_t mouse_dropped_events(void)
{
    uint32_t flags =
        interrupt_save_disable();

    uint32_t result =
        mouse_event_drop_count;

    interrupt_restore(
        flags);

    return result;
}


uint8_t mouse_get_buttons(void)
{
    uint32_t flags =
        interrupt_save_disable();

    uint8_t result =
        mouse_buttons;

    interrupt_restore(
        flags);

    return result;
}


bool mouse_button_is_down(
    mouse_button_t button)
{
    uint8_t mask;

    switch (button)
    {
        case MOUSE_BUTTON_LEFT:
            mask =
                MOUSE_BUTTON_MASK_LEFT;
            break;

        case MOUSE_BUTTON_RIGHT:
            mask =
                MOUSE_BUTTON_MASK_RIGHT;
            break;

        case MOUSE_BUTTON_MIDDLE:
            mask =
                MOUSE_BUTTON_MASK_MIDDLE;
            break;

        default:
            return false;
    }

    return
        (mouse_get_buttons() &
         mask) != 0u;
}


uint32_t mouse_packet_count(void)
{
    uint32_t flags =
        interrupt_save_disable();

    uint32_t result =
        mouse_packets_received;

    interrupt_restore(
        flags);

    return result;
}


uint32_t mouse_bad_packet_count(void)
{
    uint32_t flags =
        interrupt_save_disable();

    uint32_t result =
        mouse_bad_packets;

    interrupt_restore(
        flags);

    return result;
}