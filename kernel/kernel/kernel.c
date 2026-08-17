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

typedef struct
{
	const char *name;
	uintptr_t expected_directory;
	unsigned slot;
} u3b_task_test_t;

static u3b_task_test_t u3b_contexts[2];
static volatile bool u3b_seen[2];

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

static void u1_start_scheduler_owned_test(void)
{
	task_t *task =
		task_create_kernel_with_policy(
			u1_ring3_task,
			NULL,
			SCHED_POLICY_REALTIME);

	if (task == NULL)
	{
		printf("U1d: failed creating scheduler-owned test task\n");
		halt_forever();
	}

	printf(
		"U1d: created scheduler-owned task %u\n",
		(unsigned)task->id);

	/*
	 * The U1 task is REALTIME while the bootstrap/reaper tasks are
	 * NORMAL, so the next scheduler selection deterministically
	 * chooses the U1 task.
	 */
	task_yield();

	/*
	 * The U1 task enters CPL3 and the successful trap halts.
	 * Returning here would therefore indicate a broken test.
	 */
	printf("U1d: ERROR test task returned\n");
	halt_forever();
}

static void u3d_private_ring3_test(void)
{
	printf(
		"\n=== U3d PRIVATE RING3 ADDRESS-SPACE TEST ===\n");

	uintptr_t kernel_directory =
		paging_kernel_directory();

	if (kernel_directory == 0 ||
		paging_current_directory() !=
			kernel_directory)
	{
		printf(
			"U3d: BSP did not start in kernel address space\n");
		halt_forever();
	}

	/*
	 * Create the scheduler-owned task first.
	 *
	 * This ensures its kernel stack already exists when
	 * paging_create_user_directory() snapshots supervisor mappings.
	 */
	task_t *task =
		task_create_kernel_with_policy(
			u1_ring3_task,
			NULL,
			SCHED_POLICY_REALTIME);

	if (task == NULL)
	{
		printf(
			"U3d: failed creating scheduler-owned task\n");
		halt_forever();
	}

	uintptr_t user_directory = 0;

	if (!paging_create_user_directory(
			&user_directory))
	{
		printf(
			"U3d: failed creating user address space\n");
		halt_forever();
	}

	if (user_directory ==
		kernel_directory)
	{
		printf(
			"U3d: user address space is not distinct\n");
		halt_forever();
	}

	/*
	 * U3d deliberately keeps task/process construction minimal.
	 *
	 * task->page_directory is already the authoritative scheduler
	 * address-space field, so attach the isolated directory directly.
	 */
	task->page_directory =
		user_directory;

	printf(
		"U3d: task=%u kernel_cr3=0x%lx user_cr3=0x%lx\n",
		(unsigned)task->id,
		(unsigned long)kernel_directory,
		(unsigned long)user_directory);

	printf(
		"U3d: yielding to private-address-space task\n");

	/*
	 * The task is REALTIME while the BSP/reaper are NORMAL.
	 * Scheduler selection therefore enters this task next.
	 *
	 * scheduler_schedule() installs task->page_directory before
	 * arch_context_switch().
	 */
	task_yield();

	printf(
		"U3d: ERROR ring3 task returned\n");
	halt_forever();
}

static void u3b_cr3_task(void *argument)
{
	u3b_task_test_t *test =
		(u3b_task_test_t *)argument;

	task_t *task =
		task_current();

	if (test == NULL ||
		task == NULL)
	{
		printf("U3b: missing test/task state\n");
		halt_forever();
	}

	uintptr_t actual =
		paging_current_directory();

	printf(
		"U3b: %s task=%u task_cr3=0x%lx actual_cr3=0x%lx\n",
		test->name,
		(unsigned)task->id,
		(unsigned long)task->page_directory,
		(unsigned long)actual);

	if (task->page_directory !=
			test->expected_directory ||
		actual !=
			test->expected_directory)
	{
		printf(
			"U3b: %s CR3 FAILED\n",
			test->name);

		halt_forever();
	}

	u3b_seen[test->slot] =
		true;

	printf(
		"U3b: %s address space confirmed\n",
		test->name);

	task_exit();
}

