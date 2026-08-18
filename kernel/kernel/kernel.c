#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/logger.h>
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
#include <kernel/elf.h>
#include <kernel/user_image.h>

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

#define U10_USER_STACK_ADDRESS 0xBFFFF000u
#define U10_USER_STACK_TOP 0xC0000000u

#define U10_EXECUTABLE_PATH "/bin/hello.nex"

/*
 * Keep the initial whole-file ELF buffering deliberately bounded.
 *
 * Streaming ELF loading can come later.  One MiB is vastly larger
 * than the current hello.nex while preventing an invalid filesystem
 * size from causing an uncontrolled kernel allocation.
 */
#define U10_MAX_EXECUTABLE_SIZE (1024u * 1024u)

extern void arch_enter_user(
	uintptr_t instruction_pointer,
	uintptr_t stack_pointer)
	__attribute__((noreturn));

typedef struct
{
	uintptr_t entry;
	uintptr_t stack_top;
} user_launch_context_t;

static user_image_t u10_user_image;
static user_launch_context_t u10_launch_context;

static void u10_user_task_entry(void *argument)
	__attribute__((noreturn));

static void u10_user_task_entry(void *argument)
{
	user_launch_context_t *launch =
		(user_launch_context_t *)argument;

	if (launch == NULL ||
		launch->entry == 0 ||
		launch->stack_top == 0)
	{
		printf(
			"U10: invalid ELF launch context\n");

		halt_forever();
	}

	task_t *task =
		task_current();

	if (task == NULL)
	{
		printf(
			"U10: no current user task\n");

		halt_forever();
	}

	uintptr_t actual_directory =
		paging_current_directory();

	if (!task->owns_page_directory ||
		actual_directory !=
			task->page_directory ||
		actual_directory ==
			paging_kernel_directory())
	{
		printf(
			"U10: invalid user address space\n");

		halt_forever();
	}

	printf(
		"U10: entering ELF at 0x%lx stack=0x%lx\n",
		(unsigned long)
			launch->entry,
		(unsigned long)
			launch->stack_top);

	arch_enter_user(
		launch->entry,
		launch->stack_top);
}

