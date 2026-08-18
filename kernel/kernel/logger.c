#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/logger.h>
#include <kernel/tty.h>

#include "../arch/i386/vga.h"


/*
 * Default visual styles.
 */
#define LOG_MESSAGE_FOREGROUND LOG_COLOR_LIGHT_GREY
#define LOG_MESSAGE_BACKGROUND LOG_COLOR_BLACK

#define LOG_HIGHLIGHT_FOREGROUND LOG_COLOR_BLACK
#define LOG_HIGHLIGHT_BACKGROUND LOG_COLOR_YELLOW


/*
 * ------------------------------------------------------------
 * Color conversion
 * ------------------------------------------------------------
 *
 * This is intentionally private.
 *
 * Public logger callers only deal with log_color_t.
 * VGA color values never leak into normal kernel code.
 */
static enum vga_color log_color_to_vga(log_color_t color)
{
	switch (color)
	{
	case LOG_COLOR_BLACK:
		return VGA_COLOR_BLACK;

	case LOG_COLOR_BLUE:
		return VGA_COLOR_BLUE;

	case LOG_COLOR_GREEN:
		return VGA_COLOR_GREEN;

	case LOG_COLOR_CYAN:
		return VGA_COLOR_CYAN;

	case LOG_COLOR_RED:
		return VGA_COLOR_RED;

	case LOG_COLOR_MAGENTA:
		return VGA_COLOR_MAGENTA;

	case LOG_COLOR_BROWN:
		return VGA_COLOR_BROWN;

	case LOG_COLOR_LIGHT_GREY:
		return VGA_COLOR_LIGHT_GREY;

	case LOG_COLOR_DARK_GREY:
		return VGA_COLOR_DARK_GREY;

	case LOG_COLOR_LIGHT_BLUE:
		return VGA_COLOR_LIGHT_BLUE;

	case LOG_COLOR_LIGHT_GREEN:
		return VGA_COLOR_LIGHT_GREEN;

	case LOG_COLOR_LIGHT_CYAN:
		return VGA_COLOR_LIGHT_CYAN;

	case LOG_COLOR_LIGHT_RED:
		return VGA_COLOR_LIGHT_RED;

	case LOG_COLOR_LIGHT_MAGENTA:
		return VGA_COLOR_LIGHT_MAGENTA;

	case LOG_COLOR_YELLOW:
		return VGA_COLOR_LIGHT_BROWN;

	case LOG_COLOR_WHITE:
		return VGA_COLOR_WHITE;

	case LOG_COLOR_DEFAULT:
	default:
		return VGA_COLOR_LIGHT_GREY;
	}
}


static uint8_t log_make_terminal_color(
	log_color_t foreground,
	log_color_t background)
{
	return vga_entry_color(
		log_color_to_vga(foreground),
		log_color_to_vga(background));
}


static uint8_t log_default_message_color(void)
{
	return log_make_terminal_color(
		LOG_MESSAGE_FOREGROUND,
		LOG_MESSAGE_BACKGROUND);
}


/*
 * ------------------------------------------------------------
 * Internal message handling
 * ------------------------------------------------------------
 */

static void log_begin_internal(
	log_message_t *log,
	const char *label,
	log_color_t label_color)
{
	if (log == NULL)
	{
		return;
	}

	log->_saved_terminal_color =
		terminal_getcolor();

	log->_message_terminal_color =
		log_default_message_color();

	log->_active = true;

	if (label == NULL)
	{
		label = "LOG";
	}

	/*
	 * Prefix.
	 */
	terminal_setcolor(
		log_make_terminal_color(
			label_color,
			LOG_COLOR_BLACK));

	printf("[%s] ", label);

	/*
	 * Switch back to the default message style.
	 */
	terminal_setcolor(
		log->_message_terminal_color);
}


static void log_vtext(
	log_message_t *log,
	const char *format,
	va_list arguments)
{
	if (log == NULL ||
		!log->_active ||
		format == NULL)
	{
		return;
	}

	terminal_setcolor(
		log->_message_terminal_color);

	vprintf(
		format,
		arguments);

	terminal_setcolor(
		log->_message_terminal_color);
}


static void log_vcolor(
	log_message_t *log,
	log_color_t foreground,
	log_color_t background,
	const char *format,
	va_list arguments)
{
	if (log == NULL ||
		!log->_active ||
		format == NULL)
	{
		return;
	}

	/*
	 * Apply style only to this portion.
	 */
	terminal_setcolor(
		log_make_terminal_color(
			foreground,
			background));

	vprintf(
		format,
		arguments);

	/*
	 * Automatically return to the normal message style.
	 *
	 * The caller never has to manually reset colors.
	 */
	terminal_setcolor(
		log->_message_terminal_color);
}


