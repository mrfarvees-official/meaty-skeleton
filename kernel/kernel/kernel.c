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
	 * The BSP is currently the only CPU allowed to schedule normal tasks.
	 *
	 * AP task migration is disabled until the scheduler gains per-CPU
	 * run queues or explicit task affinity.
	 */
	scheduler_enable_preemption();

	/*
	 * Hardware interrupts may now safely request BSP scheduling.
	 */
	interrupt_enable();

	/*
	 * Start the first interactive userspace shell.
	 */
	launch_userspace_shell();

	/*
	 * kernel_main's bootstrap context yields permanently after publishing
	 * the shell.
	 */
	yield_forever();
}