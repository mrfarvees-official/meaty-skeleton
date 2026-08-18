#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>

#include "vga.h"

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static const size_t TAB_WIDTH = 4;

static uint16_t *const VGA_MEMORY = (uint16_t *)0xB8000;

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t *terminal_buffer;

static void terminal_clear_row(size_t row)
{
	for (size_t column = 0; column < VGA_WIDTH; column++)
	{
		const size_t index = row * VGA_WIDTH + column;
		terminal_buffer[index] = vga_entry(' ', terminal_color);
	}
}

static void terminal_scroll(void)
{
	for (size_t row = 1; row < VGA_HEIGHT; row++)
	{
		for (size_t column = 0; column < VGA_WIDTH; column++)
		{
			const size_t source = row * VGA_WIDTH + column;
			const size_t destination = (row - 1) * VGA_WIDTH + column;

			terminal_buffer[destination] = terminal_buffer[source];
		}
	}

	terminal_clear_row(VGA_HEIGHT - 1);
	terminal_row = VGA_HEIGHT - 1;
}

static void terminal_advance_row(void)
{
	terminal_column = 0;
	terminal_row++;

	if (terminal_row >= VGA_HEIGHT)
		terminal_scroll();
}

void terminal_initialize(void)
{
	terminal_row = 0;
	terminal_column = 0;
	terminal_color =
		vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	terminal_buffer = VGA_MEMORY;

	for (size_t row = 0; row < VGA_HEIGHT; row++)
		terminal_clear_row(row);
}

void terminal_setcolor(uint8_t color)
{
	terminal_color = color;
}

uint8_t terminal_getcolor(void)
{
	return terminal_color;
}

void terminal_putentryat(
	unsigned char character,
	uint8_t color,
	size_t x,
	size_t y)
{
	if (x >= VGA_WIDTH || y >= VGA_HEIGHT)
		return;

	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(character, color);
}

void terminal_putchar(char character)
{
	switch (character)
	{
	case '\a':
		/*
		 * Bell.
		 *
		 * VGA text mode has no built-in bell behavior.
		 * Later, this can call a PC speaker function.
		 */
		break;

	case '\b':
		/*
		 * Backspace: move back one position and erase it.
		 */
		if (terminal_column > 0)
		{
			terminal_column--;
		}
		else if (terminal_row > 0)
		{
			terminal_row--;
			terminal_column = VGA_WIDTH - 1;
		}
		else
		{
			break;
		}

		terminal_putentryat(
			' ',
			terminal_color,
			terminal_column,
			terminal_row);
		break;

	case '\t':
	{
		/*
		 * Move to the next tab stop.
		 */
		const size_t next_tab_stop =
			(terminal_column + TAB_WIDTH) & ~(TAB_WIDTH - 1);

		while (terminal_column < next_tab_stop)
			terminal_putchar(' ');

		break;
	}

	case '\n':
		/*
		 * New line.
		 */
		terminal_advance_row();
		break;

	case '\v':
		/*
		 * Vertical tab: move down one row without changing column.
		 */
		terminal_row++;

		if (terminal_row >= VGA_HEIGHT)
			terminal_scroll();

		break;

	case '\f':
		/*
		 * Form feed: clear the terminal.
		 */
		terminal_initialize();
		break;

	case '\r':
		/*
		 * Carriage return: move to the beginning of the current row.
		 */
		terminal_column = 0;
		break;

	default:
		terminal_putentryat(
			(unsigned char)character,
			terminal_color,
			terminal_column,
			terminal_row);

		terminal_column++;

		if (terminal_column >= VGA_WIDTH)
			terminal_advance_row();

		break;
	}
}

void terminal_write(const char *data, size_t size)
{
	for (size_t index = 0; index < size; index++)
		terminal_putchar(data[index]);
}

void terminal_writestring(const char *data)
{
	terminal_write(data, strlen(data));
}