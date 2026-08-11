#ifndef KERNEL_SLEEP_QUEUE_H
#define KERNEL_SLEEP_QUEUE_H

#include <stdint.h>

/*
 * Initialize the global kernel sleep queue.
 *
 * BSP initialization only.
 */
void sleep_queue_initialize(void);

/*
 * Put the current task to sleep for at least the
 * requested number of milliseconds.
 */
void task_sleep(uint64_t milliseconds);

/*
 * Called by the timer subsystem once per timer tick.
 *
 * Wakes every task whose deadline has expired.
 */
void sleep_queue_tick(uint64_t current_tick);

#endif