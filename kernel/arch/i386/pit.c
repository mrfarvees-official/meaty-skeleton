#include <stdint.h>

#include "interrupts.h"
#include "io.h"
#include "pic.h"
#include "pit.h"

#include <kernel/timer.h>

static uint32_t configured_frequency_hz = 0;

static void pit_irq_handler(struct interrupt_frame* frame)
{
    (void)frame;

    /*
     * PIT owns the hardware event.
     *
     * Generic timer code owns kernel timekeeping.
     */
    timer_handle_tick();

    /*
     * DO NOT send PIC EOI here.
     *
     * Your generic interrupt_dispatch() should do that after
     * the device IRQ handler returns.
     */
}

static uint32_t pit_calculate_reload(uint32_t requested_frequency_hz)
{
    if (requested_frequency_hz == 0) return 0;

    /*
     * PIT output frequency:
     *
     *     output = input / reload
     *
     * Round the requested frequency to the nearest available
     * integer reload value.
     */
    uint32_t reload = (PIT_INPUT_FREQUENCY_HZ + requested_frequency_hz  / 2u) / requested_frequency_hz;

    /*
     * Avoid reload 1 for mode 2.
     */
    if (reload < 2u) reload = 2u;

    /*
     * PIT channel counters are 16-bit.
     *
     * The encoded value 0 represents 65536.
     */
    if (reload > 65536u) reload = 65536u;

    return reload;
}

static uint32_t pit_frequency_from_reload(uint32_t reload)
{
    if (reload == 0) return 0;

    /*
     * Integer result is sufficient for our initial generic
     * timer abstraction.
     */
    return PIT_INPUT_FREQUENCY_HZ / reload;
}

static void pit_program_reload(uint32_t reload)
{
    uint16_t encoded_reload;

    /*
     * PIT treats zero as 65536.
     */
    if (reload == 65536u) encoded_reload = 0;
    else encoded_reload = (uint16_t)reload;

    uint8_t command = PIT_COMMAND_CHANNEL0 | PIT_COMMAND_LOHI | PIT_COMMAND_MODE2 | PIT_COMMAND_BINARY;

    /*
     * Configure channel 0:
     *
     * channel 0
     * low byte + high byte
     * mode 2
     * binary
     *
     * This evaluates to 0x34.
     */
    outb(PIT_COMMAND_PORT, command);

    /*
     * Low reload byte first.
     */
    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)(encoded_reload & 0xFFu));

    /*
     * High reload byte second.
     */
    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)((encoded_reload >> 8) & 0xFFu));
}

int pit_initialize(uint32_t frequency_hz)
{
    if (frequency_hz == 0) return -1;

    uint32_t reload = pit_calculate_reload(frequency_hz);

    if (reload == 0) return -1;

    configured_frequency_hz = pit_frequency_from_reload(reload);

    /*
     * Register IRQ0 handler BEFORE unmasking IRQ0.
     */
    if (interrupt_register_handler(pic_irq_to_vector(PIT_IRQ), pit_irq_handler) != 0)
    {
        configured_frequency_hz = 0;
        return -1;
    }

    /*
     * Tell the architecture-independent timer subsystem what
     * frequency it will actually receive.
     */
    timer_initialize(configured_frequency_hz);

    /*
     * Program PIT channel 0.
     */
    pit_program_reload(reload);

    /*
     * Everything is now ready to receive IRQ0.
     *
     * Global CPU interrupts may still be disabled at this point,
     * which is desirable during boot.
     */
    pic_unmask(PIT_IRQ);

    return 0;
}

uint32_t pit_frequency(void)
{
    return configured_frequency_hz;
}