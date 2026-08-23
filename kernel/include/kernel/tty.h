#ifndef _KERNEL_TTY_H
#define _KERNEL_TTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Initialize the legacy VGA text backend.
 *
 * This remains the first terminal backend used during very early boot,
 * before paging and framebuffer initialization are available.
 */
void terminal_initialize(void);

/*
 * Switch the existing terminal API to the framebuffer backend.
 *
 * All existing printf(), logger, syscall stdout/stderr and userspace
 * shell output continue using terminal_write(); only the display
 * backend changes.
 */
bool terminal_enable_framebuffer(void);

void terminal_setcolor(uint8_t color);
uint8_t terminal_getcolor(void);

void terminal_putchar(char character);

void terminal_write(
    const char *data,
    size_t size);

void terminal_writestring(
    const char *data);

#endif