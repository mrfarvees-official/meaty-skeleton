#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/logger.h>
#include <kernel/multiboot.h>
#include <kernel/paging.h>
#include <kernel/address_space.h>
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
#include <kernel/spawn.h>

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

static void process_waitpid_test(void)
{
	printf(
		"\n=== P1D.2 KERNEL WAITPID TEST ===\n");

	size_t processes_before =
		process_live_count();

	size_t tasks_before =
		task_live_count();

	uint64_t reaped_before =
		task_cleanup_total_reaped();

	/*
	 * ----------------------------------------------------------
	 * STEP 1
	 * Acquire PID 1, the immortal kernel parent.
	 * ----------------------------------------------------------
	 */
	process_t *parent =
		process_acquire_by_id(
			1u);

	if (parent == NULL)
	{
		log_error(
			"P1D.2: failed acquiring parent PID 1\n");

		halt_forever();
	}

	if (process_child_count(
			parent) !=
		0u)
	{
		log_error(
			"P1D.2: parent already owns children before test\n");

		halt_forever();
	}

	/*
	 * ----------------------------------------------------------
	 * STEP 2
	 * Spawn the real U12.4 userspace process.
	 *
	 * This remains the first dynamically-created process:
	 *
	 *     PID 2 = userspace process
	 *
	 * process_spawn_user() now returns PID.
	 * ----------------------------------------------------------
	 */
	const char *argv[] =
		{
			"one",
			"one",
			"two"};

	process_id_t child_pid =
		process_spawn_user(
			"/bin/hello.nex",
			3u,
			argv);

	if (child_pid ==
		PROCESS_ID_INVALID)
	{
		log_error(
			"P1D.2: process_spawn_user failed\n");

		halt_forever();
	}

	if (child_pid !=
		2u)
	{
		log_error(
			"P1D.2: expected first userspace PID=2 actual=%u\n",
			(unsigned)child_pid);

		halt_forever();
	}

	process_t *child =
		process_acquire_by_id(
			child_pid);

	if (child == NULL)
	{
		log_error(
			"P1D.2: failed acquiring child pid=%u\n",
			(unsigned)child_pid);

		halt_forever();
	}

	if (process_parent_id(
			child) !=
		process_id(parent))
	{
		log_error(
			"P1D.2: child parent mismatch "
			"expected=%u actual=%u\n",
			(unsigned)process_id(parent),
			(unsigned)process_parent_id(child));

		halt_forever();
	}

	if (process_child_count(
			parent) !=
		1u)
	{
		log_error(
			"P1D.2: parent child count expected=1 actual=%lu\n",
			(unsigned long)
				process_child_count(parent));

		halt_forever();
	}

	log_success(
		"P1D.2: parent=%u owns child=%u\n",
		(unsigned)process_id(parent),
		(unsigned)child_pid);

	/*
	 * ----------------------------------------------------------
	 * STEP 3
	 * Wait for U12.4 main + worker to exit and be reaped.
	 * ----------------------------------------------------------
	 */
	while (task_cleanup_total_reaped() <
		   reaped_before + 3u)
	{
		task_yield();
	}

	while (task_cleanup_pending_count() !=
		   0u)
	{
		task_yield();
	}

	process_info_t info;

	if (!process_snapshot(
			child,
			&info))
	{
		log_error(
			"P1D.2: child snapshot failed\n");

		halt_forever();
	}

	if (info.state !=
			PROCESS_ZOMBIE ||
		info.thread_count !=
			0u ||
		info.exit_status !=
			0 ||
		info.parent_id !=
			process_id(parent))
	{
		log_error(
			"P1D.2: invalid zombie "
			"pid=%u parent=%u state=%u "
			"threads=%lu status=%d\n",
			(unsigned)info.id,
			(unsigned)info.parent_id,
			(unsigned)info.state,
			(unsigned long)info.thread_count,
			info.exit_status);

		halt_forever();
	}

	log_success(
		"P1D.2: child=%u retained as ZOMBIE "
		"parent=%u threads=0 status=%d\n",
		(unsigned)info.id,
		(unsigned)info.parent_id,
		info.exit_status);

	/*
	 * ----------------------------------------------------------
	 * STEP 4
	 * Collect the real zombie through process_waitpid().
	 * ----------------------------------------------------------
	 */
	int status =
		-1;

	if (!process_waitpid(
			parent,
			child_pid,
			&status))
	{
		log_error(
			"P1D.2: waitpid failed collecting child=%u\n",
			(unsigned)child_pid);

		halt_forever();
	}

	if (status !=
		0)
	{
		log_error(
			"P1D.2: waitpid returned wrong status=%d\n",
			status);

		halt_forever();
	}

	if (process_child_count(
			parent) !=
		0u)
	{
		log_error(
			"P1D.2: child remained attached after collection\n");

		halt_forever();
	}

	log_success(
		"P1D.2: waitpid collected child=%u status=%d\n",
		(unsigned)child_pid,
		status);

	/*
	 * A collected child cannot be collected twice.
	 *
	 * Also verify a failed wait leaves status untouched.
	 */
	status =
		12345;

	if (process_waitpid(
			parent,
			child_pid,
			&status))
	{
		log_error(
			"P1D.2: second waitpid unexpectedly succeeded\n");

		halt_forever();
	}

	if (status !=
		12345)
	{
		log_error(
			"P1D.2: failed waitpid modified status\n");

		halt_forever();
	}

	log_success(
		"P1D.2: second waitpid correctly returned false\n");

	/*
	 * The test still owns its independent reference acquired above.
	 *
	 * Therefore waitpid removed parent ownership, but the object
	 * remains alive until this reference is released.
	 */
	process_release(
		child);

	child =
		NULL;

	process_t *lookup =
		process_acquire_by_id(
			child_pid);

	if (lookup != NULL)
	{
		log_error(
			"P1D.2: child=%u remained after final reference release\n",
			(unsigned)child_pid);

		process_release(
			lookup);

		halt_forever();
	}

	log_success(
		"P1D.2: child reference released after collection\n");

	/*
	 * ----------------------------------------------------------
	 * STEP 5
	 * Deterministically test RUNNING -> false.
	 *
	 * Doing this with the real U12.4 task immediately after spawn
	 * would be SMP-racy as a test: another CPU could legitimately
	 * run and reap it before this CPU calls waitpid().
	 *
	 * Instead create another process after U12.4 is finished and
	 * manually attach one execution member.
	 *
	 * No task is created.
	 * ----------------------------------------------------------
	 */
	address_space_t *kernel_space =
		address_space_kernel();

	if (kernel_space == NULL)
	{
		log_error(
			"P1D.2: kernel address space unavailable\n");

		halt_forever();
	}

	process_t *running_child =
		process_create(
			kernel_space,
			process_id(parent));

	if (running_child == NULL)
	{
		log_error(
			"P1D.2: failed creating synthetic running child\n");

		halt_forever();
	}

	process_id_t running_pid =
		process_id(
			running_child);

	if (!process_thread_attach(
			running_child))
	{
		log_error(
			"P1D.2: failed attaching synthetic process thread\n");

		halt_forever();
	}

	if (process_state(
			running_child) !=
		PROCESS_RUNNING)
	{
		log_error(
			"P1D.2: synthetic child did not become RUNNING\n");

		halt_forever();
	}

	status =
		54321;

	if (process_waitpid(
			parent,
			running_pid,
			&status))
	{
		log_error(
			"P1D.2: waitpid collected RUNNING child=%u\n",
			(unsigned)running_pid);

		halt_forever();
	}

	if (status !=
		54321)
	{
		log_error(
			"P1D.2: RUNNING wait modified status\n");

		halt_forever();
	}

	if (process_child_count(
			parent) !=
		1u)
	{
		log_error(
			"P1D.2: RUNNING child ownership changed\n");

		halt_forever();
	}

	log_success(
		"P1D.2: RUNNING child=%u correctly returned false\n",
		(unsigned)running_pid);

	/*
	 * ----------------------------------------------------------
	 * STEP 6
	 * Turn synthetic child into a zombie and collect it.
	 *
	 * Use a non-zero status so propagation is explicit.
	 * ----------------------------------------------------------
	 */
	if (!process_set_exit_status(
			running_child,
			37))
	{
		log_error(
			"P1D.2: failed setting synthetic exit status\n");

		halt_forever();
	}

	process_thread_detach(
		running_child);

	if (process_state(
			running_child) !=
		PROCESS_ZOMBIE)
	{
		log_error(
			"P1D.2: synthetic child did not become ZOMBIE\n");

		halt_forever();
	}

	status =
		-1;

	if (!process_waitpid(
			parent,
			running_pid,
			&status))
	{
		log_error(
			"P1D.2: failed collecting synthetic zombie=%u\n",
			(unsigned)running_pid);

		halt_forever();
	}

	if (status !=
		37)
	{
		log_error(
			"P1D.2: synthetic status expected=37 actual=%d\n",
			status);

		halt_forever();
	}

	log_success(
		"P1D.2: ZOMBIE child=%u collected status=%d\n",
		(unsigned)running_pid,
		status);

	/*
	 * The creator reference is now the only reference left.
	 */
	process_release(
		running_child);

	running_child =
		NULL;

	/*
	 * ----------------------------------------------------------
	 * STEP 7
	 * A PID that is not a child must return false.
	 *
	 * PID 1 obviously cannot be its own child.
	 * ----------------------------------------------------------
	 */
	status =
		777;

	if (process_waitpid(
			parent,
			process_id(parent),
			&status))
	{
		log_error(
			"P1D.2: non-child PID unexpectedly collected\n");

		halt_forever();
	}

	if (status !=
		777)
	{
		log_error(
			"P1D.2: non-child wait modified status\n");

		halt_forever();
	}

	log_success(
		"P1D.2: non-child PID correctly returned false\n");

	/*
	 * ----------------------------------------------------------
	 * STEP 8
	 * Existing task/process accounting must return to baseline.
	 * ----------------------------------------------------------
	 */
	if (task_live_count() !=
		tasks_before)
	{
		log_error(
			"P1D.2: task count did not restore "
			"before=%lu after=%lu\n",
			(unsigned long)tasks_before,
			(unsigned long)task_live_count());

		halt_forever();
	}

	if (task_cleanup_total_reaped() !=
		reaped_before + 3u)
	{
		log_error(
			"P1D.2: unexpected reaper count "
			"before=%llu after=%llu\n",
			(unsigned long long)reaped_before,
			(unsigned long long)
				task_cleanup_total_reaped());

		halt_forever();
	}

	if (process_child_count(
			parent) !=
		0u)
	{
		log_error(
			"P1D.2: parent still owns children at end of test\n");

		halt_forever();
	}

	if (process_live_count() !=
		processes_before)
	{
		log_error(
			"P1D.2: process count did not restore "
			"before=%lu after=%lu\n",
			(unsigned long)processes_before,
			(unsigned long)process_live_count());

		halt_forever();
	}

	process_release(
		parent);

	parent =
		NULL;

	log_success(
		"P1D.2: KERNEL WAITPID TEST PASSED\n");
}

