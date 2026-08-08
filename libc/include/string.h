#ifndef _STRING_H
#define _STRING_H 1

#include <sys/cdefs.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int memcmp(const void*, const void*, size_t);
void* memcpy(void* __restrict, const void* __restrict, size_t);
void* memmove(void*, const void*, size_t);
void* memset(void*, int, size_t);

size_t strlen(const char*);
char *strchr(const char *string, int character);
char *strcpy(char *restrict destination, const char *restrict source);
char *strncpy(char *restrict destination, const char *restrict source, size_t count);
char *strcat(char *restrict destination, const char *restrict source);
int strcmp(const char *left, const char *right);

#ifdef __cplusplus
}
#endif

#endif
