.section .text
.code32


.global arch_enter_user
.type arch_enter_user, @function

/*
 * void arch_enter_user(
 *     uintptr_t instruction_pointer,
 *     uintptr_t stack_pointer
 * );
 *
 * cdecl:
 *
 *     4(%esp) = user EIP
 *     8(%esp) = user ESP
 *
 * Does not return.
 */
arch_enter_user:
    movl 4(%esp), %eax
    movl 8(%esp), %edx

    /*
     * User data selectors may be loaded into DS/ES/FS/GS while
     * running at CPL0.  SS is supplied separately in the IRET frame.
     */
    movw $0x23, %cx

    movw %cx, %ds
    movw %cx, %es
    movw %cx, %fs
    movw %cx, %gs

    /*
     * IRET privilege-change frame, in reverse pop order:
     *
     *     SS
     *     ESP
     *     EFLAGS
     *     CS
     *     EIP
     */
    pushl $0x23
    pushl %edx

    /*
     * Reserved bit 1 set, IF set.
     * IOPL remains zero.
     */
    pushl $0x202

    pushl $0x1B
    pushl %eax

    iret

.size arch_enter_user, . - arch_enter_user


/*
 * Keep the U1 userspace probe on its own page.
 *
 * The linker currently collects .text exactly, rather than .text.*,
 * so keeping this in .text avoids changing linker.ld.
 */
.balign 4096

.global u1_user_test_entry
.type u1_user_test_entry, @function

u1_user_test_entry:
    /*
     * Use two locations in the already-mapped writable user stack.
     */
    movl $0x00401FE0, %edi

    /*
     * Write "MATE" at 0x401fe0.
     */
    movb $'M', 0(%edi)
    movb $'A', 1(%edi)
    movb $'T', 2(%edi)
    movb $'E', 3(%edi)

    /*
     * ----------------------------------------------------------
     * U2d test 1:
     * invalid user address must be rejected, not fault the kernel.
     *
     * EBX = invalid input
     * ECX = valid output
     * EDX = length
     * ----------------------------------------------------------
     */
    movl $3,          %eax
    movl $0xFFFFF000, %ebx
    movl $0x00401FF0, %ecx
    movl $4,          %edx
    int $0x80

    cmpl $-3, %eax
    jne 1f

    /*
     * ----------------------------------------------------------
     * U2d test 2:
     * valid copy from/to userspace.
     * ----------------------------------------------------------
     */
    movl $3,          %eax
    movl $0x00401FE0, %ebx
    movl $0x00401FF0, %ecx
    movl $4,          %edx
    int $0x80

    cmpl $4, %eax
    jne 1f

    /*
     * Verify the kernel copied "OKAY" back into our user page.
     */
    movl $0x00401FF0, %esi

    cmpb $'O', 0(%esi)
    jne 1f

    cmpb $'K', 1(%esi)
    jne 1f

    cmpb $'A', 2(%esi)
    jne 1f

    cmpb $'Y', 3(%esi)
    jne 1f

    /*
     * Leave EAX=4 for the success trap.
     */
    int3

1:
    ud2

2:
    jmp 2b

.size u1_user_test_entry, . - u1_user_test_entry

/*
 * Prevent the following input .text section from sharing this page.
 */
.balign 4096