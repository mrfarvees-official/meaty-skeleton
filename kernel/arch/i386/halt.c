#include <kernel/halt.h>

__attribute__((noreturn))
void arch_halt(void)
{
    __asm__ volatile("cli" ::: "memory");

    for (;;) {
        __asm__ volatile("hlt");
    }
}