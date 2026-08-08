#include <stdint.h>

#include <kernel/timer.h>
#include <kernel/sleep_queue.h>
#include <kernel/scheduler.h>

#include "../arch/i386/interrupts.h"

static volatile uint64_t system_ticks = 0;
static uint32_t system_timer_frequency = 0;

void timer_initialize(uint32_t frequency_hz)
{
    uint32_t flags = interrupt_save_disable();

    system_ticks = 0;
    system_timer_frequency = frequency_hz;

    interrupt_restore(flags);
}

void timer_handle_tick(void)
{
    ++system_ticks;

    /*
     * Wake expired sleeping tasks first.
     */
    sleep_queue_tick(system_ticks);

    /*
     * Then update scheduling accounting.
     */
    scheduler_tick();
}

uint64_t timer_ticks(void)
{
    /*
     * We're on 32-bit x86.
     *
     * Reading a 64-bit variable is not inherently atomic.
     * IRQ0 could otherwise modify system_ticks between the two
     * 32-bit halves of the read.
     */
    uint32_t flags = interrupt_save_disable();

    uint64_t ticks = system_ticks;

    interrupt_restore(flags);

    return ticks;
}

uint32_t timer_frequency(void)
{
    /*
     * Frequency is configured once during boot for now, so a
     * simple read is sufficient.
     */
    return system_timer_frequency;
}

uint64_t timer_uptime_ms(void)
{
    uint32_t frequency = timer_frequency();

    if (frequency == 0) return 0;

    uint64_t ticks = timer_ticks();

    /*
     * Don't simply do:
     *
     *     ticks * 1000 / frequency
     *
     * because ticks * 1000 will overflow sooner.
     */
    uint64_t seconds = ticks / frequency;

    uint64_t remainder = ticks % frequency;

    return seconds * 1000u + (remainder * 1000u) / frequency;
}

uint64_t timer_ms_to_ticks(uint64_t milliseconds)
{
    uint32_t frequency = timer_frequency();

    if (frequency == 0) return 0;

    /*
     * Split the calculation to delay overflow.
     */
    uint64_t seconds = milliseconds / 1000u;

    uint64_t remainder_ms = milliseconds % 1000u;

    uint64_t ticks = seconds * frequency;

    /*
     * Round the fractional part UP.
     *
     * For example, if the kernel timer is 100 Hz:
     *
     *     1 ms  -> 1 tick
     *     10 ms -> 1 tick
     *     11 ms -> 2 ticks
     *
     * This is appropriate for deadlines because a requested
     * delay should not expire earlier merely because the timer
     * resolution is coarse.
     */
    ticks  += (remainder_ms * frequency + 999u) / 1000u;

    return ticks;
}