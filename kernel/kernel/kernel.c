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

static void process_exit_status_test(void)
{
	printf(
		"\n=== P1C PROCESS EXIT STATUS TEST ===\n");

	/*
	 * Expected baseline:
	 *
	 *     PID 1 = immortal kernel process
	 */
	size_t processes_before =
		process_live_count();

	uint64_t reaped_before =
		task_cleanup_total_reaped();

	size_t tasks_before =
		task_live_count();

	/*
	 * ----------------------------------------------------------
	 * STEP 1
	 * Spawn the real userspace ELF.
	 * ----------------------------------------------------------
	 */
	const char *argv[] =
		{
			"one",
			"one",
			"two"};

	task_id_t main_tid =
		process_spawn_user(
			"/bin/hello.nex",
			3u,
			argv);

	if (main_tid == 0u)
	{
		log_error(
			"P1C: process_spawn_user failed\n");

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	/*
	 * We currently know the first dynamically-created process
	 * receives PID 2 during this isolated boot test.
	 *
	 * Later process_spawn_user() should return PID directly,
	 * which will remove this temporary assumption.
	 */
	process_id_t pid =
		2u;

	process_t *process =
		process_acquire_by_id(
			pid);

	if (process == NULL)
	{
		log_error(
			"P1C: failed acquiring pid=%u\n",
			(unsigned)pid);

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	/*
	 * Holding this retained reference is important.
	 *
	 * Without it, the final task reaper could release the final
	 * process reference and destroy the process before this test
	 * gets a chance to inspect its zombie state.
	 */
	if (process_state(
			process) !=
		PROCESS_RUNNING)
	{
		log_error(
			"P1C: spawned process is not RUNNING\n");

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	log_success(
		"P1C: spawned pid=%u main_tid=%u\n",
		(unsigned)pid,
		(unsigned)main_tid);

	/*
	 * ----------------------------------------------------------
	 * STEP 2
	 * Wait for both userspace tasks to exit and be reaped.
	 *
	 * U12.4 creates:
	 *
	 *     main
	 *     worker
	 *
	 * therefore exactly two additional tasks should eventually
	 * pass through the reaper.
	 * ----------------------------------------------------------
	 */
	while (task_cleanup_total_reaped() <
		   reaped_before + 2u)
	{
		task_yield();
	}

	/*
	 * Make sure cleanup accounting has actually settled.
	 */
	while (task_cleanup_pending_count() !=
		   0u)
	{
		task_yield();
	}

	/*
	 * ----------------------------------------------------------
	 * STEP 3
	 * Inspect retained zombie process.
	 * ----------------------------------------------------------
	 */
	process_info_t info;

	if (!process_snapshot(
			process,
			&info))
	{
		log_error(
			"P1C: process_snapshot failed\n");

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	if (info.id !=
		pid)
	{
		log_error(
			"P1C: snapshot PID mismatch "
			"expected=%u actual=%u\n",
			(unsigned)pid,
			(unsigned)info.id);

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	if (info.state !=
		PROCESS_ZOMBIE)
	{
		log_error(
			"P1C: process did not become ZOMBIE "
			"state=%u\n",
			(unsigned)info.state);

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	if (info.thread_count !=
		0u)
	{
		log_error(
			"P1C: zombie still has threads=%lu\n",
			(unsigned long)
				info.thread_count);

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	if (info.termination_reason !=
		PROCESS_TERMINATION_NORMAL)
	{
		log_error(
			"P1C: wrong termination reason=%u\n",
			(unsigned)
				info.termination_reason);

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	if (info.exit_status !=
		0)
	{
		log_error(
			"P1C: wrong exit status=%d\n",
			info.exit_status);

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	log_success(
		"P1C: pid=%u ZOMBIE "
		"threads=0 status=%d\n",
		(unsigned)info.id,
		info.exit_status);

	/*
	 * ----------------------------------------------------------
	 * STEP 4
	 * Verify the process is still discoverable while this test
	 * owns its retained reference.
	 * ----------------------------------------------------------
	 */
	process_t *lookup =
		process_acquire_by_id(
			pid);

	if (lookup == NULL)
	{
		log_error(
			"P1C: zombie disappeared from registry too early\n");

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	process_info_t lookup_info;

	if (!process_snapshot(
			lookup,
			&lookup_info))
	{
		log_error(
			"P1C: zombie lookup snapshot failed\n");

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	if (lookup_info.state !=
			PROCESS_ZOMBIE ||
		lookup_info.exit_status !=
			0 ||
		lookup_info.thread_count !=
			0u)
	{
		log_error(
			"P1C: registry zombie snapshot invalid\n");

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	process_release(
		lookup);

	lookup =
		NULL;

	/*
	 * ----------------------------------------------------------
	 * STEP 5
	 * Validate task cleanup.
	 * ----------------------------------------------------------
	 */
	if (task_live_count() !=
		tasks_before)
	{
		log_error(
			"P1C: task count did not restore "
			"before=%lu after=%lu\n",
			(unsigned long)
				tasks_before,
			(unsigned long)
				task_live_count());

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	if (task_cleanup_total_reaped() !=
		reaped_before + 2u)
	{
		log_error(
			"P1C: expected exactly two reaped tasks "
			"before=%llu after=%llu\n",
			(unsigned long long)
				reaped_before,
			(unsigned long long)
				task_cleanup_total_reaped());

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	/*
	 * ----------------------------------------------------------
	 * STEP 6
	 * Release the test's retained process reference.
	 *
	 * No task references remain now.
	 *
	 * Therefore this should destroy PID 2 and release its final
	 * address-space reference.
	 * ----------------------------------------------------------
	 */
	process_release(
		process);

	process =
		NULL;

	/*
	 * PID must disappear from the registry.
	 */
	lookup =
		process_acquire_by_id(
			pid);

	if (lookup != NULL)
	{
		log_error(
			"P1C: pid=%u remained after final release\n",
			(unsigned)pid);

		process_release(
			lookup);

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	/*
	 * Process accounting must return to the value from before
	 * spawning the ELF.
	 */
	if (process_live_count() !=
		processes_before)
	{
		log_error(
			"P1C: process count did not restore "
			"before=%lu after=%lu\n",
			(unsigned long)
				processes_before,
			(unsigned long)
				process_live_count());

		for (;;)
			__asm__ volatile(
				"cli; hlt");
	}

	log_success(
		"P1C: zombie retained exit status correctly\n");

	log_success(
		"P1C: PROCESS EXIT STATUS TEST PASSED\n");
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

	process_exit_status_test();

	yield_forever();
}