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
     * Build "Hello from ring 3!\n" in the existing user stack page.
     *
     * Buffer address: 0x00401FC0
     */
    movl $0x00401FC0, %edi

    movb $'H',  0(%edi)
    movb $'e',  1(%edi)
    movb $'l',  2(%edi)
    movb $'l',  3(%edi)
    movb $'o',  4(%edi)
    movb $' ',  5(%edi)
    movb $'f',  6(%edi)
    movb $'r',  7(%edi)
    movb $'o',  8(%edi)
    movb $'m',  9(%edi)
    movb $' ', 10(%edi)
    movb $'r', 11(%edi)
    movb $'i', 12(%edi)
    movb $'n', 13(%edi)
    movb $'g', 14(%edi)
    movb $' ', 15(%edi)
    movb $'3', 16(%edi)
    movb $'!', 17(%edi)
    movb $'\n', 18(%edi)

    /*
     * ----------------------------------------------------------
     * U2e test 1:
     * invalid pointer must still be rejected.
     * ----------------------------------------------------------
     *
     * syscall 4 = debug_write
     * EBX = user buffer
     * ECX = length
     */
    movl $4,          %eax
    movl $0xFFFFF000, %ebx
    movl $4,          %ecx
    int $0x80

    cmpl $-3, %eax
    jne 1f

    /*
     * ----------------------------------------------------------
     * U2e test 2:
     * oversized write must be rejected.
     * ----------------------------------------------------------
     */
    movl $4,          %eax
    movl $0x00401FC0, %ebx
    movl $129,        %ecx
    int $0x80

    cmpl $-4, %eax
    jne 1f

    /*
     * ----------------------------------------------------------
     * U2e test 3:
     * real userspace console output.
     * ----------------------------------------------------------
     */
    movl $4,          %eax
    movl $0x00401FC0, %ebx
    movl $19,         %ecx
    int $0x80

    cmpl $19, %eax
    jne 1f

    /*
     * Success trap.
     *
     * Preserve EAX=19 for kernel-side verification.
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