static void launch_userspace_shell(void)
{
	const char *shell_argv[] =
		{
			"/bin/sh.nex",
			NULL};

	process_id_t shell_pid =
		process_spawn_user(
			"/bin/sh.nex",
			1u,
			shell_argv);

	if (shell_pid ==
		PROCESS_ID_INVALID)
	{
		log_error(
			"shell: failed launching /bin/sh.nex\n");

		halt_forever();
	}

	log_success(
		"shell: launched /bin/sh.nex pid=%u\n",
		(unsigned)shell_pid);
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_address)
{
	terminal_initialize();

	validate_multiboot_magic(multiboot_magic);

	pmm_initialize(multiboot_info_address);
	log_info("pmm initialized\n");

	gdt_initialize();
	log_info("gdt initialized\n");

	idt_initialize();
	log_info("idt initialized\n");

	interrupt_initialization();
	log_info("interrupts initialized\n");

	if (!syscall_initialize())
	{
		log_error("Syscall initialization failed\n");
		halt_forever();
	}

	log_info("syscall gate initialized\n");

	pic_initialize();
	log_info("pic initialized\n");

	paging_initialize();
	log_info("paging initialized\n");

	heap_initialize();
	log_info("heap initialized\n");

	if (!address_space_initialize())
	{
		log_error("address-space initialization failed\n");

		halt_forever();
	}
	log_info("address-space subsystem initialized\n");

	if (!acpi_initialize())
	{
		log_error("ACPI initialization failed\n");
		halt_forever();
	}

	if (!smp_detect_cpus())
	{
		log_error("SMP CPU detection failed\n");
		halt_forever();
	}

	cpu_local_initialize();
	log_info("BSP CPU-local state initialized\n");

	cpu_local_t *bsp_cpu =
		cpu_current();

	if (bsp_cpu == NULL)
	{
		log_error("Failed to resolve BSP CPU-local state\n");
		halt_forever();
	}

	if (!gdt_load_tss(bsp_cpu->index))
	{
		log_error("U1: failed to load BSP TSS\n");
		halt_forever();
	}

	log_info("U1: BSP TSS loaded for CPU %u\n", (unsigned)bsp_cpu->index);

	scheduler_initialize();
	log_info("scheduler initialized\n");

	task_initialize();
	log_info("BSP task system initialized\n");

	process_initialize();
	log_info("process system initialized\n");

	sleep_queue_initialize();
	log_info("sleep queue initialized\n");

	vfs_initialize();
	log_info("VFS initialized\n");

	pci_device_t ahci_controller;

	if (pci_find_ahci_controller(&ahci_controller))
	{
		if (!ahci_probe(
				&ahci_controller))
		{
			log_error("AHCI: probe failed\n");
		}
	}
	else
	{
		log_info("AHCI: no controller detected\n");
	}

	block_device_t *disk =
		ahci_primary_disk();

	if (disk == NULL)
	{
		log_error("Storage: no AHCI disk\n");

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
		log_error("Storage: no usable partitions\n");

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
			log_error("Partition: initial write-test read failed\n");

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
			log_error("Partition: write test failed\n");

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
			log_error("Partition: verify read failed\n");

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
			log_error("Partition: write verification FAILED\n");

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
			log_error("Partition: failed restoring test sector\n");

			halt_forever();
		}

		log_info("Partition: write/read test passed\n");
	}

	static ext2_fs_t ext2_fs;

	if (!ext2_mount(
			&partitions[0].block,
			&ext2_fs))
	{
		log_error("EXT2: mount failed\n");

		halt_forever();
	}

	if (!vfs_set_root(
			&ext2_fs.root_vnode))
	{
		log_error("EXT2: failed to set VFS root\n");

		halt_forever();
	}

	log_info("EXT2: mounted as /\n");

	/*
	 * U10 proves that a native .nex executable can be obtained through
	 * the mounted filesystem/VFS and passed into the existing ELF +
	 * U7 userspace lifecycle.
	 *
	 * The test intentionally halts after completing, so later hardware
	 * initialization remains unreachable during this bring-up proof.
	 */

	if (!smp_start_aps())
	{
		log_error("AP startup failed\n");
		halt_forever();
	}

	log_info("SMP online CPUs: %u\n", (unsigned)smp_online_cpu_count());

	if (pit_initialize(PIT_DEFAULT_FREQUENCY_HZ) != 0)
	{
		log_error("PIT initialization failed\n");

		halt_forever();
	}

	log_info("PIT initialized\n");

	if (!keyboard_initialize())
	{
		log_error("keyboard initialization failed\n");

		halt_forever();
	}

	log_info("keyboard initialized\n");

	/*
	 * Allow hardware IRQ delivery first.
	 *
	 * Timer accounting, sleep wakeups, keyboard IRQs, PIC EOIs,
	 * etc. can all run here.
	 *
	 * Actual scheduler context switching is still gated on this CPU.
	 */
	interrupt_enable();

	process_waitpid_test();

	/*
	 * P1D.2 has completed and restored its process/task baseline.
	 *
	 * Start the first interactive userspace shell as a child of
	 * immortal kernel PID 1.
	 */
	launch_userspace_shell();

	yield_forever();
}