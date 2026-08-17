#include <stddef.h>
#include <stdint.h>

#define SYS_DEBUG_WRITE 4u

static int32_t syscall2(
	uint32_t number,
	uint32_t arg0,
	uint32_t arg1)
{
	uint32_t result =
		number;

	__asm__ volatile(
		"int $0x80"
		: "+a"(result)
		: "b"(arg0),
		  "c"(arg1)
		: "memory", "cc");

	return (int32_t)result;
}

static size_t string_length(
	const char *string)
{
	size_t length = 0;

	while (string[length] != '\0')
		++length;

	return length;
}

static int write_string(
	const char *string)
{
	size_t length =
		string_length(string);

	int32_t result =
		syscall2(
			SYS_DEBUG_WRITE,
			(uint32_t)(uintptr_t)string,
			(uint32_t)length);

	return result ==
			(int32_t)length
		? 0
		: -1;
}

int main(
	int argc,
	char **argv)
{
	static const char greeting[] =
		"Hello from ELF userspace!\n";

	static const char argc_ok[] =
		"U10 user: argc=3\n";

	static const char argv0_prefix[] =
		"U10 user: argv[0]=";

	static const char argv1_prefix[] =
		"U10 user: argv[1]=";

	static const char argv2_prefix[] =
		"U10 user: argv[2]=";

	static const char newline[] =
		"\n";

	if (write_string(greeting) != 0)
		return 1;

	if (argc != 3 ||
		argv == NULL ||
		argv[0] == NULL ||
		argv[1] == NULL ||
		argv[2] == NULL ||
		argv[3] != NULL)
	{
		return 2;
	}

	if (write_string(argc_ok) != 0)
		return 3;

	if (write_string(argv0_prefix) != 0 ||
		write_string(argv[0]) != 0 ||
		write_string(newline) != 0)
	{
		return 4;
	}

	if (write_string(argv1_prefix) != 0 ||
		write_string(argv[1]) != 0 ||
		write_string(newline) != 0)
	{
		return 5;
	}

	if (write_string(argv2_prefix) != 0 ||
		write_string(argv[2]) != 0 ||
		write_string(newline) != 0)
	{
		return 6;
	}

	return 0;
}