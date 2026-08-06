#include <stdio.h>
#include <kernel/halt.h>
#include <kernel/panic.h>

__attribute__((noreturn))
void kernel_panic(const char *message)
{
    printf("\n*** KERNEL PANIC ***\n");
    printf("%s\n", message);

    arch_halt();
}