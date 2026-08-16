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
     * Success means this instruction executes at CPL3 and reaches
     * the kernel through IDT vector 3.
     */
    int3

1:
    jmp 1b

.size u1_user_test_entry, . - u1_user_test_entry

/*
 * Prevent the following input .text section from sharing this page.
 */
.balign 4096