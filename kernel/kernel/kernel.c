#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/multiboot.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/heap.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/wait_queue.h>
#include <kernel/sleep_queue.h>
#include <kernel/acpi.h>
#include <kernel/smp.h>
#include <kernel/cpu.h>
#include <kernel/keyboard.h>
#include <kernel/vfs.h>
#include <kernel/ramfs.h>
#include <kernel/fd.h>
#include <kernel/ata.h>
#include <kernel/block_device.h>
#include <kernel/partition.h>
#include <kernel/ext2.h>
#include <kernel/pci.h>
#include <kernel/ahci.h>

#include <kernel/test.h>
#include <kernel/system_info.h>

#include "../arch/i386/gdt.h"
#include "../arch/i386/idt.h"
#include "../arch/i386/interrupts.h"
#include "../arch/i386/pic.h"
#include "../arch/i386/pit.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u

static void halt_forever(void)
{
	for (;;)
		__asm__ volatile("cli; hlt");
}

static void yield_forever(void)
{
	for (;;)
		task_yield();
}

void validate_multiboot_magic(uint32_t magic)
{
	if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
	{
		/* Not entered by a Multiboot-compliant bootloader */
		for (;;)
			__asm__ volatile("cli; hlt");
	}
}

#define U1_USER_CODE_ADDRESS 0x00400000u
#define U1_USER_STACK_ADDRESS 0x00401000u
#define U1_USER_STACK_TOP 0x00402000u

extern void arch_enter_user(
	uintptr_t instruction_pointer,
	uintptr_t stack_pointer)
	__attribute__((noreturn));

extern void u1_user_test_entry(void);

/*
 * One physical page that will be aliased into the user part of the
 * current bootstrap address space.
 */
static uint8_t u1_user_stack[PAGE_SIZE]
	__attribute__((aligned(PAGE_SIZE)));

/*
 * Temporary BSP-only privilege-transition stack.
 *
 * This is deliberately not pretending to be the final task kernel
 * stack solution.  The next U1 milestone can hook esp0 to scheduled
 * tasks and make the TSS per-CPU.
 */
static uint8_t u1_kernel_entry_stack[16u * 1024u]
	__attribute__((aligned(16)));

static void u1_ring3_test(void)
	__attribute__((noreturn));

