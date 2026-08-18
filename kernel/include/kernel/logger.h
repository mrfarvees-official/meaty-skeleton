#ifndef _KERNEL_LOGGER_H
#define _KERNEL_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Public colors are semantic logger colors.
 *
 * Users of the logger never need to know VGA numeric color indexes.
 * Conversion to VGA colors happens inside logger.c.
 */
typedef enum log_color
{
	LOG_COLOR_DEFAULT,

	LOG_COLOR_BLACK,
	LOG_COLOR_BLUE,
	LOG_COLOR_GREEN,
	LOG_COLOR_CYAN,
	LOG_COLOR_RED,
	LOG_COLOR_MAGENTA,
	LOG_COLOR_BROWN,
	LOG_COLOR_LIGHT_GREY,
	LOG_COLOR_DARK_GREY,
	LOG_COLOR_LIGHT_BLUE,
	LOG_COLOR_LIGHT_GREEN,
	LOG_COLOR_LIGHT_CYAN,
	LOG_COLOR_LIGHT_RED,
	LOG_COLOR_LIGHT_MAGENTA,
	LOG_COLOR_YELLOW,
	LOG_COLOR_WHITE
} log_color_t;


/*
 * Represents one active styled log message.
 *
 * Fields are internal.
 * Callers should not read or modify them.
 */
typedef struct log_message
{
	uint8_t _saved_terminal_color;
	uint8_t _message_terminal_color;
	bool _active;
} log_message_t;


/*
 * ------------------------------------------------------------
 * Simple one-shot loggers
 * ------------------------------------------------------------
 *
 * Use these when the whole message uses the normal message color.
 *
 * Examples:
 *
 *     log_success("PMM initialized");
 *     log_error("Failed to mount %s", path);
 *     log_warning("Disk %u is slow", disk);
 *     log_info("Found %u CPUs", cpu_count);
 *
 * Custom:
 *
 *     log_custom(
 *         "PCI",
 *         LOG_COLOR_LIGHT_MAGENTA,
 *         "Found device %x:%x",
 *         vendor,
 *         device);
 */
void log_success(const char *format, ...);
void log_error(const char *format, ...);
void log_warning(const char *format, ...);
void log_info(const char *format, ...);

void log_custom(
	const char *label,
	log_color_t label_color,
	const char *format,
	...);


/*
 * ------------------------------------------------------------
 * Styled message loggers
 * ------------------------------------------------------------
 *
 * Use these when different portions of the same message need
 * different colors or highlights.
 */
void log_success_begin(log_message_t *log);
void log_error_begin(log_message_t *log);
void log_warning_begin(log_message_t *log);
void log_info_begin(log_message_t *log);

void log_custom_begin(
	log_message_t *log,
	const char *label,
	log_color_t label_color);


/*
 * Normal message portion.
 *
 * Supports normal printf formatting.
 */
void log_text(
	log_message_t *log,
	const char *format,
	...);


/*
 * Colored foreground portion.
 *
 * Background remains black.
 */
void log_color(
	log_message_t *log,
	log_color_t color,
	const char *format,
	...);


/*
 * Default highlighted portion.
 *
 * Uses black foreground with yellow background.
 */
void log_highlight(
	log_message_t *log,
	const char *format,
	...);


/*
 * Fully configurable highlighted portion.
 *
 * Example:
 *
 *     log_highlight_color(
 *         &log,
 *         LOG_COLOR_BLACK,
 *         LOG_COLOR_LIGHT_GREEN,
 *         "READY");
 */
void log_highlight_color(
	log_message_t *log,
	log_color_t foreground,
	log_color_t background,
	const char *format,
	...);


/*
 * Ends the current log line and restores the terminal color
 * that was active before the log began.
 */
void log_end(log_message_t *log);

#endif