.section .text

.global arch_context_switch
.type arch_context_switch, @function

/*
 * void arch_context_switch(
 *     uintptr_t* old_stack_pointer,
 *     uintptr_t new_stack_pointer
 * );
 *
 * cdecl:
 *
 * entry:
 *
 *     0(%esp)   return address
 *     4(%esp)   &old_stack_pointer
 *     8(%esp)   new_stack_pointer
 */
arch_context_switch:

    /*
     * Save CPU state that must survive a normal function call.
     *
     * Also preserve EFLAGS so a newly-created thread can begin with
     * interrupts enabled.
     */
    pushfl

    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi

    /*
     * We pushed 20 bytes.
     *
     * Original arg1 was at old ESP + 4.
     *
     * Therefore it's now:
     *
     *     20 + 4 = 24(%esp)
     */
    movl 24(%esp), %eax

    /*
     * Save current ESP into:
     *
     *     current->stack_pointer
     */
    movl %esp, (%eax)

    /*
     * arg2:
     *
     *     old ESP + 8
     *     => 28(%esp)
     */
    movl 28(%esp),  %eax

    /*
     * Switch stacks.
     */
    movl %eax, %esp

    /*
     * Restore next task.
     */
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp

    popfl

    /*
     * Existing task:
     *     returns to wherever arch_context_switch() was called.
     *
     * New task:
     *     returns into task_bootstrap().
     */
    ret

.size arch_context_switch, . - arch_context_switch