static void u1_ring3_test(void)
{
	uintptr_t user_code_physical;
	uintptr_t user_stack_physical;

	if (((uintptr_t)u1_user_test_entry &
		 (PAGE_SIZE - 1u)) != 0)
	{
		printf("U1: user test code is not page aligned\n");
		halt_forever();
	}

	if (!paging_get_physical_address(
			(uintptr_t)u1_user_test_entry,
			&user_code_physical))
	{
		printf("U1: failed to resolve test-code physical page\n");
		halt_forever();
	}

	if (!paging_get_physical_address(
			(uintptr_t)u1_user_stack,
			&user_stack_physical))
	{
		printf("U1: failed to resolve test-stack physical page\n");
		halt_forever();
	}

	user_code_physical &=
		~(uintptr_t)(PAGE_SIZE - 1u);

	user_stack_physical &=
		~(uintptr_t)(PAGE_SIZE - 1u);

	if (!paging_map_page(
			U1_USER_CODE_ADDRESS,
			user_code_physical,
			PAGE_USER))
	{
		printf("U1: failed to map user code\n");
		halt_forever();
	}

	if (!paging_map_page(
			U1_USER_STACK_ADDRESS,
			user_stack_physical,
			PAGE_USER | PAGE_WRITABLE))
	{
		printf("U1: failed to map user stack\n");
		halt_forever();
	}

	uintptr_t kernel_stack_top =
		(uintptr_t)u1_kernel_entry_stack +
		sizeof(u1_kernel_entry_stack);

	kernel_stack_top &=
		~(uintptr_t)0xFu;

	gdt_set_kernel_stack(kernel_stack_top);

	printf(
		"U1: entering user mode eip=0x%lx esp=0x%lx\n",
		(unsigned long)U1_USER_CODE_ADDRESS,
		(unsigned long)U1_USER_STACK_TOP);

	arch_enter_user(
		U1_USER_CODE_ADDRESS,
		U1_USER_STACK_TOP);
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_address)
{
	terminal_initialize();

	validate_multiboot_magic(multiboot_magic);

	pmm_initialize(multiboot_info_address);
	printf("pmm initialized\n");

	gdt_initialize();
	printf("gdt initialized\n");

	idt_initialize();
	printf("idt initialized\n");

	interrupt_initialization();
	printf("interrupts initialized\n");

	pic_initialize();
	printf("pic initialized\n");

	paging_initialize();
	printf("paging initialized\n");

	/*
	 * U1a ends inside the breakpoint handler after proving a CPL3
	 * interrupt transition.
	 *
	 * Keep this before heap/SMP/device startup so only the BSP is
	 * involved in the first privilege-transition test.
	 */
	u1_ring3_test();

	heap_initialize();
	printf("heap initialized\n");

	if (!acpi_initialize())
	{
		printf("ACPI initialization failed\n");
		halt_forever();
	}

	if (!smp_detect_cpus())
	{
		printf("SMP CPU detection failed\n");
		halt_forever();
	}

	cpu_local_initialize();
	printf("BSP CPU-local state initialized\n");

	scheduler_initialize();
	printf("scheduler initialized\n");

	task_initialize();
	printf("BSP task system initialized\n");

	process_initialize();
	printf("process system initialized\n");

	sleep_queue_initialize();
	printf("sleep queue initialized\n");

	vfs_initialize();
	printf("VFS initialized\n");

	pci_device_t ahci_controller;

	if (pci_find_ahci_controller(&ahci_controller))
	{

		if (!ahci_probe(&ahci_controller))
		{
			printf("AHCI: probe failed\n");
		}
	}
	else
	{
		printf("AHCI: no controller detected\n");
	}

	block_device_t *disk =
		ahci_primary_disk();

	if (disk == NULL)
	{
		printf(
			"Storage: no AHCI disk\n");

		halt_forever();
	}

	static partition_device_t partitions[8];

	size_t partition_count =
		partition_scan(
			disk,
			partitions,
			8);

	if (partition_count == 0)
	{
		printf("Storage: no usable partitions\n");

		halt_forever();
	}
	else
	{
		block_device_t *partition =
			&partitions[0].block;

		uint8_t original[512];
		uint8_t test_data[512];
		uint8_t verify[512];

		/*
		 * Use a sector well inside the partition.
		 * We save and restore it immediately.
		 */
		const uint64_t test_lba = 100;

		if (block_read(
				partition,
				test_lba,
				1,
				original) != 0)
		{
			printf(
				"Partition: initial write-test read failed\n");

			halt_forever();
		}

		for (size_t i = 0;
			 i < sizeof(test_data);
			 i++)
		{
			test_data[i] =
				(uint8_t)((i * 29u) ^
						  0x5Au);
		}

		if (block_write(
				partition,
				test_lba,
				1,
				test_data) != 0)
		{
			printf(
				"Partition: write test failed\n");

			halt_forever();
		}

		memset(
			verify,
			0,
			sizeof(verify));

		if (block_read(
				partition,
				test_lba,
				1,
				verify) != 0)
		{
			printf(
				"Partition: verify read failed\n");

			/*
			 * Attempt restore before stopping.
			 */
			block_write(
				partition,
				test_lba,
				1,
				original);

			halt_forever();
		}

		if (memcmp(
				test_data,
				verify,
				sizeof(test_data)) != 0)
		{
			printf(
				"Partition: write verification FAILED\n");

			block_write(
				partition,
				test_lba,
				1,
				original);

			halt_forever();
		}

		if (block_write(
				partition,
				test_lba,
				1,
				original) != 0)
		{
			printf(
				"Partition: failed restoring test sector\n");

			halt_forever();
		}

		printf(
			"Partition: write/read test passed\n");
	}

	static ext2_fs_t ext2_fs;

	if (!ext2_mount(
			&partitions[0].block,
			&ext2_fs))
	{
		printf(
			"EXT2: mount failed\n");

		halt_forever();
	}

	if (!vfs_set_root(
			&ext2_fs.root_vnode))
	{
		printf(
			"EXT2: failed to set VFS root\n");

		halt_forever();
	}

	printf(
		"EXT2: mounted as /\n");

	if (!smp_start_aps())
	{
		printf("AP startup failed\n");
		halt_forever();
	}

	printf("SMP online CPUs: %u\n", (unsigned)smp_online_cpu_count());

	if (pit_initialize(PIT_DEFAULT_FREQUENCY_HZ) != 0)
	{
		printf("PIT initialization failed\n");
		halt_forever();
	}

	if (!keyboard_initialize())
	{
		printf("keyboard initialization FAILED\n");
		halt_forever();
	}

	/*
	 * PIT IRQ0 is now configured and unmasked.
	 *
	 * Allow maskable hardware interrupts.
	 */
	interrupt_enable();
	printf("hardware interrupts enabled\n");

	{
		printf(
			"\n=== setvbuf test ===\n");

		/*
		 * ==========================================================
		 * Full buffering with caller-owned buffer
		 * ==========================================================
		 */
		FILE *file =
			fopen(
				"/setvbuf-full.txt",
				"w");

		static char full_buffer[16];

		printf(
			"full open=%d\n",
			file != NULL);

		if (file != NULL)
		{
			int result =
				setvbuf(
					file,
					full_buffer,
					_IOFBF,
					sizeof(full_buffer));

			printf(
				"full setvbuf=%d\n",
				result);

			fwrite(
				"FULL",
				1,
				4,
				file);

			FILE *reader =
				fopen(
					"/setvbuf-full.txt",
					"r");

			if (reader != NULL)
			{
				char data[8] = {0};

				size_t read =
					fread(
						data,
						1,
						4,
						reader);

				printf(
					"full before flush=%u\n",
					(unsigned)read);

				fclose(reader);
			}

			fflush(file);

			reader =
				fopen(
					"/setvbuf-full.txt",
					"r");

			if (reader != NULL)
			{
				char data[8] = {0};

				size_t read =
					fread(
						data,
						1,
						4,
						reader);

				printf(
					"full after flush=%u data=%s\n",
					(unsigned)read,
					data);

				fclose(reader);
			}

			fclose(file);
		}

		/*
		 * ==========================================================
		 * Unbuffered
		 * ==========================================================
		 */
		file =
			fopen(
				"/setvbuf-none.txt",
				"w");

		if (file != NULL)
		{
			int result =
				setvbuf(
					file,
					NULL,
					_IONBF,
					0);

			printf(
				"none setvbuf=%d\n",
				result);

			fwrite(
				"NOW",
				1,
				3,
				file);

			FILE *reader =
				fopen(
					"/setvbuf-none.txt",
					"r");

			if (reader != NULL)
			{
				char data[4] = {0};

				size_t read =
					fread(
						data,
						1,
						3,
						reader);

				printf(
					"none immediate=%u data=%s\n",
					(unsigned)read,
					data);

				fclose(reader);
			}

			fclose(file);
		}

		/*
		 * ==========================================================
		 * Line buffering
		 * ==========================================================
		 */
		file =
			fopen(
				"/setvbuf-line.txt",
				"w");

		if (file != NULL)
		{
			int result =
				setvbuf(
					file,
					NULL,
					_IOLBF,
					16);

			printf(
				"line setvbuf=%d\n",
				result);

			fwrite(
				"LINE",
				1,
				4,
				file);

			FILE *reader =
				fopen(
					"/setvbuf-line.txt",
					"r");

			if (reader != NULL)
			{
				char data[8] = {0};

				size_t read =
					fread(
						data,
						1,
						5,
						reader);

				printf(
					"line before newline=%u\n",
					(unsigned)read);

				fclose(reader);
			}

			fputc(
				'\n',
				file);

			reader =
				fopen(
					"/setvbuf-line.txt",
					"r");

			if (reader != NULL)
			{
				char data[8] = {0};

				size_t read =
					fread(
						data,
						1,
						5,
						reader);

				printf(
					"line after newline=%u first=%c last=%u\n",
					(unsigned)read,
					data[0],
					(unsigned char)data[4]);

				fclose(reader);
			}

			fclose(file);
		}

		/*
		 * Read-only streams are deliberately unsupported in this
		 * output-buffering phase.
		 */
		file =
			fopen(
				"/setvbuf-full.txt",
				"r");

		if (file != NULL)
		{
			printf(
				"read-only setvbuf=%d\n",
				setvbuf(
					file,
					NULL,
					_IOFBF,
					16));

			fclose(file);
		}
	}

	yield_forever();
}
