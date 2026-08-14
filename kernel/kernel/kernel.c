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
		uint32_t block_size =
			1024u << ext2_fs.superblock.log_block_size;

		uint32_t entries_per_block =
			block_size /
			sizeof(uint32_t);

		/*
		 * End two data blocks inside the double-indirect region.
		 *
		 * Layout:
		 *
		 * 12 direct blocks
		 * N single-indirect blocks
		 * then double-indirect blocks
		 */
		size_t target_size =
			((size_t)12u +
			 entries_per_block +
			 2u) *
				block_size +
			123u;

		file_t *file = NULL;

		if (vfs_open(
				"/grow.txt",
				VFS_OPEN_WRITE,
				&file) != 0)
		{
			printf(
				"EXT2-W5: failed opening /grow.txt\n");

			halt_forever();
		}

		size_t old_size =
			file->vnode->size;

		if (old_size >= target_size)
		{
			printf(
				"EXT2-W5: /grow.txt already beyond test target\n");

			vfs_close(file);
			halt_forever();
		}

		size_t test_size =
			target_size -
			old_size;

		uint8_t *test_data =
			kmalloc(test_size);

		uint8_t *verify =
			kmalloc(test_size);

		if (test_data == NULL ||
			verify == NULL)
		{
			printf(
				"EXT2-W5: buffer allocation failed\n");

			vfs_close(file);
			halt_forever();
		}

		for (size_t i = 0;
			 i < test_size;
			 i++)
		{
			test_data[i] =
				(uint8_t)((i * 47u) ^
						  (i >> 3) ^
						  0x6Du);
		}

		file->offset =
			old_size;

		size_t written = 0;

		uint64_t write_start_ms =
			timer_uptime_ms();

		int write_result =
			vfs_write(
				file,
				test_data,
				test_size,
				&written);

		uint64_t write_end_ms =
			timer_uptime_ms();

		uint64_t write_elapsed_ms =
			write_end_ms -
			write_start_ms;

		if (write_result != 0 ||
			written != test_size)
		{
			printf(
				"EXT2-W5: indirect growth write FAILED\n");

			vfs_close(file);
			halt_forever();
		}

		size_t new_size =
			file->vnode->size;

		vfs_close(file);

		if (new_size != target_size)
		{
			printf(
				"EXT2-W5: size update FAILED\n");

			halt_forever();
		}

		/*
		 * Reopen to force inode information to be loaded
		 * from the filesystem again.
		 */
		file = NULL;

		if (vfs_open(
				"/grow.txt",
				VFS_OPEN_READ,
				&file) != 0)
		{
			printf(
				"EXT2-W5: reopen failed\n");

			halt_forever();
		}

		file->offset =
			old_size;

		size_t total_read =
			0;

		while (total_read <
			   test_size)
		{
			size_t chunk_read =
				0;

			size_t remaining =
				test_size -
				total_read;

			if (vfs_read(
					file,
					verify +
						total_read,
					remaining,
					&chunk_read) != 0)
			{
				printf(
					"EXT2-W5: verification read FAILED\n");

				vfs_close(file);
				halt_forever();
			}

			if (chunk_read == 0)
			{
				printf(
					"EXT2-W5: unexpected EOF\n");

				vfs_close(file);
				halt_forever();
			}

			total_read +=
				chunk_read;
		}

		vfs_close(file);

		if (memcmp(
				test_data,
				verify,
				test_size) != 0)
		{
			printf(
				"EXT2-W5: data verification FAILED\n");

			halt_forever();
		}

		printf(
			"EXT2-W5: single indirect PASSED\n");

		printf(
			"EXT2-W5: double indirect PASSED\n");

		printf(
			"EXT2-W5: size %u -> %u\n",
			(unsigned)old_size,
			(unsigned)new_size);

		printf(
			"EXT2-W5: bytes written = %u\n",
			(unsigned)written);

		printf(
			"EXT2-W5: write time = %u ms\n",
			(unsigned)write_elapsed_ms);

		kfree(verify);
		kfree(test_data);
	}

	yield_forever();
}
