#ifndef KERNEL_ARCH_I386_SYSCALL_H
#define KERNEL_ARCH_I386_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#define I386_SYSCALL_VECTOR       0x80u

/*
 * i386 syscall register ABI:
 *
 *     EAX = syscall number
 *
 *     EBX = argument 0
 *     ECX = argument 1
 *     EDX = argument 2
 *     ESI = argument 3
 *     EDI = argument 4
 *
 *     EAX = return value
 */
#define I386_SYSCALL_TEST_SIMPLE      0u
#define I386_SYSCALL_TEST_ARGUMENTS   1u

#define I386_SYSCALL_TEST_RESULT      0xC0DEFACEu
#define I386_SYSCALL_ARGUMENT_RESULT  0x11223355u

bool syscall_initialize(void);

#endif