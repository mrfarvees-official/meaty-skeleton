#ifndef KERNEL_ARCH_I386_PIT_H
#define KERNEL_ARCH_I386_PIT_H

#include <stdint.h>

/*
 * 100 Hz:
 *
 * 1 timer tick ~= 10 ms.
 *
 * This is a reasonable bring-up frequency and will later give
 * your existing 5-tick round-robin quantum approximately 50 ms.
 */
#define PIT_DEFAULT_FREQUENCY_HZ    100u

/*
 * PIT input clock.
 *
 * The traditional PIT frequency is approximately:
 *
 *     1.193182 MHz
 */
#define PIT_INPUT_FREQUENCY_HZ      1193182u

/*
 * Channel 0 data register.
 *
 * Channel 0 drives IRQ0 on the traditional PC architecture.
 */
#define PIT_CHANNEL0_DATA_PORT      0x40u

/*
 * PIT mode/command register.
 */
#define PIT_COMMAND_PORT            0x43u

/*
 * PIT is connected to legacy PIC IRQ0.
 */
#define PIT_IRQ                     0u

/*
 * Command register:
 *
 * bits 7-6 = 00
 *     select channel 0
 */
#define PIT_COMMAND_CHANNEL0        0x00u

/*
 * bits 5-4 = 11
 *     write low byte followed by high byte
 */
#define PIT_COMMAND_LOHI            0x30u

/*
 * bits 3-1 = 010
 *     mode 2: rate generator
 */
#define PIT_COMMAND_MODE2           0x04u

/*
 * bit 0 = 0
 *     binary counter
 */
#define PIT_COMMAND_BINARY          0x00u

/*
 * Initialize PIT channel 0 as the system periodic timer.
 *
 * Returns:
 *   0  success
 *  -1  invalid configuration / handler registration failed
 */
int pit_initialize(uint32_t frequency_hz);

/*
 * Actual frequency generated after integer PIT divisor rounding.
 */
uint32_t pit_frequency(void);

#endif