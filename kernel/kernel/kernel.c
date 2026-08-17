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
#define U3_TEST_USER_ADDRESS 0x00400000u

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
#define U3C_KERNEL_TEST_ADDRESS 0xE4000000u

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

static void u1_ring3_task(void *argument)
	__attribute__((noreturn));

static void u1_ring3_task(void *argument)
{
	(void)argument;

	task_t *task =
		task_current();

	cpu_local_t *cpu =
		cpu_current();

	if (task == NULL ||
		cpu == NULL)
	{
		printf("U1d: missing current task/CPU\n");
		halt_forever();
	}

	uintptr_t actual_directory =
		paging_current_directory();

	printf(
		"U3d: task=%u task_cr3=0x%lx actual_cr3=0x%lx\n",
		(unsigned)task->id,
		(unsigned long)task->page_directory,
		(unsigned long)actual_directory);

	if (actual_directory !=
			task->page_directory ||
		actual_directory ==
			paging_kernel_directory())
	{
		printf(
			"U3d: private task address space FAILED\n");
		halt_forever();
	}

	printf(
		"U3d: scheduler installed private address space\n");

	uintptr_t expected_esp0 =
		task_kernel_stack_top(task);

	uintptr_t actual_esp0 = 0;

	if (expected_esp0 == 0)
	{
		printf("U1d: test task has no managed kernel stack\n");
		halt_forever();
	}

	if (!gdt_get_kernel_stack(
			cpu->index,
			&actual_esp0))
	{
		printf("U1d: failed reading TSS esp0\n");
		halt_forever();
	}

	printf(
		"U1d: task=%u cpu=%u kernel_stack_top=0x%lx\n",
		(unsigned)task->id,
		(unsigned)cpu->index,
		(unsigned long)expected_esp0);

	printf(
		"U1d: TSS esp0=0x%lx\n",
		(unsigned long)actual_esp0);

	if (actual_esp0 != expected_esp0)
	{
		printf("U1d: TSS esp0 does not match current task\n");
		halt_forever();
	}

	printf("U1d: scheduler-owned kernel stack confirmed\n");

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

	printf(
		"U1: entering user mode eip=0x%lx esp=0x%lx\n",
		(unsigned long)U1_USER_CODE_ADDRESS,
		(unsigned long)U1_USER_STACK_TOP);

	arch_enter_user(
		U1_USER_CODE_ADDRESS,
		U1_USER_STACK_TOP);
}

static void u4_inactive_mapping_test(void)
{
	printf(
		"\n=== U4b USER ADDRESS-SPACE DESTRUCTION TEST ===\n");

	const uintptr_t user_test_address =
		0x00800000u;

	const uint32_t test_value =
		0x44B00001u;

	uintptr_t kernel_directory =
		paging_kernel_directory();

	if (kernel_directory == 0 ||
		paging_current_directory() !=
			kernel_directory)
	{
		printf(
			"U4b: not running in kernel address space\n");
		halt_forever();
	}

	/*
	 * Warm up the kernel scratch page-table infrastructure before
	 * recording the PMM baseline.
	 *
	 * paging_create_user_directory() uses 0xE0000000 as a temporary
	 * mapping.  Its first use may allocate one persistent supervisor
	 * page-table frame in the kernel directory.
	 *
	 * That frame belongs to the kernel paging infrastructure, not to
	 * the user address space being tested below.
	 */
	uintptr_t warmup_directory = 0;

	if (!paging_create_user_directory(
			&warmup_directory))
	{
		printf(
			"U4b: failed warming scratch mapping\n");
		halt_forever();
	}

	paging_destroy_user_directory(
		warmup_directory);

	if (paging_current_directory() !=
		kernel_directory)
	{
		printf(
			"U4b: warmup changed active CR3\n");
		halt_forever();
	}

	/*
	 * From this point onward, all temporary allocations measured by
	 * the test belong to the user address space itself:
	 *
	 *     1 page-directory frame
	 *     1 user data frame
	 *     1 private page-table frame
	 */
	size_t free_frames_before =
		pmm_get_free_frame_count();

	printf(
		"U4b: free frames before=%lu\n",
		(unsigned long)free_frames_before);

	uintptr_t user_directory = 0;

	if (!paging_create_user_directory(
			&user_directory))
	{
		printf(
			"U4b: failed creating user directory\n");
		halt_forever();
	}

	if (user_directory ==
		kernel_directory)
	{
		printf(
			"U4b: user directory is not distinct\n");
		halt_forever();
	}

	uintptr_t user_frame =
		pmm_allocate_frame();

	if (user_frame == 0)
	{
		printf(
			"U4b: failed allocating user frame\n");

		paging_destroy_user_directory(
			user_directory);

		halt_forever();
	}

	if (!paging_map_page_in_directory(
			user_directory,
			user_test_address,
			user_frame,
			PAGE_USER | PAGE_WRITABLE))
	{
		printf(
			"U4b: failed mapping private user page\n");

		pmm_free_frame(
			user_frame);

		paging_destroy_user_directory(
			user_directory);

		halt_forever();
	}

	printf(
		"U4b: private page created in inactive CR3\n");

	if (!paging_switch_directory(
			user_directory))
	{
		printf(
			"U4b: failed switching to user directory\n");
		halt_forever();
	}

	volatile uint32_t *probe =
		(volatile uint32_t *)
			user_test_address;

	*probe =
		test_value;

	if (*probe !=
		test_value)
	{
		printf(
			"U4b: private mapping read/write failed\n");
		halt_forever();
	}

	uintptr_t resolved_physical = 0;

	if (!paging_get_physical_address(
			user_test_address,
			&resolved_physical))
	{
		printf(
			"U4b: private mapping disappeared\n");
		halt_forever();
	}

	resolved_physical &=
		~(uintptr_t)(PAGE_SIZE - 1u);

	if (resolved_physical !=
		user_frame)
	{
		printf(
			"U4b: private mapping resolved incorrectly\n");
		halt_forever();
	}

	printf(
		"U4b: private user mapping verified\n");

	if (!paging_switch_directory(
			kernel_directory))
	{
		printf(
			"U4b: failed returning to kernel CR3\n");
		halt_forever();
	}

	paging_destroy_user_directory(
		user_directory);

	if (paging_current_directory() !=
		kernel_directory)
	{
		printf(
			"U4b: destroy changed active CR3\n");
		halt_forever();
	}

	size_t free_frames_after =
		pmm_get_free_frame_count();

	printf(
		"U4b: free frames after=%lu\n",
		(unsigned long)free_frames_after);

	if (free_frames_after !=
		free_frames_before)
	{
		printf(
			"U4b: address-space frames leaked\n");

		printf(
			"U4b: before=%lu after=%lu\n",
			(unsigned long)free_frames_before,
			(unsigned long)free_frames_after);

		halt_forever();
	}

	printf(
		"U4b: private pages and page tables reclaimed\n");

	printf(
		"U4b: user address-space destruction confirmed\n");

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

	u4_inactive_mapping_test();

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
