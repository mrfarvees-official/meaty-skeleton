#include <stdio.h>

static FILE __stdin = {
	.fd = 0,
	.flags = _IO_READ,
};

static FILE __stdout = {
	.fd = 1,
	.flags = _IO_WRITE,
};

static FILE __stderr = {
	.fd = 2,
	.flags = _IO_WRITE,
};

FILE *stdin  = &__stdin;
FILE *stdout = &__stdout;
FILE *stderr = &__stderr;