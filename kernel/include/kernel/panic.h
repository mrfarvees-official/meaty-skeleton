#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((noreturn))
void kernel_panic(const char *message);

#ifdef __cplusplus
}
#endif

#endif