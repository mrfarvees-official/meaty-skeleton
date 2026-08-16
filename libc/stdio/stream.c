#include <stdio.h>

static FILE __stdin = {
	.fd = 0,
	.flags = _IO_READ,
	.pushback = 0,
	.has_pushback = 0,
	.write_buffer = NULL,
	.write_buffer_size = 0,
	.write_buffer_used = 0,
	.buffering_mode = _IONBF,
};

static FILE __stdout = {
	.fd = 1,
	.flags = _IO_WRITE,
	.pushback = 0,
	.has_pushback = 0,
	.write_buffer = NULL,
	.write_buffer_size = 0,
	.write_buffer_used = 0,
	.buffering_mode = _IONBF,
};

static FILE __stderr = {
	.fd = 2,
	.flags = _IO_WRITE,
	.pushback = 0,
	.has_pushback = 0,
	.write_buffer = NULL,
	.write_buffer_size = 0,
	.write_buffer_used = 0,
	.buffering_mode = _IONBF,
};

FILE *stdin  = &__stdin;
FILE *stdout = &__stdout;
FILE *stderr = &__stderr;