/*
 * ------------------------------------------------------------
 * Begin functions
 * ------------------------------------------------------------
 */

void log_success_begin(log_message_t *log)
{
	log_begin_internal(
		log,
		"SUCCESS",
		LOG_COLOR_LIGHT_GREEN);
}


void log_error_begin(log_message_t *log)
{
	log_begin_internal(
		log,
		"ERROR",
		LOG_COLOR_LIGHT_RED);
}


void log_warning_begin(log_message_t *log)
{
	log_begin_internal(
		log,
		"WARNING",
		LOG_COLOR_YELLOW);
}


void log_info_begin(log_message_t *log)
{
	log_begin_internal(
		log,
		"INFO",
		LOG_COLOR_LIGHT_CYAN);
}


void log_custom_begin(
	log_message_t *log,
	const char *label,
	log_color_t label_color)
{
	log_begin_internal(
		log,
		label,
		label_color);
}


/*
 * ------------------------------------------------------------
 * Message portions
 * ------------------------------------------------------------
 */

void log_text(
	log_message_t *log,
	const char *format,
	...)
{
	va_list arguments;

	va_start(
		arguments,
		format);

	log_vtext(
		log,
		format,
		arguments);

	va_end(arguments);
}


void log_color(
	log_message_t *log,
	log_color_t color,
	const char *format,
	...)
{
	if (log == NULL ||
		!log->_active ||
		format == NULL)
	{
		return;
	}

	/*
	 * LOG_COLOR_DEFAULT means normal message styling.
	 */
	if (color == LOG_COLOR_DEFAULT)
	{
		va_list arguments;

		va_start(
			arguments,
			format);

		log_vtext(
			log,
			format,
			arguments);

		va_end(arguments);

		return;
	}

	va_list arguments;

	va_start(
		arguments,
		format);

	log_vcolor(
		log,
		color,
		LOG_COLOR_BLACK,
		format,
		arguments);

	va_end(arguments);
}


void log_highlight(
	log_message_t *log,
	const char *format,
	...)
{
	va_list arguments;

	va_start(
		arguments,
		format);

	log_vcolor(
		log,
		LOG_HIGHLIGHT_FOREGROUND,
		LOG_HIGHLIGHT_BACKGROUND,
		format,
		arguments);

	va_end(arguments);
}


void log_highlight_color(
	log_message_t *log,
	log_color_t foreground,
	log_color_t background,
	const char *format,
	...)
{
	va_list arguments;

	va_start(
		arguments,
		format);

	log_vcolor(
		log,
		foreground,
		background,
		format,
		arguments);

	va_end(arguments);
}


/*
 * ------------------------------------------------------------
 * End
 * ------------------------------------------------------------
 */

void log_end(log_message_t *log)
{
	if (log == NULL ||
		!log->_active)
	{
		return;
	}

	/*
	 * Newline belongs to the normal message style.
	 */
	terminal_setcolor(
		log->_message_terminal_color);

	printf("\n");

	/*
	 * Restore whatever terminal color was active before the
	 * logger was entered.
	 */
	terminal_setcolor(
		log->_saved_terminal_color);

	log->_active = false;
}


/*
 * ------------------------------------------------------------
 * One-shot logger implementation
 * ------------------------------------------------------------
 */

static void log_vsimple(
	const char *label,
	log_color_t label_color,
	const char *format,
	va_list arguments)
{
	log_message_t log;

	log_begin_internal(
		&log,
		label,
		label_color);

	log_vtext(
		&log,
		format,
		arguments);

	// log_end(&log);
}


void log_success(
	const char *format,
	...)
{
	va_list arguments;

	va_start(
		arguments,
		format);

	log_vsimple(
		"SUCCESS",
		LOG_COLOR_LIGHT_GREEN,
		format,
		arguments);

	va_end(arguments);
}


void log_error(
	const char *format,
	...)
{
	va_list arguments;

	va_start(
		arguments,
		format);

	log_vsimple(
		"ERROR",
		LOG_COLOR_LIGHT_RED,
		format,
		arguments);

	va_end(arguments);
}


void log_warning(
	const char *format,
	...)
{
	va_list arguments;

	va_start(
		arguments,
		format);

	log_vsimple(
		"WARNING",
		LOG_COLOR_YELLOW,
		format,
		arguments);

	va_end(arguments);
}


void log_info(
	const char *format,
	...)
{
	va_list arguments;

	va_start(
		arguments,
		format);

	log_vsimple(
		"INFO",
		LOG_COLOR_LIGHT_CYAN,
		format,
		arguments);

	va_end(arguments);
}


void log_custom(
	const char *label,
	log_color_t label_color,
	const char *format,
	...)
{
	va_list arguments;

	va_start(
		arguments,
		format);

	log_vsimple(
		label,
		label_color,
		format,
		arguments);

	va_end(arguments);
}