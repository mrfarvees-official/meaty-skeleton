#ifndef _STDIO_H
#define _STDIO_H 1

#include <sys/cdefs.h>
#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

#define _IO_READ   (1U << 0)
#define _IO_WRITE  (1U << 1)
#define _IO_EOF    (1U << 2)
#define _IO_ERROR  (1U << 3)

typedef struct FILE {
	int fd;
	unsigned int flags;

	int pushback;
	int has_pushback;
} FILE;

#ifdef __cplusplus
extern "C" {
#endif

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int fgetc(FILE *stream);
int getchar(void);
int ungetc(int c, FILE *stream);

char *fgets(char *str, int size, FILE *stream);

int fputc(int, FILE *);
int putchar(int);

size_t fread(
	void *ptr,
	size_t size,
	size_t nmemb,
	FILE *stream);

size_t fwrite(
	const void *ptr,
	size_t size,
	size_t nmemb,
	FILE *stream);

int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

int fputs(const char *, FILE *);
int puts(const char *);

int vfscanf(FILE *, const char *, va_list);
int fscanf(FILE *, const char *, ...);
int scanf(const char *, ...);

int vfprintf(FILE *, const char *, va_list);
int fprintf(FILE *, const char *, ...);

int vprintf(const char *, va_list);
int printf(const char* __restrict, ...);

#ifdef __cplusplus
}
#endif

#endif
