#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/logger.h>
#include <kernel/multiboot.h>
#include <kernel/framebuffer.h>
#include <kernel/display.h>
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
#include <kernel/mouse.h>
#include <kernel/mouse_cursor.h>
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
	extern bool mouse_cursor_initialize(void);
	extern void mouse_cursor_poll(void);

	if (!mouse_cursor_initialize())
	{
		log_error(
			"mouse cursor initialization failed\n");
	}

	for (;;)
	{
		mouse_cursor_poll();
		task_yield();
	}
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

static void debugcon_putc(char c)
{
	__asm__ volatile(
		"outb %0, %1"
		:
		: "a"((uint8_t)c),
		  "Nd"((uint16_t)0xE9));
}

static void debugcon_write(const char *text)
{
	while (*text != '\0')
	{
		debugcon_putc(*text);
		++text;
	}
}

static void debugcon_hex32(uint32_t value)
{
	static const char digits[] =
		"0123456789ABCDEF";

	for (int shift = 28;
		 shift >= 0;
		 shift -= 4)
	{
		debugcon_putc(
			digits[(value >> shift) &
				   0xFu]);
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

void kernel_main(
	uint32_t multiboot_magic,
	uint32_t multiboot_info_address)
{
	/*
	 * ------------------------------------------------------------
	 * Early boot terminal
	 * ------------------------------------------------------------
	 *
	 * Keep the legacy VGA terminal available during the earliest
	 * boot stages.
	 *
	 * Once paging, framebuffer mapping and the runtime display driver
	 * are ready, the existing terminal API switches to framebuffer
	 * rendering.
	 */
	terminal_initialize();

	debugcon_write(
		"\nMEATY: entered kernel_main\n");

	debugcon_write(
		"MEATY: multiboot magic = 0x");

	debugcon_hex32(
		multiboot_magic);

	debugcon_write("\n");

	debugcon_write(
		"MEATY: mbi = 0x");

	debugcon_hex32(
		multiboot_info_address);

	debugcon_write("\n");

	/*
	 * ------------------------------------------------------------
	 * Validate Multiboot entry
	 * ------------------------------------------------------------
	 */

	validate_multiboot_magic(
		multiboot_magic);

	debugcon_write(
		"MEATY: multiboot magic OK\n");

	/*
	 * ------------------------------------------------------------
	 * Copy boot framebuffer information
	 * ------------------------------------------------------------
	 *
	 * This must happen before paging changes how we access the
	 * Multiboot information structure.
	 *
	 * Multiboot only supplies our bootstrap graphics mode.
	 * It no longer determines the final runtime display resolution.
	 */

	framebuffer_boot_info_t
		boot_framebuffer;

	memset(
		&boot_framebuffer,
		0,
		sizeof(boot_framebuffer));

	if (!framebuffer_read_multiboot(
			multiboot_info_address,
			&boot_framebuffer))
	{
		debugcon_write(
			"MEATY: framebuffer metadata FAILED\n");

		halt_forever();
	}

	debugcon_write(
		"MEATY: framebuffer metadata OK\n");

	debugcon_write(
		"MEATY: boot framebuffer width = 0x");

	debugcon_hex32(
		boot_framebuffer.width);

	debugcon_write("\n");

	debugcon_write(
		"MEATY: boot framebuffer height = 0x");

	debugcon_hex32(
		boot_framebuffer.height);

	debugcon_write("\n");

	debugcon_write(
		"MEATY: boot framebuffer pitch = 0x");

	debugcon_hex32(
		boot_framebuffer.pitch);

	debugcon_write("\n");

	/*
	 * ------------------------------------------------------------
	 * Physical memory
	 * ------------------------------------------------------------
	 */

	pmm_initialize(
		multiboot_info_address);

	debugcon_write(
		"MEATY: PMM initialized\n");

	/*
	 * ------------------------------------------------------------
	 * Core x86 architecture
	 * ------------------------------------------------------------
	 */

	gdt_initialize();

	debugcon_write(
		"MEATY: GDT initialized\n");

	idt_initialize();

	debugcon_write(
		"MEATY: IDT initialized\n");

	interrupt_initialization();

	debugcon_write(
		"MEATY: interrupt subsystem initialized\n");

	/*
	 * ------------------------------------------------------------
	 * Syscalls
	 * ------------------------------------------------------------
	 */

	if (!syscall_initialize())
	{
		debugcon_write(
			"MEATY: syscall initialization FAILED\n");

		halt_forever();
	}

	debugcon_write(
		"MEATY: syscall gate initialized\n");

	/*
	 * ------------------------------------------------------------
	 * PIC
	 * ------------------------------------------------------------
	 */

	pic_initialize();

	debugcon_write(
		"MEATY: PIC initialized\n");

	/*
	 * ------------------------------------------------------------
	 * Paging
	 * ------------------------------------------------------------
	 */

	paging_initialize();

	debugcon_write(
		"MEATY: paging enabled\n");

	/*
	 * ------------------------------------------------------------
	 * Bootstrap framebuffer mapping
	 * ------------------------------------------------------------
	 *
	 * Map the framebuffer supplied by Multiboot.
	 *
	 * framebuffer_initialize() establishes the initial permanent
	 * kernel virtual mapping and records the physical framebuffer
	 * address.
	 */

	if (!framebuffer_initialize(
			&boot_framebuffer))
	{
		debugcon_write(
			"MEATY: framebuffer initialization FAILED\n");

		halt_forever();
	}

	debugcon_write(
		"MEATY: bootstrap framebuffer mapped\n");

	/*
	 * ------------------------------------------------------------
	 * Runtime display driver
	 * ------------------------------------------------------------
	 *
	 * Detect the QEMU/Bochs VBE interface and expand the framebuffer
	 * mapping to cover the device's complete video-memory aperture.
	 *
	 * From this point onward Multiboot no longer controls the current
	 * resolution.
	 */

	if (!display_initialize())
	{
		debugcon_write(
			"MEATY: runtime display driver FAILED\n");

		halt_forever();
	}

	debugcon_write(
		"MEATY: runtime display driver initialized\n");

	/*
	 * Read and print device capabilities before changing mode.
	 */

	display_capabilities_t
		display_capabilities;

	memset(
		&display_capabilities,
		0,
		sizeof(display_capabilities));

	if (!display_get_capabilities(
			&display_capabilities))
	{
		debugcon_write(
			"MEATY: display capability query FAILED\n");

		halt_forever();
	}

	debugcon_write(
		"MEATY: max display width = 0x");

	debugcon_hex32(
		display_capabilities.max_width);

	debugcon_write("\n");

	debugcon_write(
		"MEATY: max display height = 0x");

	debugcon_hex32(
		display_capabilities.max_height);

	debugcon_write("\n");

	debugcon_write(
		"MEATY: video RAM bytes = 0x");

	debugcon_hex32(
		(uint32_t)
			display_capabilities
				.video_memory_bytes);

	debugcon_write("\n");

	/*
	 * ------------------------------------------------------------
	 * Select runtime mode
	 * ------------------------------------------------------------
	 *
	 * QEMU standard VGA / Bochs VBE requires X resolution to be a
	 * multiple of 8.
	 *
	 * Therefore:
	 *
	 *     1366 -> 1360
	 *
	 * This is now a runtime request rather than a Multiboot-header
	 * constant.
	 *
	 * Later this can be called again from a kernel API or syscall.
	 */

	display_mode_t
		active_display_mode;

	memset(
		&active_display_mode,
		0,
		sizeof(active_display_mode));

	if (!display_set_mode(
			1360u,
			768u,
			32u,
			&active_display_mode))
	{
		debugcon_write(
			"MEATY: runtime mode switch FAILED\n");

		halt_forever();
	}

	debugcon_write(
		"MEATY: runtime display mode accepted\n");

	debugcon_write(
		"MEATY: active width = 0x");

	debugcon_hex32(
		active_display_mode.width);

	debugcon_write("\n");

	debugcon_write(
		"MEATY: active height = 0x");

	debugcon_hex32(
		active_display_mode.height);

	debugcon_write("\n");

	debugcon_write(
		"MEATY: active pitch = 0x");

	debugcon_hex32(
		active_display_mode.pitch);

	debugcon_write("\n");

	debugcon_write(
		"MEATY: active bpp = 0x");

	debugcon_hex32(
		active_display_mode.bpp);

	debugcon_write("\n");

	/*
	 * ------------------------------------------------------------
	 * Switch terminal to framebuffer
	 * ------------------------------------------------------------
	 *
	 * terminal_enable_framebuffer() reads the new framebuffer width
	 * and height and calculates terminal rows/columns dynamically.
	 */

	if (!terminal_enable_framebuffer())
	{
		debugcon_write(
			"MEATY: framebuffer terminal FAILED\n");

		halt_forever();
	}

	debugcon_write(
		"MEATY: framebuffer terminal enabled\n");

	/*
	 * From here on, normal kernel logging is visible in the graphical
	 * framebuffer terminal.
	 */

	log_success(
		"runtime display initialized: %ux%u %u-bpp pitch=%u\n",
		(unsigned)
			active_display_mode.width,
		(unsigned)
			active_display_mode.height,
		(unsigned)
			active_display_mode.bpp,
		(unsigned)
			active_display_mode.pitch);

	/*
	 * ------------------------------------------------------------
	 * Heap
	 * ------------------------------------------------------------
	 */

	heap_initialize();

	log_info(
		"heap initialized\n");

	/*
	 * ------------------------------------------------------------
	 * Address spaces
	 * ------------------------------------------------------------
	 */

	if (!address_space_initialize())
	{
		log_error(
			"address-space initialization failed\n");

		halt_forever();
	}

	log_info(
		"address-space subsystem initialized\n");

	/*
	 * ------------------------------------------------------------
	 * ACPI
	 * ------------------------------------------------------------
	 */

	if (!acpi_initialize())
	{
		log_error(
			"ACPI initialization failed\n");

		halt_forever();
	}

	log_info(
		"ACPI initialized\n");

	/*
	 * ------------------------------------------------------------
	 * SMP detection
	 * ------------------------------------------------------------
	 */

	if (!smp_detect_cpus())
	{
		log_error(
			"SMP CPU detection failed\n");

		halt_forever();
	}

	log_info(
		"SMP CPU detection completed\n");

	/*
	 * ------------------------------------------------------------
	 * BSP CPU-local state
	 * ------------------------------------------------------------
	 */

	cpu_local_initialize();

	log_info(
		"BSP CPU-local state initialized\n");

	cpu_local_t *bsp_cpu =
		cpu_current();

	if (bsp_cpu == NULL)
	{
		log_error(
			"Failed to resolve BSP CPU-local state\n");

		halt_forever();
	}

	/*
	 * ------------------------------------------------------------
	 * BSP TSS
	 * ------------------------------------------------------------
	 */

	if (!gdt_load_tss(
			bsp_cpu->index))
	{
		log_error(
			"U1: failed to load BSP TSS\n");

		halt_forever();
	}

	log_info(
		"U1: BSP TSS loaded for CPU %u\n",
		(unsigned)
			bsp_cpu->index);

	/*
	 * ------------------------------------------------------------
	 * Scheduler
	 * ------------------------------------------------------------
	 */

	scheduler_initialize();

	log_info(
		"scheduler initialized\n");

	/*
	 * ------------------------------------------------------------
	 * Task subsystem
	 * ------------------------------------------------------------
	 */

	task_initialize();

	log_info(
		"BSP task system initialized\n");

	/*
	 * ------------------------------------------------------------
	 * Process subsystem
	 * ------------------------------------------------------------
	 */

	process_initialize();

	log_info(
		"process system initialized\n");

	/*
	 * ------------------------------------------------------------
	 * Sleep queues
	 * ------------------------------------------------------------
	 */

	sleep_queue_initialize();

	log_info(
		"sleep queue initialized\n");

	/*
	 * ------------------------------------------------------------
	 * VFS
	 * ------------------------------------------------------------
	 */

	vfs_initialize();

	log_info(
		"VFS initialized\n");

	/*
	 * ------------------------------------------------------------
	 * AHCI
	 * ------------------------------------------------------------
	 */

	pci_device_t
		ahci_controller;

	if (pci_find_ahci_controller(
			&ahci_controller))
	{
		if (!ahci_probe(
				&ahci_controller))
		{
			log_error(
				"AHCI: probe failed\n");
		}
	}
	else
	{
		log_info(
			"AHCI: no controller detected\n");
	}

	/*
	 * ------------------------------------------------------------
	 * Primary disk
	 * ------------------------------------------------------------
	 */

	block_device_t *disk =
		ahci_primary_disk();

	if (disk == NULL)
	{
		log_error(
			"Storage: no AHCI disk\n");

		halt_forever();
	}

	/*
	 * ------------------------------------------------------------
	 * Partition discovery
	 * ------------------------------------------------------------
	 */

	static partition_device_t
		partitions[8];

	size_t partition_count =
		partition_scan(
			disk,
			partitions,
			8);

	if (partition_count == 0)
	{
		log_error(
			"Storage: no usable partitions\n");

		halt_forever();
	}

	/*
	 * ------------------------------------------------------------
	 * Existing disk write/read verification
	 * ------------------------------------------------------------
	 */

	{
		block_device_t *partition =
			&partitions[0].block;

		uint8_t original[512];
		uint8_t test_data[512];
		uint8_t verify[512];

		const uint64_t test_lba =
			100u;

		if (block_read(
				partition,
				test_lba,
				1,
				original) != 0)
		{
			log_error(
				"Partition: initial write-test read failed\n");

			halt_forever();
		}

		for (size_t index = 0;
			 index < sizeof(test_data);
			 ++index)
		{
			test_data[index] =
				(uint8_t)((index * 29u) ^
						  0x5Au);
		}

		if (block_write(
				partition,
				test_lba,
				1,
				test_data) != 0)
		{
			log_error(
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
			log_error(
				"Partition: verify read failed\n");

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
			log_error(
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
			log_error(
				"Partition: failed restoring test sector\n");

			halt_forever();
		}

		log_info(
			"Partition: write/read test passed\n");
	}

	/*
	 * ------------------------------------------------------------
	 * EXT2
	 * ------------------------------------------------------------
	 */

	static ext2_fs_t
		ext2_fs;

	if (!ext2_mount(
			&partitions[0].block,
			&ext2_fs))
	{
		log_error(
			"EXT2: mount failed\n");

		halt_forever();
	}

	if (!vfs_set_root(
			&ext2_fs.root_vnode))
	{
		log_error(
			"EXT2: failed to set VFS root\n");

		halt_forever();
	}

	log_info(
		"EXT2: mounted as /\n");

	/*
	 * ------------------------------------------------------------
	 * Start APs
	 * ------------------------------------------------------------
	 *
	 * Existing scheduling rule remains unchanged:
	 *
	 * normal userspace tasks stay BSP-only until scheduler migration
	 * becomes safe.
	 */

	if (!smp_start_aps())
	{
		log_error(
			"AP startup failed\n");

		halt_forever();
	}

	log_info(
		"SMP online CPUs: %u\n",
		(unsigned)
			smp_online_cpu_count());

	/*
	 * ------------------------------------------------------------
	 * PIT
	 * ------------------------------------------------------------
	 */

	if (pit_initialize(
			PIT_DEFAULT_FREQUENCY_HZ) != 0)
	{
		log_error(
			"PIT initialization failed\n");

		halt_forever();
	}

	log_info(
		"PIT initialized\n");

	/*
	 * ------------------------------------------------------------
	 * PS/2 mouse
	 * ------------------------------------------------------------
	 *
	 * Initialize the auxiliary i8042 device while global CPU
	 * interrupts are still disabled.
	 *
	 * The mouse owns IRQ12.
	 */
	if (!mouse_initialize())
	{
		log_error(
			"mouse initialization failed\n");

		halt_forever();
	}

	log_info(
		"PS/2 mouse initialized on IRQ12\n");

	/*
	 * ------------------------------------------------------------
	 * Keyboard
	 * ------------------------------------------------------------
	 *
	 * Keyboard remains on IRQ1.
	 */
	if (!keyboard_initialize())
	{
		log_error(
			"keyboard initialization failed\n");

		halt_forever();
	}

	log_info(
		"keyboard initialized\n");

	/*
	 * ------------------------------------------------------------
	 * Scheduling + interrupts
	 * ------------------------------------------------------------
	 *
	 * Keep normal task scheduling on the BSP.
	 */

	scheduler_enable_preemption();

	interrupt_enable();

	/*
	 * ------------------------------------------------------------
	 * Userspace shell
	 * ------------------------------------------------------------
	 *
	 * stdout/stderr already flow through terminal_write(), so the
	 * existing shell automatically renders through the framebuffer.
	 */

	launch_userspace_shell();

	/*
	 * kernel_main's bootstrap execution context is finished.
	 */

	yield_forever();
}
