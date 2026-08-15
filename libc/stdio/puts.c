#include <stdio.h>

int puts(const char *string) 
{
	if (fputs(string, stdout) == EOF) 
		return EOF;
		
	if (fputc('\n', stdout) == EOF)
		return EOF;

	return 0;
}
