#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

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

	if (!ata_initialize())
	{
		printf("ATA initialization failed\n");
	}
	else
	{
		printf("ATA initialized\n");

		block_device_t *disk = ata_primary_master();

		if (disk == NULL)
		{
			printf("Storage: no ATA disk\n");
			halt_forever();
		}

		static partition_device_t partitions[8];

		size_t partition_count = partition_scan(disk, partitions, 8);

		printf("Storage: found %u partitions\n", (unsigned)partition_count);

		if (partition_count == 0)
		{
			printf("Storage: no usable partitions\n");
			halt_forever();
		}

		static ext2_fs_t ext2_fs;

		if (!ext2_mount(&partitions[0].block, &ext2_fs))
		{
			printf("EXT2: mount failed\n");
			halt_forever();
		}

		if (!vfs_set_root(&ext2_fs.root_vnode))
		{
			printf("EXT2: failed to set VFS root\n");
			halt_forever();
		}

		printf("EXT2: mounted as /\n");

		file_t *file = NULL;
		if (vfs_open("/hello.txt", VFS_OPEN_READ, &file) != 0)
		{
			printf("VFS: failed opening /hello.txt\n");
			halt_forever();
		}
		char buffer[128];
		size_t bytes_read = 0;
		if (vfs_read(file, buffer, sizeof(buffer) - 1, &bytes_read) != 0)
		{
			printf("VFS: failed reading /hello.txt\n");
			vfs_close(file);
			halt_forever();
		}
		buffer[bytes_read] = '\0';
		printf("VFS: /hello.txt = %s\n", buffer);
		vfs_close(file);

		file = NULL;
		if (vfs_open("/big.txt", VFS_OPEN_READ, &file) != 0)
		{
			printf("VFS: failed opening /big.txt\n");
			halt_forever();
		}
		char big_buffer[4096];
		size_t total_big_read = 0;
		for (;;)
		{
			size_t chunk_read = 0;

			if (vfs_read(file, big_buffer, sizeof(big_buffer), &chunk_read) != 0)
			{
				printf("VFS: failed reading /big.txt\n");
				vfs_close(file);
				halt_forever();
			}

			if (chunk_read == 0)
				break;

			total_big_read += chunk_read;
		}
		printf("VFS: /big.txt bytes read = %u\n", (unsigned)total_big_read);
		vfs_close(file);
	}

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
