#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/tty.h>
#include <kernel/multiboot.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>

#include "../arch/i386/gdt.h"
#include "../arch/i386/idt.h"
#include "../arch/i386/interrupts.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u

static void halt_forever(void)
{
	for(;;)
		__asm__ volatile ("cli; hlt");
}

// static void test_page_fault(void)
// {
//     printf("Triggering intentional page fault...\n");

//     volatile uint32_t* invalid_address =
//         (volatile uint32_t*)0xD0000000u;

//     *invalid_address = 0xDEADBEEFu;

//     /*
//      * This line must never execute because the page-fault handler
//      * should halt the kernel.
//      */
//     printf("ERROR: page fault did not occur\n");
// }

static void validate_multiboot_magic(uint32_t magic)
{
	if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
	{
		printf("Invalid multiboot magic: 0x%x\n", magic);
		halt_forever();
	}
}
void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_address)
{
	terminal_initialize();

	validate_multiboot_magic(multiboot_magic);

	// test success
	// printf("Hello, kernel World!\n");
	// printf("String: %s\n", "kernel");
	// printf("Character: %c\n", 'A');
	// printf("Signed: %d\n", -123);
	// printf("Unsigned: %u\n", 123U);
	// printf("Octal: %o\n", 10U);
	// printf("Hex: %x\n", 255U);
	// printf("HEX: %X\n", 255U);
	// printf("Pointer: %p\n", (void *)0x1234);
	// printf("Width: |%10d|\n", 42);
	// printf("Left: |%-10d|\n", 42);
	// printf("Zero: |%010d|\n", -42);
	// printf("Prefix: %#x\n", 255U);
	// printf("Percent: 100%%\n");
	// printf("Limited string: %.3s\n", "Hello");
	// printf("zero:       %Lf\n", 0.0L);
	// printf("negative:   %Lf\n", -123.456L);
	// printf("small e:    %Le\n", 0.000012345L);
	// printf("large e:    %Le\n", 123456789.0L);
	// printf("g fixed:    %.5Lg\n", 12.34567L);
	// printf("g exponent: %.5Lg\n", 1234567.0L);
	// printf("hex:        %La\n", 12.375L);
	// printf("uppercase:  %LA\n", 12.375L);
	// printf("width:      |%20.4Lf|\n", 12.375L);
	// printf("left:       |%-20.4Lf|\n", 12.375L);
	// printf("zero pad:   |%020.4Lf|\n", -12.375L);
	// printf("sign:       |%+.4Lf|\n", 12.375L);
	// printf("alternate:  |%#.0Lf|\n", 12.0L);

	// test success
	// const struct multiboot_info* mbi = (const struct multiboot_info*)multiboot_info_address;
	// printf("Multiboot magic: 0x%x\n", multiboot_magic);
	// printf("Multiboot information at: 0x%x\n", multiboot_info_address);
	// print_memory_map(mbi);

	pmm_initialize(multiboot_info_address);
	printf("pmm initialized\n");

	// test success
	// uintptr_t a = pmm_allocate_frame();
	// uintptr_t b = pmm_allocate_frame();
	// printf("frame a: 0x%x\n", a);
	// printf("frame b: 0x%x\n", b);
	// if (a == 0 || b == 0 || a == b)
	// {
	// 	printf("PMM test failed\n");
	// 	halt_forever();
	// }
	// pmm_free_frame(a);
	// uintptr_t c = pmm_allocate_frame();
	// printf("frame c: 0x%x\n", c);
	// printf("PMM test passed\n");

	gdt_initialize();
	printf("gdt initialized\n");

	idt_initialize();
	printf("idt initialized\n");

	interrupt_initialization();
	printf("interrupts initialized\n");

	// test success
	// __asm__ volatile ("int $3");
	// printf("int3 returned successfully\n");

    paging_initialize();
	printf("paging initialized\n");

	// test success
	// test_page_fault();

	halt_forever();
}
