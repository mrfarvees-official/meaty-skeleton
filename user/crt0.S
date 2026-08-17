.section .text
.code32

.global _start
.type _start, @function

_start:
	xorl %ebp, %ebp

	/*
	 * Initial U10 process stack:
	 *
	 *     0(%esp) = argc
	 *     4(%esp) = argv[0]
	 *     8(%esp) = argv[1]
	 *     ...
	 *
	 * main uses normal i386 cdecl:
	 *
	 *     int main(int argc, char **argv)
	 */
	movl (%esp), %eax
	leal 4(%esp), %edx

	pushl %edx
	pushl %eax

	call main

	addl $8, %esp

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