#include <stdint.h>

#include <kernel/panic.h>

#if UINTPTR_MAX == UINT32_MAX
#define STACK_CHK_GUARD_VALUE UINT32_C(0xE2DEE396)
#elif UINTPTR_MAX == UINT64_MAX
#define STACK_CHK_GUARD_VALUE UINT64_C(0x595E9FBD94FDA766)
#else
#error "Unsupported uintptr_t size"
#endif

uintptr_t __stack_chk_guard = (uintptr_t)STACK_CHK_GUARD_VALUE;

__attribute__((noreturn, no_stack_protector))
void __stack_chk_fail(void)
{
    kernel_panic("Stack smashing detected");
}