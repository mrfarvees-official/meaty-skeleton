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
#include <kernel/elf.h>
#include <kernel/user_image.h>

#include "../arch/i386/gdt.h"
#include "../arch/i386/idt.h"
#include "../arch/i386/interrupts.h"
#include "../arch/i386/syscall.h"
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

#define U8_USER_STACK_ADDRESS 0xBFFFF000u
#define U8_USER_STACK_TOP 0xC0000000u

extern void arch_enter_user(
	uintptr_t instruction_pointer,
	uintptr_t stack_pointer)
	__attribute__((noreturn));

extern const unsigned char
	u8_hello_elf_start[];

extern const unsigned char
	u8_hello_elf_end[];

typedef struct
{
	uintptr_t entry;
	uintptr_t stack_top;
} user_launch_context_t;

static user_image_t u8_user_image;
static user_launch_context_t u8_launch_context;

static void u8_user_task_entry(void *argument)
	__attribute__((noreturn));

static void u8_user_task_entry(void *argument)
{
	user_launch_context_t *launch =
		(user_launch_context_t *)argument;

	if (launch == NULL ||
		launch->entry == 0 ||
		launch->stack_top == 0)
	{
		printf(
			"U8: invalid ELF launch context\n");

		halt_forever();
	}

	task_t *task =
		task_current();

	if (task == NULL)
	{
		printf(
			"U8: no current user task\n");

		halt_forever();
	}

	uintptr_t actual_directory =
		paging_current_directory();

	if (!task->owns_page_directory ||
		actual_directory !=
			task->page_directory ||
		actual_directory ==
			paging_kernel_directory())
	{
		printf(
			"U8: invalid user address space\n");

		halt_forever();
	}

	printf(
		"U8: entering ELF at 0x%lx stack=0x%lx\n",
		(unsigned long)
			launch->entry,
		(unsigned long)
			launch->stack_top);

	arch_enter_user(
		launch->entry,
		launch->stack_top);
}

static void u8_elf_test(void)
{
	printf(
		"\n=== U8 ELF USERSPACE TEST ===\n");

	uintptr_t kernel_directory =
		paging_kernel_directory();

	if (kernel_directory == 0 ||
		paging_current_directory() !=
			kernel_directory)
	{
		printf(
			"U8: kernel CR3 is not active\n");

		halt_forever();
	}

	if (u8_hello_elf_end <=
		u8_hello_elf_start)
	{
		printf(
			"U8: embedded ELF is empty\n");

		halt_forever();
	}

	size_t elf_size =
		(size_t)(u8_hello_elf_end -
				 u8_hello_elf_start);

	printf(
		"U8: embedded hello.elf size=%lu bytes\n",
		(unsigned long)elf_size);

	size_t live_before =
		task_live_count();

	uint64_t reaped_before =
		task_cleanup_total_reaped();

	if (!elf_load_user_image(
			&u8_user_image,
			u8_hello_elf_start,
			elf_size,
			U8_USER_STACK_ADDRESS,
			U8_USER_STACK_TOP))
	{
		printf(
			"U8: ELF loader rejected hello.elf\n");

		halt_forever();
	}

	printf(
		"U8: ELF loaded entry=0x%lx CR3=0x%lx\n",
		(unsigned long)
			u8_user_image.entry,
		(unsigned long)
			u8_user_image.page_directory);

	u8_launch_context.entry =
		u8_user_image.entry;

	u8_launch_context.stack_top =
		u8_user_image.stack_top;

	uintptr_t user_directory =
		u8_user_image.page_directory;

	task_t *task =
		task_create_user_with_policy(
			u8_user_task_entry,
			&u8_launch_context,
			user_directory,
			SCHED_POLICY_REALTIME);

	if (task == NULL)
	{
		user_image_destroy(
			&u8_user_image);

		printf(
			"U8: failed creating ELF user task\n");

		halt_forever();
	}

	if (!task->owns_page_directory ||
		task->page_directory !=
			user_directory)
	{
		printf(
			"U8: user task did not accept ELF CR3\n");

		halt_forever();
	}

	uintptr_t detached_directory =
		user_image_detach_directory(
			&u8_user_image);

	if (detached_directory !=
		user_directory)
	{
		printf(
			"U8: ELF address-space ownership transfer failed\n");

		halt_forever();
	}

	task_id_t user_tid =
		task->id;

	printf(
		"U8: starting ELF task %u\n",
		(unsigned)user_tid);

	/*
	 * hello.elf should:
	 *
	 *     start at ELF e_entry
	 *     execute separately compiled C
	 *     call debug_write
	 *     return from main
	 *     crt0 converts main's return value to exit(status)
	 */
	task_yield();

	/*
	 * The task pointer may already have been freed by the reaper.
	 * Do not dereference it beyond this point.
	 */
	for (size_t i = 0;
		 i < 64u;
		 ++i)
	{
		if (task_cleanup_total_reaped() >=
				reaped_before + 1u &&
			task_cleanup_pending_count() == 0)
		{
			break;
		}

		task_yield();
	}

	uint64_t reaped_after =
		task_cleanup_total_reaped();

	size_t live_after =
		task_live_count();

	size_t pending_after =
		task_cleanup_pending_count();

	if (reaped_after !=
		reaped_before + 1u)
	{
		printf(
			"U8: ELF task was not reaped exactly once\n");

		halt_forever();
	}

	if (pending_after != 0)
	{
		printf(
			"U8: ELF task cleanup did not finish\n");

		halt_forever();
	}

	if (live_after !=
		live_before)
	{
		printf(
			"U8: ELF task lifecycle leaked a task\n");

		halt_forever();
	}

	printf(
		"U8: ELF task exited and was reaped\n");

	printf(
		"U8: real ELF32 userspace confirmed\n");

	halt_forever();
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

	if (!syscall_initialize())
	{
		printf("U2: syscall initialization failed\n");
		halt_forever();
	}

	printf("U2: syscall gate initialized\n");

	pic_initialize();
	printf("pic initialized\n");

	paging_initialize();
	printf("paging initialized\n");

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

	cpu_local_t *bsp_cpu =
		cpu_current();

	if (bsp_cpu == NULL)
	{
		printf("U1: failed to resolve BSP CPU-local state\n");
		halt_forever();
	}

	if (!gdt_load_tss(bsp_cpu->index))
	{
		printf("U1: failed to load BSP TSS\n");
		halt_forever();
	}

	printf(
		"U1: BSP TSS loaded for CPU %u\n",
		(unsigned)bsp_cpu->index);

	scheduler_initialize();
	printf("scheduler initialized\n");

	task_initialize();
	printf("BSP task system initialized\n");

	u8_elf_test();

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

	yield_forever();
}