static void u3_scheduler_address_space_test(void)
{
	printf(
		"\n=== U3b SCHEDULER ADDRESS-SPACE TEST ===\n");

	uintptr_t kernel_directory =
		paging_kernel_directory();

	if (kernel_directory == 0 ||
		paging_current_directory() !=
			kernel_directory)
	{
		printf(
			"U3b: BSP did not start in kernel address space\n");

		halt_forever();
	}

	/*
	 * IMPORTANT:
	 *
	 * Allocate both task objects and their stacks BEFORE taking the
	 * U3a-style supervisor mapping snapshot.
	 *
	 * That guarantees their kernel stacks are included in the
	 * mappings inherited by the second address space.
	 */
	u3b_contexts[0].name =
		"kernel-space task";
	u3b_contexts[0].expected_directory =
		kernel_directory;
	u3b_contexts[0].slot =
		0u;

	task_t *kernel_task =
		task_create_kernel_with_policy(
			u3b_cr3_task,
			&u3b_contexts[0],
			SCHED_POLICY_REALTIME);

	if (kernel_task == NULL)
	{
		printf(
			"U3b: failed creating kernel-space task\n");

		halt_forever();
	}

	/*
	 * Allocate B while we still use the canonical kernel directory.
	 * Its page_directory will be overridden only after the isolated
	 * directory has been constructed.
	 */
	u3b_contexts[1].name =
		"isolated-space task";
	u3b_contexts[1].expected_directory =
		0;
	u3b_contexts[1].slot =
		1u;

	task_t *isolated_task =
		task_create_kernel_with_policy(
			u3b_cr3_task,
			&u3b_contexts[1],
			SCHED_POLICY_REALTIME);

	if (isolated_task == NULL)
	{
		printf(
			"U3b: failed creating isolated-space task\n");

		halt_forever();
	}

	/*
	 * Both are ordinary kernel tasks at creation time.
	 */
	if (kernel_task->page_directory !=
			kernel_directory ||
		isolated_task->page_directory !=
			kernel_directory)
	{
		printf(
			"U3b: kernel-task page-directory initialization FAILED\n");

		halt_forever();
	}

	uintptr_t isolated_directory =
		0;

	if (!paging_create_user_directory(
			&isolated_directory))
	{
		printf(
			"U3b: failed creating isolated directory\n");

		halt_forever();
	}

	if (isolated_directory ==
		kernel_directory)
	{
		printf(
			"U3b: isolated directory is not distinct\n");

		halt_forever();
	}

	/*
	 * U3b test-only assignment.
	 *
	 * We are not adding a user-task constructor yet.
	 */
	isolated_task->page_directory =
		isolated_directory;

	u3b_contexts[1].expected_directory =
		isolated_directory;

	printf(
		"U3b: kernel CR3=0x%lx isolated CR3=0x%lx\n",
		(unsigned long)kernel_directory,
		(unsigned long)isolated_directory);

	/*
	 * Both test tasks are REALTIME while the bootstrap task is NORMAL.
	 *
	 * They will run, verify their CR3 values, and terminate before
	 * this bootstrap context resumes.
	 */
	task_yield();

	/*
	 * Scheduler must have restored the bootstrap task's kernel CR3.
	 */
	if (paging_current_directory() !=
		kernel_directory)
	{
		printf(
			"U3b: BSP resumed under wrong CR3\n");

		halt_forever();
	}

	if (!u3b_seen[0] ||
		!u3b_seen[1])
	{
		printf(
			"U3b: not all address-space tasks ran\n");

		halt_forever();
	}

	printf(
		"U3b: BSP kernel address space restored\n");

	/*
	 * Neither task owns this directory yet.  U3b deliberately keeps
	 * address-space lifetime separate from task lifetime.
	 */
	paging_destroy_user_directory(
		isolated_directory);

	printf(
		"U3b: scheduler CR3 switching confirmed\n");

	halt_forever();
}

