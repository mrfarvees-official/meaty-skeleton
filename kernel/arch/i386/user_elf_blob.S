.section .rodata
.balign 16

.global u8_hello_elf_start
.global u8_hello_elf_end

.type u8_hello_elf_start, @object

u8_hello_elf_start:
	.incbin "../user/hello.elf"

u8_hello_elf_end:

.size u8_hello_elf_start, u8_hello_elf_end - u8_hello_elf_start