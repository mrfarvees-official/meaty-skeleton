# Declare constants for the multiboot header.
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set FLAGS,    ALIGN | MEMINFO  # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot

# Declare a header as in the Multiboot Standard.
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

# Storage for GRUB-provided Multiboot values.
.section .bss
.align 4

multiboot_magic:
.skip 4

multiboot_info:
.skip 4

# Initial kernel stack.
.align 16

stack_bottom:
.skip 16384 # 16 KiB
stack_top:

# The kernel entry point.
.section .text
.code32

.global _start
.type _start, @function

.extern gdt_initialize
.extern kernel_main
.extern _init
.extern _fini

_start:
	/*
	 * GRUB enters here:
	 *
	 * EAX = 0x2BADBOOT
	 * EBX = physical address of the Multiboot information structure
	 */
	movl %eax, multiboot_magic
	movl %ebx, multiboot_info
	
	/*
	 * GRUB already placed the CPU in 32-bit protected mode.
	 * We only need to establish our stack and our own GDT.
	 */
	movl $stack_top, %esp

	/*
	 * Keep the stack aligned according to the i386 System V ABI.
	 */
	andl $-16, %esp

	/*
	 * Install the kernel-owned GDT.
	 */
	call gdt_initialize

	/*
	 * Invoke global constructors, as in the original
	 * Meaty Skeleton boot.S.
	 */
	call _init

	/*
	 * Call:
	 *
	 * kernel_main(magic, multiboot_info_address)
	 * 
	 * The i386 cdecl convention pushes arguments from right to left
	 */
	pushl multiboot_info
	pushl multiboot_magic

	/*
	 * Call the C kernel entry point.
	 */
	call kernel_main

	/*
	 * Remove the two 32-bit arguments
	 */
	addl $8, %esp

	/*
	 * Invoke global destructors if kernel_main returns.
	 */
	call _fini

	# Hang if kernel_main unexpectedly returns.
	cli

1:
	hlt
	jmp 1b

.size _start, . - _start