static void u4_inactive_mapping_test(void)
{
	printf(
		"\n=== U4a INACTIVE ADDRESS-SPACE MAPPING TEST ===\n");

	const uintptr_t user_test_address =
		0x00800000u;

	uintptr_t kernel_directory =
		paging_kernel_directory();

	if (kernel_directory == 0 ||
		paging_current_directory() !=
			kernel_directory)
	{
		printf(
			"U4a: not running in kernel address space\n");
		halt_forever();
	}

	uintptr_t user_directory = 0;

	if (!paging_create_user_directory(
			&user_directory))
	{
		printf(
			"U4a: failed creating user directory\n");
		halt_forever();
	}

	if (user_directory ==
		kernel_directory)
	{
		printf(
			"U4a: user directory is not distinct\n");
		halt_forever();
	}

	printf(
		"U4a: kernel CR3=0x%lx user CR3=0x%lx\n",
		(unsigned long)kernel_directory,
		(unsigned long)user_directory);

	/*
	 * The user directory is still inactive here.
	 */
	if (paging_current_directory() !=
		kernel_directory)
	{
		printf(
			"U4a: CR3 changed before mapping test\n");
		halt_forever();
	}

	uintptr_t user_frame =
		pmm_allocate_frame();

	if (user_frame == 0)
	{
		printf(
			"U4a: failed allocating user frame\n");
		halt_forever();
	}

	if (!paging_map_page_in_directory(
			user_directory,
			user_test_address,
			user_frame,
			PAGE_USER | PAGE_WRITABLE))
	{
		printf(
			"U4a: inactive-directory mapping failed\n");

		pmm_free_frame(
			user_frame);

		halt_forever();
	}

	printf(
		"U4a: mapped user page while target CR3 inactive\n");

	/*
	 * The helper must have restored the BSP's kernel CR3.
	 */
	if (paging_current_directory() !=
		kernel_directory)
	{
		printf(
			"U4a: helper did not restore kernel CR3\n");
		halt_forever();
	}

	printf(
		"U4a: kernel CR3 restored after mapping\n");

	/*
	 * Now enter the target address space only to verify the mapping
	 * that was constructed while it was inactive.
	 */
	if (!paging_switch_directory(
			user_directory))
	{
		printf(
			"U4a: failed switching to user directory\n");
		halt_forever();
	}

	printf(
		"U4a: switched to target CR3 for verification\n");

	uintptr_t resolved_physical = 0;

	if (!paging_get_physical_address(
			user_test_address,
			&resolved_physical))
	{
		printf(
			"U4a: new mapping is missing\n");
		halt_forever();
	}

	resolved_physical &=
		~(uintptr_t)(PAGE_SIZE - 1u);

	if (resolved_physical !=
		user_frame)
	{
		printf(
			"U4a: mapping resolved to wrong frame\n");

		printf(
			"U4a: expected=0x%lx actual=0x%lx\n",
			(unsigned long)user_frame,
			(unsigned long)resolved_physical);

		halt_forever();
	}

	uint32_t effective_flags = 0;

	if (!paging_get_effective_flags(
			user_test_address,
			&effective_flags))
	{
		printf(
			"U4a: failed reading mapping permissions\n");
		halt_forever();
	}

	if ((effective_flags &
		 (PAGE_PRESENT |
		  PAGE_USER |
		  PAGE_WRITABLE)) !=
		(PAGE_PRESENT |
		 PAGE_USER |
		 PAGE_WRITABLE))
	{
		printf(
			"U4a: user mapping permissions are wrong\n");
		halt_forever();
	}

	volatile uint32_t *probe =
		(volatile uint32_t *)
			user_test_address;

	*probe =
		0x44A00001u;

	if (*probe !=
		0x44A00001u)
	{
		printf(
			"U4a: mapped page read/write failed\n");
		halt_forever();
	}

	printf(
		"U4a: private user mapping works in target CR3\n");

	if (!paging_switch_directory(
			kernel_directory))
	{
		halt_forever();
	}

	printf(
		"U4a: returned to kernel CR3\n");

	/*
	 * Intentional U4a limitation:
	 *
	 * Do not destroy this directory yet.
	 *
	 * It now owns a private page table, while
	 * paging_destroy_user_directory() is still the old U3a version
	 * that only releases the directory frame.
	 *
	 * U4b will handle private address-space destruction cleanly.
	 */
	printf(
		"U4a: inactive address-space mapping confirmed\n");

	halt_forever();
}

