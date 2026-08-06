#include <stdlib.h>

#if defined(__is_libk)
#include <kernel/panic.h>
#endif

__attribute__((noreturn))
void abort(void)
{
#if defined(__is_libk)
    kernel_panic("abort()");
#else
    /*
     * Later:
     * raise(SIGABRT);
     * terminate the current process.
     */
    for (;;) {
    }
#endif
}