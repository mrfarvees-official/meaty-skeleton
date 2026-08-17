.section .text
.code32

.global _start
.type _start, @function

_start:
	xorl %ebp, %ebp

	call main

	/*
	 * main() return value becomes exit status.
	 *
	 * syscall 5 = exit
	 * EBX       = status
	 */
	movl %eax, %ebx
	movl $5, %eax
	int $0x80

	/*
	 * exit must never return.
	 */
	ud2

.size _start, . - _start