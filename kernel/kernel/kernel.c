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

#define U1_USER_CODE_ADDRESS 0x00400000u
#define U1_USER_STACK_ADDRESS 0x00401000u
#define U1_USER_STACK_TOP 0x00402000u

/*
 * Temporary supervisor alias used only while copying the hand-written
 * userspace probe into its newly allocated private physical frame.
 *
 * paging_create_user_directory() already uses this same VA internally
 * and removes its PTE before returning, so it is free here.
 */
#define U5_CODE_COPY_ADDRESS 0xE0000000u

extern void arch_enter_user(
	uintptr_t instruction_pointer,
	uintptr_t stack_pointer)
	__attribute__((noreturn));

extern void u1_user_test_entry(void);

typedef struct
{
	uintptr_t entry;
	uintptr_t stack_top;
} user_launch_context_t;

static user_launch_context_t u5_launch_context;

static void u1_ring3_task(void *argument)
	__attribute__((noreturn));

static void u1_ring3_task(void *argument)
{
	user_launch_context_t *launch =
		(user_launch_context_t *)argument;

	if (launch == NULL ||
		launch->entry == 0 ||
		launch->stack_top == 0)
	{
		printf(
			"U5b: missing user launch context\n");
		halt_forever();
	}

	task_t *task =
		task_current();

	if (task == NULL)
	{
		printf(
			"U5b: no current task\n");
		halt_forever();
	}

	uintptr_t actual_directory =
		paging_current_directory();

	printf(
		"U5b: task=%u task_cr3=0x%lx actual_cr3=0x%lx\n",
		(unsigned)task->id,
		(unsigned long)task->page_directory,
		(unsigned long)actual_directory);

	if (actual_directory !=
			task->page_directory ||
		actual_directory ==
			paging_kernel_directory())
	{
		printf(
			"U5b: scheduler did not install prepared CR3\n");
		halt_forever();
	}

	/*
	 * Copy the launch values into locals while still in CPL0.
	 *
	 * After arch_enter_user(), the task begins executing the prepared
	 * image directly and does not need to reference this kernel-side
	 * context again.
	 */
	uintptr_t entry =
		launch->entry;

	uintptr_t stack_top =
		launch->stack_top;

	printf(
		"U5b: launch context entry=0x%lx stack=0x%lx\n",
		(unsigned long)entry,
		(unsigned long)stack_top);

	printf(
		"U5b: entering prepared userspace\n");

	arch_enter_user(
		entry,
		stack_top);
}

static void u5_prepared_ring3_test(void)
{
	printf(
		"\n=== U5b USER LAUNCH CONTEXT TEST ===\n");

	uintptr_t kernel_directory =
		paging_kernel_directory();

	if (kernel_directory == 0 ||
		paging_current_directory() !=
			kernel_directory)
	{
		printf(
			"U5b: not running in kernel address space\n");
		halt_forever();
	}

	/*
	 * Describe the userspace image independently of the scheduler
	 * task that will eventually launch it.
	 *
	 * A future loader will produce these values instead.
	 */
	u5_launch_context.entry =
		U1_USER_CODE_ADDRESS;

	u5_launch_context.stack_top =
		U1_USER_STACK_TOP;

	printf(
		"U5b: image entry=0x%lx stack=0x%lx\n",
		(unsigned long)u5_launch_context.entry,
		(unsigned long)u5_launch_context.stack_top);

	/*
	 * This raw one-page probe currently requires a page-aligned entry
	 * because its whole code page is copied and mapped at that address.
	 *
	 * This restriction belongs to the temporary probe preparation,
	 * not to the generic launch task.
	 */
	if ((u5_launch_context.entry &
		 (PAGE_SIZE - 1u)) != 0)
	{
		printf(
			"U5b: probe entry is not page aligned\n");
		halt_forever();
	}

	/*
	 * Create the scheduler task first so its managed kernel stack is
	 * already represented in the supervisor mappings inherited by
	 * the new address space.
	 *
	 * Pass the launch description through the task system's existing
	 * argument field.
	 */
	task_t *task =
		task_create_kernel_with_policy(
			u1_ring3_task,
			&u5_launch_context,
			SCHED_POLICY_REALTIME);

	if (task == NULL)
	{
		printf(
			"U5b: failed creating scheduler task\n");
		halt_forever();
	}

	printf(
		"U5b: created task %u with launch context\n",
		(unsigned)task->id);

	uintptr_t user_directory = 0;

	if (!paging_create_user_directory(
			&user_directory))
	{
		printf(
			"U5b: failed creating user directory\n");
		halt_forever();
	}

	if (user_directory ==
		kernel_directory)
	{
		printf(
			"U5b: user directory is not distinct\n");
		halt_forever();
	}

	printf(
		"U5b: created private CR3=0x%lx\n",
		(unsigned long)user_directory);

	/*
	 * Allocate a private code frame and copy the existing hand-written
	 * ring-3 probe into it through the temporary supervisor alias.
	 */
	uintptr_t code_frame =
		pmm_allocate_frame();

	if (code_frame == 0)
	{
		printf(
			"U5b: failed allocating code frame\n");
		halt_forever();
	}

	if (!paging_map_page(
			U5_CODE_COPY_ADDRESS,
			code_frame,
			PAGE_WRITABLE))
	{
		printf(
			"U5b: failed mapping code-copy alias\n");
		halt_forever();
	}

	memcpy(
		(void *)U5_CODE_COPY_ADDRESS,
		(const void *)u1_user_test_entry,
		PAGE_SIZE);

	if (!paging_unmap_page(
			U5_CODE_COPY_ADDRESS,
			false))
	{
		printf(
			"U5b: failed removing code-copy alias\n");
		halt_forever();
	}

	if (!paging_map_page_in_directory(
			user_directory,
			u5_launch_context.entry,
			code_frame,
			PAGE_USER))
	{
		printf(
			"U5b: failed mapping private user code\n");
		halt_forever();
	}

	/*
	 * The current probe uses one stack page immediately below
	 * U1_USER_STACK_TOP.
	 */
	uintptr_t stack_frame =
		pmm_allocate_frame();

	if (stack_frame == 0)
	{
		printf(
			"U5b: failed allocating stack frame\n");
		halt_forever();
	}

	if (!paging_map_page_in_directory(
			user_directory,
			U1_USER_STACK_ADDRESS,
			stack_frame,
			PAGE_USER | PAGE_WRITABLE))
	{
		printf(
			"U5b: failed mapping private user stack\n");
		halt_forever();
	}

	printf(
		"U5b: image prepared while target CR3 inactive\n");

	if (paging_current_directory() !=
		kernel_directory)
	{
		printf(
			"U5b: image preparation changed active CR3\n");
		halt_forever();
	}

	task->page_directory =
		user_directory;

	printf(
		"U5b: attached CR3=0x%lx to task %u\n",
		(unsigned long)user_directory,
		(unsigned)task->id);

	printf(
		"U5b: yielding to launch-context task\n");

	task_yield();

	printf(
		"U5b: ERROR user task returned\n");
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

	u5_prepared_ring3_test();

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
