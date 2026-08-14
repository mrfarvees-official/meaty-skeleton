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

	// if (!ramfs_initialize())
	// {
	// 	printf("RAMFS initialization failed\n");
	// 	halt_forever();
	// }
	// printf("RAMFS mounted as /\n");

	pci_device_t ahci_controller;

	if (pci_find_ahci_controller(&ahci_controller))
	{
		printf(
			"AHCI: PCI %u:%u.%u vendor=%x device=%x\n",
			(unsigned)ahci_controller.bus,
			(unsigned)ahci_controller.device,
			(unsigned)ahci_controller.function,
			(unsigned)ahci_controller.vendor_id,
			(unsigned)ahci_controller.device_id);

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

	printf(
		"Storage: using %s\n",
		disk->name);

	static partition_device_t partitions[8];

	size_t partition_count =
		partition_scan(
			disk,
			partitions,
			8);

	printf(
		"Storage: found %u partitions\n",
		(unsigned)partition_count);

	if (partition_count == 0)
	{
		printf(
			"Storage: no usable partitions\n");

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
		uint8_t original[32];
		uint8_t verify[32];

		const char test_data[] =
			"EXT2-W1-WRITE-TEST";

		size_t original_read = 0;
		size_t written = 0;
		size_t verify_read = 0;

		file_t *read_file = NULL;

		if (vfs_open(
				"/double.txt",
				VFS_OPEN_READ,
				&read_file) != 0)
		{
			printf(
				"EXT2-W1: failed opening file for initial read\n");

			halt_forever();
		}

		if (vfs_read(
				read_file,
				original,
				sizeof(test_data),
				&original_read) != 0 ||
			original_read != sizeof(test_data))
		{
			printf(
				"EXT2-W1: failed saving original data\n");

			vfs_close(read_file);
			halt_forever();
		}

		vfs_close(read_file);

		file_t *write_file = NULL;

		if (vfs_open(
				"/double.txt",
				VFS_OPEN_WRITE,
				&write_file) != 0)
		{
			printf(
				"EXT2-W1: failed opening file for write\n");

			halt_forever();
		}

		if (vfs_write(
				write_file,
				test_data,
				sizeof(test_data),
				&written) != 0 ||
			written != sizeof(test_data))
		{
			printf(
				"EXT2-W1: write failed\n");

			vfs_close(write_file);
			halt_forever();
		}

		vfs_close(write_file);

		read_file = NULL;

		if (vfs_open(
				"/double.txt",
				VFS_OPEN_READ,
				&read_file) != 0)
		{
			printf(
				"EXT2-W1: failed reopening file\n");

			halt_forever();
		}

		if (vfs_read(
				read_file,
				verify,
				sizeof(test_data),
				&verify_read) != 0 ||
			verify_read != sizeof(test_data))
		{
			printf(
				"EXT2-W1: verify read failed\n");

			vfs_close(read_file);
			halt_forever();
		}

		vfs_close(read_file);

		if (memcmp(
				verify,
				test_data,
				sizeof(test_data)) != 0)
		{
			printf(
				"EXT2-W1: verification FAILED\n");

			halt_forever();
		}

		/*
		 * Restore original bytes.
		 */
		write_file = NULL;

		if (vfs_open(
				"/double.txt",
				VFS_OPEN_WRITE,
				&write_file) != 0)
		{
			printf(
				"EXT2-W1: failed opening file for restore\n");

			halt_forever();
		}

		written = 0;

		if (vfs_write(
				write_file,
				original,
				original_read,
				&written) != 0 ||
			written != original_read)
		{
			printf(
				"EXT2-W1: restore failed\n");

			vfs_close(write_file);
			halt_forever();
		}

		vfs_close(write_file);

		printf(
			"EXT2-W1: existing-file overwrite passed\n");
	}

	yield_forever();
}
