#include <stdio.h>

static FILE __stdin = {
	.fd = 0,
	.flags = _IO_READ,
	.pushback = 0,
	.has_pushback = 0,
};

static FILE __stdout = {
	.fd = 1,
	.flags = _IO_WRITE,
	.pushback = 0,
	.has_pushback = 0,
};

static FILE __stderr = {
	.fd = 2,
	.flags = _IO_WRITE,
	.pushback = 0,
	.has_pushback = 0,
};

FILE *stdin  = &__stdin;
FILE *stdout = &__stdout;
FILE *stderr = &__stderr;