static void u10_argv_test(void)
{
	printf(
		"\n=== U10 USERSPACE ARGV TEST ===\n");

	uintptr_t kernel_directory =
		paging_kernel_directory();

	if (kernel_directory == 0 ||
		paging_current_directory() !=
			kernel_directory)
	{
		printf(
			"U10: kernel CR3 is not active\n");

		halt_forever();
	}

	file_t *file = NULL;

	if (vfs_open(
			U10_EXECUTABLE_PATH,
			VFS_OPEN_READ,
			&file) != 0 ||
		file == NULL)
	{
		printf(
			"U10: failed opening %s\n",
			U10_EXECUTABLE_PATH);

		halt_forever();
	}

	printf(
		"U10: opened %s\n",
		U10_EXECUTABLE_PATH);

	if (file->vnode == NULL ||
		file->vnode->type !=
			VNODE_REGULAR)
	{
		printf(
			"U10: executable vnode is not a regular file\n");

		vfs_close(file);
		halt_forever();
	}

	uint64_t executable_size_64 =
		file->vnode->size;

	if (executable_size_64 == 0)
	{
		printf(
			"U10: executable is empty\n");

		vfs_close(file);
		halt_forever();
	}

	if (executable_size_64 >
			(uint64_t)SIZE_MAX ||
		executable_size_64 >
			U10_MAX_EXECUTABLE_SIZE)
	{
		printf(
			"U10: executable size is invalid: %llu bytes\n",
			(unsigned long long)
				executable_size_64);

		vfs_close(file);
		halt_forever();
	}

	size_t executable_size =
		(size_t)executable_size_64;

	uint8_t *executable_data =
		kmalloc(executable_size);

	if (executable_data == NULL)
	{
		printf(
			"U10: failed allocating executable buffer\n");

		vfs_close(file);
		halt_forever();
	}

	size_t total_read = 0;

	while (total_read <
		   executable_size)
	{
		size_t bytes_read = 0;

		if (vfs_read(
				file,
				executable_data +
					total_read,
				executable_size -
					total_read,
				&bytes_read) != 0)
		{
			printf(
				"U10: VFS read failed at offset %lu\n",
				(unsigned long)
					total_read);

			kfree(
				executable_data);

			vfs_close(
				file);

			halt_forever();
		}

		/*
		 * Successful zero-byte read before reaching the vnode's
		 * advertised size means the file was truncated or the
		 * filesystem returned inconsistent data.
		 */
		if (bytes_read == 0)
		{
			printf(
				"U10: unexpected EOF at %lu of %lu bytes\n",
				(unsigned long)
					total_read,
				(unsigned long)
					executable_size);

			kfree(
				executable_data);

			vfs_close(
				file);

			halt_forever();
		}

		if (bytes_read >
			executable_size -
				total_read)
		{
			printf(
				"U10: VFS returned an invalid read length\n");

			kfree(
				executable_data);

			vfs_close(
				file);

			halt_forever();
		}

		total_read +=
			bytes_read;
	}

	vfs_close(file);
	file = NULL;

	if (total_read !=
		executable_size)
	{
		printf(
			"U10: executable read length mismatch\n");

		kfree(
			executable_data);

		halt_forever();
	}

	printf(
		"U10: loaded %lu bytes from VFS\n",
		(unsigned long)
			executable_size);

	/*
	 * U10 initial userspace argument vector.
	 *
	 * These strings are copied into the new private userspace stack
	 * by elf_load_user_image(); userspace never receives pointers to
	 * these kernel strings.
	 */
	static const char *const user_argv[] =
		{
			U10_EXECUTABLE_PATH,
			"one",
			"two"};

	const size_t user_argc =
		sizeof(user_argv) /
		sizeof(user_argv[0]);

	printf(
		"U10: launching %s argc=%lu\n",
		U10_EXECUTABLE_PATH,
		(unsigned long)
			user_argc);

	size_t live_before =
		task_live_count();

	uint64_t reaped_before =
		task_cleanup_total_reaped();

	if (!elf_load_user_image(
			&u10_user_image,
			executable_data,
			executable_size,
			U10_USER_STACK_ADDRESS,
			U10_USER_STACK_TOP,
			user_argc,
			user_argv))
	{
		printf(
			"U10: ELF loader rejected %s\n",
			U10_EXECUTABLE_PATH);

		kfree(
			executable_data);

		halt_forever();
	}

	/*
	 * The ELF loader copied all PT_LOAD contents and argv strings
	 * into frames owned by the prepared user image.
	 *
	 * The filesystem source buffer is no longer required.
	 */
	kfree(
		executable_data);

	executable_data = NULL;

	if (u10_user_image.stack_top <
			U10_USER_STACK_ADDRESS ||
		u10_user_image.stack_top >=
			U10_USER_STACK_TOP)
	{
		printf(
			"U10: invalid prepared initial user ESP 0x%lx\n",
			(unsigned long)
				u10_user_image.stack_top);

		user_image_destroy(
			&u10_user_image);

		halt_forever();
	}

	printf(
		"U10: ELF loaded entry=0x%lx CR3=0x%lx ESP=0x%lx\n",
		(unsigned long)
			u10_user_image.entry,
		(unsigned long)
			u10_user_image.page_directory,
		(unsigned long)
			u10_user_image.stack_top);

	u10_launch_context.entry =
		u10_user_image.entry;

	u10_launch_context.stack_top =
		u10_user_image.stack_top;

	uintptr_t user_directory =
		u10_user_image.page_directory;

	task_t *task =
		task_create_user_with_policy(
			u10_user_task_entry,
			&u10_launch_context,
			user_directory,
			SCHED_POLICY_REALTIME);

	if (task == NULL)
	{
		user_image_destroy(
			&u10_user_image);

		printf(
			"U10: failed creating ELF user task\n");

		halt_forever();
	}

	if (!task->owns_page_directory ||
		task->page_directory !=
			user_directory)
	{
		printf(
			"U10: user task did not accept ELF CR3\n");

		halt_forever();
	}

	uintptr_t detached_directory =
		user_image_detach_directory(
			&u10_user_image);

	if (detached_directory !=
		user_directory)
	{
		printf(
			"U10: ELF address-space ownership transfer failed\n");

		halt_forever();
	}

	task_id_t user_tid =
		task->id;

	printf(
		"U10: starting %s as task %u\n",
		U10_EXECUTABLE_PATH,
		(unsigned)user_tid);

	/*
	 * hello.nex should now observe:
	 *
	 *     argc == 3
	 *
	 *     argv[0] == "/bin/hello.nex"
	 *     argv[1] == "one"
	 *     argv[2] == "two"
	 *     argv[3] == NULL
	 *
	 * _start converts the initial process stack into normal cdecl
	 * arguments for main(int argc, char **argv).
	 */
	task_yield();

	/*
	 * The task may already have been destroyed by the reaper.
	 * Do not dereference task after this point.
	 */
	for (size_t i = 0;
		 i < 64u;
		 ++i)
	{
		if (task_cleanup_total_reaped() >=
				reaped_before + 1u &&
			task_cleanup_pending_count() == 0)
		{
			break;
		}

		task_yield();
	}

	uint64_t reaped_after =
		task_cleanup_total_reaped();

	size_t live_after =
		task_live_count();

	size_t pending_after =
		task_cleanup_pending_count();

	if (reaped_after !=
		reaped_before + 1u)
	{
		printf(
			"U10: ELF task was not reaped exactly once\n");

		halt_forever();
	}

	if (pending_after != 0)
	{
		printf(
			"U10: ELF task cleanup did not finish\n");

		halt_forever();
	}

	if (live_after !=
		live_before)
	{
		printf(
			"U10: ELF task lifecycle leaked a task\n");

		halt_forever();
	}

	printf(
		"U10: ELF task exited and was reaped\n");

	printf(
		"U10: argc/argv userspace ABI confirmed\n");

	halt_forever();
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

	log_info("U2: syscall gate initialized\n");

	pic_initialize();
	log_info("pic initialized\n");

	paging_initialize();
	log_info("paging initialized\n");

	heap_initialize();
	log_info("heap initialized\n");

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

	u10_argv_test();


	/*
	 * Allow hardware IRQ delivery first.
	 *
	 * Timer accounting, sleep wakeups, keyboard IRQs, PIC EOIs,
	 * etc. can all run here.
	 *
	 * Actual scheduler context switching is still gated on this CPU.
	 */
	interrupt_enable();

	yield_forever();
}