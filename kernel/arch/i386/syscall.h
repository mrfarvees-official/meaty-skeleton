#ifndef KERNEL_ARCH_I386_SYSCALL_H
#define KERNEL_ARCH_I386_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#define I386_SYSCALL_VECTOR       0x80u

/*
 * U2a temporary test ABI.
 */
#define I386_SYSCALL_TEST         0u
#define I386_SYSCALL_TEST_RESULT  0xC0DEFACEu

bool syscall_initialize(void);

#endif