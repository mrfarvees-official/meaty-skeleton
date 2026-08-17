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

int main(void)
{
	static const char message[] =
		"Hello from ELF userspace!\n";

	int32_t result =
		syscall2(
			SYS_DEBUG_WRITE,
			(uint32_t)(uintptr_t)message,
			(uint32_t)(sizeof(message) - 1u));

	if (result !=
		(int32_t)(sizeof(message) - 1u))
	{
		return 1;
	}

	return 0;
}