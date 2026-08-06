#include <string.h>

char *strchr(const char *string, int character)
{
	const char target = (char)character;

	for (;;)
	{
		if (*string == target)
		{
			return (char *)string;
		}

		if (*string == '\0')
		{
			return NULL;
		}

		++string;
	}
}