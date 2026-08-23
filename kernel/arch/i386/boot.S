# Declare constants for the Multiboot header.
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory information
.set VIDEO,    1<<2             # request graphics framebuffer information
.set FLAGS,    ALIGN | MEMINFO | VIDEO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

# Declare a Multiboot v1 header.
.section .multiboot
.align 4

.long MAGIC
.long FLAGS
.long CHECKSUM

#
# Address fields occupy offsets 12..28 in the Multiboot header.
#
# We are not using MULTIBOOT_AOUT_KLUDGE (flag bit 16), so these
# remain zero. They must still occupy their normal positions before
# the graphics fields.
#
.long 0                        # header_addr
.long 0                        # load_addr
.long 0                        # load_end_addr
.long 0                        # bss_end_addr
.long 0                        # entry_addr

#
# Graphics request.
#
# mode_type = 0:
#     linear graphics mode
#
# GRUB treats width/height/depth as preferences, so the kernel must
# always inspect the actual mode returned in the Multiboot info.
#
.long 0                        # mode_type = linear graphics
.long 1024                     # preferred width
.long 768                      # preferred height
.long 32                       # preferred bits per pixel

# Storage for GRUB-provided Multiboot values.
.section .bss
.align 4

multiboot_magic:
.skip 4

multiboot_info:
.skip 4

# Initial bootstrap kernel stack.
.align 16

stack_bottom:
.skip 16384                    # 16 KiB bootstrap stack
stack_top:

# Kernel entry point.
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
	 * EAX = 0x2BADB002
	 * EBX = physical address of the Multiboot information structure
	 */
	movl %eax, multiboot_magic
	movl %ebx, multiboot_info

	/*
	 * GRUB already placed the CPU in 32-bit protected mode.
	 * Establish our stack and GDT.
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
	 * Invoke global constructors.
	 */
	call _init

	/*
	 * kernel_main(magic, multiboot_info_address)
	 *
	 * i386 cdecl pushes arguments from right to left.
	 */
	pushl multiboot_info
	pushl multiboot_magic

	call kernel_main

	/*
	 * Remove the two 32-bit arguments.
	 */
	addl $8, %esp

	/*
	 * Invoke global destructors if kernel_main returns.
	 */
	call _fini

	cli

1:
	hlt
	jmp 1b

.size _start, . - _start