static void u3c_kernel_pde_test(void)
{
	printf(
		"\n=== U3c KERNEL PDE SHARE TEST ===\n");

	uintptr_t kernel_directory =
		paging_kernel_directory();

	if (kernel_directory == 0 ||
		paging_current_directory() !=
			kernel_directory)
	{
		printf(
			"U3c: not running in kernel address space\n");

		halt_forever();
	}

	uintptr_t user_directory = 0;

	if (!paging_create_user_directory(
			&user_directory))
	{
		printf(
			"U3c: failed creating user directory\n");

		halt_forever();
	}

	printf(
		"U3c: created user CR3=0x%lx\n",
		(unsigned long)user_directory);

	if (paging_is_mapped(
			U3C_KERNEL_TEST_ADDRESS))
	{
		printf(
			"U3c: test address already mapped\n");

		halt_forever();
	}

	uintptr_t frame =
		pmm_allocate_frame();

	if (frame == 0)
	{
		printf(
			"U3c: failed allocating frame\n");

		halt_forever();
	}

	if (!paging_map_page(
			U3C_KERNEL_TEST_ADDRESS,
			frame,
			PAGE_WRITABLE))
	{
		printf(
			"U3c: failed creating late kernel mapping\n");

		halt_forever();
	}

	printf(
		"U3c: late kernel mapping created\n");

	volatile uint32_t *probe =
		(volatile uint32_t *)
			U3C_KERNEL_TEST_ADDRESS;

	*probe =
		0xC3C3C3C3u;

	if (!paging_share_kernel_pde(
			user_directory,
			U3C_KERNEL_TEST_ADDRESS))
	{
		printf(
			"U3c: failed sharing kernel PDE\n");

		halt_forever();
	}

	printf(
		"U3c: kernel PDE copied into user directory\n");

	if (!paging_switch_directory(
			user_directory))
	{
		printf(
			"U3c: failed switching to user directory\n");

		halt_forever();
	}

	printf(
		"U3c: switched to user directory\n");

	if (!paging_is_mapped(
			U3C_KERNEL_TEST_ADDRESS))
	{
		printf(
			"U3c: shared kernel mapping is missing\n");

		halt_forever();
	}

	volatile uint32_t *shared_probe =
		(volatile uint32_t *)
			U3C_KERNEL_TEST_ADDRESS;

	if (*shared_probe !=
		0xC3C3C3C3u)
	{
		printf(
			"U3c: shared mapping contains wrong value\n");

		halt_forever();
	}

	printf(
		"U3c: late kernel mapping works in user directory\n");

	if (!paging_switch_directory(
			kernel_directory))
	{
		halt_forever();
	}

	printf(
		"U3c: returned to kernel directory\n");

	paging_destroy_user_directory(
		user_directory);

	if (!paging_unmap_page(
			U3C_KERNEL_TEST_ADDRESS,
			true))
	{
		printf(
			"U3c: cleanup failed\n");

		halt_forever();
	}

	printf(
		"U3c: kernel PDE sharing confirmed\n");

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
