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

/*
 * ==========================================================================
 * U12.3A USER STACK SLOT ALLOCATOR TEST
 * ==========================================================================
 *
 * This test intentionally does NOT allocate physical stack pages.
 *
 * It proves only that one shared userspace address space can safely
 * reserve unique reusable virtual stack regions.
 */
static void u12_user_stack_slot_test(void)
{
    printf(
        "\n=== U12.3A USER STACK SLOT TEST ===\n");

    uintptr_t kernel_directory =
        paging_kernel_directory();

    if (kernel_directory == 0 ||
        paging_current_directory() !=
            kernel_directory)
    {
        log_error(
            "U12.3A: canonical kernel CR3 is not active\n");

        halt_forever();
    }

    /*
     * --------------------------------------------------------------
     * STEP 1
     *
     * Create an otherwise-empty userspace address space.
     * --------------------------------------------------------------
     */
    uintptr_t directory =
        0;

    if (!paging_create_user_directory(
            &directory))
    {
        log_error(
            "U12.3A: failed creating user directory\n");

        halt_forever();
    }

    address_space_t *space =
        address_space_adopt_user(
            directory);

    if (space == NULL)
    {
        log_error(
            "U12.3A: failed adopting user address space\n");

        paging_destroy_user_directory(
            directory);

        halt_forever();
    }


    /*
     * --------------------------------------------------------------
     * STEP 2
     *
     * Explicitly reserve slot 0.
     *
     * This simulates the already-existing main-thread stack:
     *
     *     0xBFF00000 - 0xC0000000
     *
     * We are NOT mapping it in this test.
     * --------------------------------------------------------------
     */
    address_space_user_stack_slot_t main_slot;

    if (!address_space_user_stack_slot_reserve_index(
            space,
            0u,
            &main_slot))
    {
        log_error(
            "U12.3A: failed reserving main stack slot\n");

        halt_forever();
    }

    if (main_slot.index != 0u ||
        main_slot.stack_top !=
            ADDRESS_SPACE_USER_STACK_REGION_TOP ||
        main_slot.stack_top -
            main_slot.stack_bottom !=
                ADDRESS_SPACE_USER_STACK_SIZE ||
        main_slot.guard_top !=
            main_slot.stack_bottom ||
        main_slot.guard_top -
            main_slot.guard_bottom !=
                ADDRESS_SPACE_USER_STACK_GUARD_SIZE)
    {
        log_error(
            "U12.3A: main stack slot layout invalid\n");

        halt_forever();
    }

    log_success(
        "U12.3A: slot %lu "
        "stack=[0x%lx,0x%lx) "
        "guard=[0x%lx,0x%lx)\n",
        (unsigned long)main_slot.index,
        (unsigned long)main_slot.stack_bottom,
        (unsigned long)main_slot.stack_top,
        (unsigned long)main_slot.guard_bottom,
        (unsigned long)main_slot.guard_top);


    /*
     * Reserving the same explicit slot twice must fail.
     */
    address_space_user_stack_slot_t duplicate_slot;

    if (address_space_user_stack_slot_reserve_index(
            space,
            0u,
            &duplicate_slot))
    {
        log_error(
            "U12.3A: duplicate reservation of slot 0 succeeded\n");

        halt_forever();
    }


    /*
     * --------------------------------------------------------------
     * STEP 3
     *
     * Allocate two worker-thread slots automatically.
     *
     * Since slot 0 is occupied, they must become:
     *
     *     worker A -> slot 1
     *     worker B -> slot 2
     * --------------------------------------------------------------
     */
    address_space_user_stack_slot_t worker_a;

    address_space_user_stack_slot_t worker_b;

    if (!address_space_user_stack_slot_reserve(
            space,
            &worker_a))
    {
        log_error(
            "U12.3A: failed allocating worker A stack slot\n");

        halt_forever();
    }

    if (!address_space_user_stack_slot_reserve(
            space,
            &worker_b))
    {
        log_error(
            "U12.3A: failed allocating worker B stack slot\n");

        halt_forever();
    }

    if (worker_a.index != 1u ||
        worker_b.index != 2u)
    {
        log_error(
            "U12.3A: unexpected automatic slots "
            "A=%lu B=%lu\n",
            (unsigned long)worker_a.index,
            (unsigned long)worker_b.index);

        halt_forever();
    }


    /*
     * Stack slots descend through userspace with exactly one
     * guard page separating neighboring stacks.
     *
     * Therefore:
     *
     *     slot 0 guard bottom == slot 1 stack top
     *
     * and:
     *
     *     slot 1 guard bottom == slot 2 stack top
     */
    if (main_slot.guard_bottom !=
            worker_a.stack_top ||
        worker_a.guard_bottom !=
            worker_b.stack_top)
    {
        log_error(
            "U12.3A: neighboring slot layout overlaps/gaps incorrectly\n");

        halt_forever();
    }


    size_t reserved =
        address_space_user_stack_slot_reserved_count(
            space);

    if (reserved != 3u)
    {
        log_error(
            "U12.3A: expected 3 reserved slots, got %lu\n",
            (unsigned long)reserved);

        halt_forever();
    }

    log_success(
        "U12.3A: worker slots A=%lu B=%lu "
        "reserved=%lu\n",
        (unsigned long)worker_a.index,
        (unsigned long)worker_b.index,
        (unsigned long)reserved);


    /*
     * --------------------------------------------------------------
     * STEP 4
     *
     * Release worker A's slot.
     *
     * The next allocation should reuse the lowest free slot:
     *
     *     slot 1
     * --------------------------------------------------------------
     */
    if (!address_space_user_stack_slot_release(
            space,
            worker_a.index))
    {
        log_error(
            "U12.3A: failed releasing worker A slot\n");

        halt_forever();
    }

    reserved =
        address_space_user_stack_slot_reserved_count(
            space);

    if (reserved != 2u)
    {
        log_error(
            "U12.3A: expected 2 slots after release, got %lu\n",
            (unsigned long)reserved);

        halt_forever();
    }


    address_space_user_stack_slot_t reused;

    if (!address_space_user_stack_slot_reserve(
            space,
            &reused))
    {
        log_error(
            "U12.3A: failed reallocating released slot\n");

        halt_forever();
    }

    /*
     * It must be the exact same virtual range worker A had.
     */
    if (reused.index !=
            worker_a.index ||
        reused.stack_bottom !=
            worker_a.stack_bottom ||
        reused.stack_top !=
            worker_a.stack_top ||
        reused.guard_bottom !=
            worker_a.guard_bottom ||
        reused.guard_top !=
            worker_a.guard_top)
    {
        log_error(
            "U12.3A: released slot was not reused correctly\n");

        halt_forever();
    }

    log_success(
        "U12.3A: released slot %lu was reused "
        "at stack=[0x%lx,0x%lx)\n",
        (unsigned long)reused.index,
        (unsigned long)reused.stack_bottom,
        (unsigned long)reused.stack_top);


    /*
     * --------------------------------------------------------------
     * STEP 5
     *
     * Release all remaining reservations.
     * --------------------------------------------------------------
     */
    if (!address_space_user_stack_slot_release(
            space,
            main_slot.index) ||
        !address_space_user_stack_slot_release(
            space,
            reused.index) ||
        !address_space_user_stack_slot_release(
            space,
            worker_b.index))
    {
        log_error(
            "U12.3A: failed releasing final stack slots\n");

        halt_forever();
    }

    reserved =
        address_space_user_stack_slot_reserved_count(
            space);

    if (reserved != 0u)
    {
        log_error(
            "U12.3A: stack-slot leak count=%lu\n",
            (unsigned long)reserved);

        halt_forever();
    }


    /*
     * No slot pages were ever mapped in U12.3A.
     *
     * The final address-space reference can therefore simply
     * destroy the empty userspace page directory.
     */
    if (!address_space_release(
            space))
    {
        log_error(
            "U12.3A: final address-space release failed\n");

        halt_forever();
    }

    log_success(
        "U12.3A: unique reusable user-stack slots PASSED\n");
}

/*
 * ==========================================================================
 * U12.2 SHARED ADDRESS-SPACE LIFETIME TEST
 * ==========================================================================
 *
 * This is intentionally a kernel-controlled test.
 *
 * The two tasks execute kernel entry functions while using the same
 * userspace page directory.
 *
 * We are not creating real ring-3 threads yet.
 *
 * The purpose of this test is narrower:
 *
 *     prove that multiple task_t objects can safely reference one
 *     address_space_t, and that destroying one task does not destroy
 *     memory still required by another task.
 */

#define U12_SHARED_TEST_ADDRESS \
	((uintptr_t)0x40000000u)

#define U12_SHARED_MAGIC_A \
	((uint32_t)0xA12A12A1u)

#define U12_SHARED_MAGIC_B \
	((uint32_t)0xB12B12B2u)

/*
 * These live in kernel memory.
 *
 * They let the BSP test task observe what the two worker tasks proved.
 */
static volatile bool u12_task_a_passed =
	false;

static volatile bool u12_task_b_passed =
	false;

/*
 * --------------------------------------------------------------------------
 * TASK A
 * --------------------------------------------------------------------------
 *
 * Both tasks already exist before either is published.
 *
 * Therefore the expected address-space reference count when A runs is:
 *
 *     test owner = 1
 *     task A     = 1
 *     task B     = 1
 *                  ---
 *                  3
 *
 * Task A writes a value into an actual PAGE_USER mapping and exits.
 */
static void u12_shared_space_task_a(
	void *argument)
{
	address_space_t *space =
		(address_space_t *)argument;

	task_t *task =
		task_current();

	if (space == NULL ||
		task == NULL ||
		task->address_space != space)
	{
		log_error(
			"U12.2: task A invalid task/address space\n");

		task_exit();
	}

	if (address_space_is_kernel(
			space))
	{
		log_error(
			"U12.2: task A unexpectedly uses kernel address space\n");

		task_exit();
	}

	uintptr_t expected_directory =
		address_space_page_directory(
			space);

	uintptr_t current_directory =
		paging_current_directory();

	if (expected_directory == 0 ||
		current_directory !=
			expected_directory)
	{
		log_error(
			"U12.2: task A CR3 mismatch "
			"current=0x%lx expected=0x%lx\n",
			(unsigned long)current_directory,
			(unsigned long)expected_directory);

		task_exit();
	}

	size_t references =
		address_space_reference_count(
			space);

	if (references != 3u)
	{
		log_error(
			"U12.2: task A expected refs=3, got %lu\n",
			(unsigned long)references);

		task_exit();
	}

	/*
	 * This virtual address exists only inside the shared
	 * userspace address space.
	 *
	 * Task A writes it.
	 *
	 * Task B must still be able to read this exact value
	 * after task A has exited AND been completely reaped.
	 */
	volatile uint32_t *shared_value =
		(volatile uint32_t *)
			U12_SHARED_TEST_ADDRESS;

	*shared_value =
		U12_SHARED_MAGIC_A;

	u12_task_a_passed =
		true;

	log_success(
		"U12.2: task A tid=%u wrote shared page "
		"refs=%lu value=0x%lx\n",
		(unsigned)task->id,
		(unsigned long)references,
		(unsigned long)*shared_value);

	/*
	 * The reaper should release only A's reference.
	 *
	 * The shared address space must survive because task B
	 * and the BSP test owner still reference it.
	 */
	task_exit();
}

/*
 * --------------------------------------------------------------------------
 * TASK B
 * --------------------------------------------------------------------------
 *
 * Task B begins using the same address space as task A.
 *
 * It immediately blocks.
 *
 * The BSP will wake it only AFTER task A has been fully reaped.
 */
static void u12_shared_space_task_b(
	void *argument)
{
	address_space_t *space =
		(address_space_t *)argument;

	task_t *task =
		task_current();

	if (space == NULL ||
		task == NULL ||
		task->address_space != space)
	{
		log_error(
			"U12.2: task B invalid task/address space\n");

		task_exit();
	}

	if (address_space_is_kernel(
			space))
	{
		log_error(
			"U12.2: task B unexpectedly uses kernel address space\n");

		task_exit();
	}

	/*
	 * Wait until task A's address-space reference
	 * has disappeared.
	 *
	 * Initial ownership:
	 *
	 *     test owner = 1
	 *     task A     = 1
	 *     task B     = 1
	 *                  ---
	 *                  3
	 *
	 * After task A has been reaped:
	 *
	 *     test owner = 1
	 *     task B     = 1
	 *                  ---
	 *                  2
	 *
	 * Do NOT busy-spin.
	 *
	 * Yield so the reaper and the other CPUs can make
	 * forward progress.
	 */
	for (;;)
	{
		size_t references =
			address_space_reference_count(
				space);

		if (references == 2u)
			break;

		/*
		 * refs should never drop below 2 while task B
		 * and the test owner are both alive.
		 */
		if (references < 2u)
		{
			log_error(
				"U12.2: task B observed invalid refs=%lu\n",
				(unsigned long)references);

			task_exit();
		}

		task_yield();
	}

	/*
	 * Seeing refs == 2 proves task A's address-space
	 * reference has been released.
	 */
	size_t references =
		address_space_reference_count(
			space);

	uintptr_t expected_directory =
		address_space_page_directory(
			space);

	uintptr_t current_directory =
		paging_current_directory();

	if (expected_directory == 0 ||
		current_directory !=
			expected_directory)
	{
		log_error(
			"U12.2: task B CR3 mismatch "
			"current=0x%lx expected=0x%lx\n",
			(unsigned long)current_directory,
			(unsigned long)expected_directory);

		task_exit();
	}

	/*
	 * Task A wrote this before exiting.
	 *
	 * If A's cleanup incorrectly destroyed the shared
	 * address space, this access cannot remain valid.
	 */
	volatile uint32_t *shared_value =
		(volatile uint32_t *)
			U12_SHARED_TEST_ADDRESS;

	uint32_t value =
		*shared_value;

	if (value !=
		U12_SHARED_MAGIC_A)
	{
		log_error(
			"U12.2: task B shared value mismatch "
			"expected=0x%lx got=0x%lx\n",
			(unsigned long)U12_SHARED_MAGIC_A,
			(unsigned long)value);

		task_exit();
	}

	/*
	 * Also prove that B can still write into the
	 * surviving address space.
	 */
	*shared_value =
		U12_SHARED_MAGIC_B;

	u12_task_b_passed =
		true;

	log_success(
		"U12.2: task B tid=%u survived task A "
		"refs=%lu value=0x%lx\n",
		(unsigned)task->id,
		(unsigned long)references,
		(unsigned long)*shared_value);

	task_exit();
}

/*
 * --------------------------------------------------------------------------
 * TEST DRIVER
 * --------------------------------------------------------------------------
 */
static void u12_shared_address_space_test(void)
{
	printf(
		"\n=== U12.2 SHARED ADDRESS SPACE TEST ===\n");

	uintptr_t kernel_directory =
		paging_kernel_directory();

	/*
	 * Address-space construction and destruction currently
	 * require the canonical kernel CR3.
	 */
	if (kernel_directory == 0 ||
		paging_current_directory() !=
			kernel_directory)
	{
		log_error(
			"U12.2: canonical kernel CR3 is not active\n");

		halt_forever();
	}

	u12_task_a_passed =
		false;

	u12_task_b_passed =
		false;

	/*
	 * Record task/reaper accounting before creating
	 * anything belonging to this test.
	 */
	size_t live_before =
		task_live_count();

	uint64_t reaped_before =
		task_cleanup_total_reaped();

	/*
	 * --------------------------------------------------------------
	 * STEP 1
	 *
	 * Create one empty userspace page directory.
	 * --------------------------------------------------------------
	 */
	uintptr_t directory =
		0;

	if (!paging_create_user_directory(
			&directory))
	{
		log_error(
			"U12.2: failed creating user directory\n");

		halt_forever();
	}

	if (directory == 0 ||
		directory ==
			kernel_directory)
	{
		log_error(
			"U12.2: invalid user directory\n");

		halt_forever();
	}

	/*
	 * --------------------------------------------------------------
	 * STEP 2
	 *
	 * Create one real userspace page shared by both tasks.
	 * --------------------------------------------------------------
	 */
	uintptr_t shared_frame =
		pmm_allocate_frame();

	if (shared_frame == 0)
	{
		log_error(
			"U12.2: failed allocating shared frame\n");

		halt_forever();
	}

	if (!paging_map_page_in_directory(
			directory,
			U12_SHARED_TEST_ADDRESS,
			shared_frame,
			PAGE_USER |
				PAGE_WRITABLE))
	{
		log_error(
			"U12.2: failed mapping shared user page\n");

		/*
		 * Bring-up invariant failure.
		 *
		 * We intentionally stop here rather than trying to
		 * continue from a partially constructed test object.
		 */
		halt_forever();
	}

	/*
	 * --------------------------------------------------------------
	 * STEP 3
	 *
	 * Wrap the raw directory in address_space_t.
	 *
	 * Initial:
	 *
	 *     refs = 1
	 *
	 * owned by this test function.
	 * --------------------------------------------------------------
	 */
	address_space_t *space =
		address_space_adopt_user(
			directory);

	if (space == NULL)
	{
		log_error(
			"U12.2: failed adopting address space\n");

		halt_forever();
	}

	size_t references =
		address_space_reference_count(
			space);

	if (references != 1u)
	{
		log_error(
			"U12.2: initial refs expected=1 got=%lu\n",
			(unsigned long)references);

		halt_forever();
	}

	/*
	 * --------------------------------------------------------------
	 * STEP 4
	 *
	 * Create TWO tasks referencing the exact same address_space_t.
	 *
	 * Neither task is runnable yet.
	 * --------------------------------------------------------------
	 */
	task_t *task_a =
		task_create_user_unpublished(
			u12_shared_space_task_a,
			space,
			space,
			SCHED_POLICY_REALTIME);

	if (task_a == NULL)
	{
		log_error(
			"U12.2: failed creating task A\n");

		halt_forever();
	}

	task_t *task_b =
		task_create_user_unpublished(
			u12_shared_space_task_b,
			space,
			space,
			SCHED_POLICY_REALTIME);

	if (task_b == NULL)
	{
		log_error(
			"U12.2: failed creating task B\n");

		halt_forever();
	}

	/*
	 * The creator plus two tasks now own references.
	 */
	references =
		address_space_reference_count(
			space);

	if (references != 3u)
	{
		log_error(
			"U12.2: expected refs=3 after creation, got=%lu\n",
			(unsigned long)references);

		halt_forever();
	}

	/*
	 * Save IDs BEFORE publication.
	 *
	 * Once task_publish() runs, another CPU may execute
	 * and eventually free that task_t.
	 */
	task_id_t tid_a =
		task_a->id;

	task_id_t tid_b =
		task_b->id;

	log_success(
		"U12.2: shared CR3=0x%lx "
		"taskA=%u taskB=%u refs=%lu\n",
		(unsigned long)
			address_space_page_directory(
				space),
		(unsigned)tid_a,
		(unsigned)tid_b,
		(unsigned long)references);

	/*
	 * --------------------------------------------------------------
	 * STEP 5
	 *
	 * Publish B first.
	 *
	 * It will enter and block on u12_shared_space_gate.
	 *
	 * Then publish A, which writes the shared page and exits.
	 * --------------------------------------------------------------
	 */
	task_publish(
		task_b);

	task_publish(
		task_a);

	/*
	 * DO NOT dereference task_a or task_b after this point.
	 */

	/*
	 * --------------------------------------------------------------
	 * STEP 6
	 *
	 * Wait until exactly one new task has been COMPLETELY reaped.
	 *
	 * B is blocked, therefore the first reaped worker must be A.
	 * --------------------------------------------------------------
	 */
	for (size_t i = 0;
		 i < 256u;
		 ++i)
	{
		if (task_cleanup_total_reaped() >=
			reaped_before + 1u)
		{
			break;
		}

		task_yield();
	}

	if (task_cleanup_total_reaped() <
		reaped_before + 1u)
	{
		log_error(
			"U12.2: task A was not reaped\n");

		halt_forever();
	}

	if (!u12_task_a_passed)
	{
		log_error(
			"U12.2: task A validation failed\n");

		halt_forever();
	}

	/*
	 * A's task reference must now be gone.
	 *
	 * The address space MUST still exist because:
	 *
	 *     test owner = 1
	 *     task B     = 1
	 */
	references =
		address_space_reference_count(
			space);

	if (references != 2u)
	{
		log_error(
			"U12.2: after reaping A expected refs=2 got=%lu\n",
			(unsigned long)references);

		halt_forever();
	}

	log_success(
		"U12.2: task A reaped, shared address space survived "
		"refs=%lu\n",
		(unsigned long)references);

	/*
	 * --------------------------------------------------------------
	 * STEP 7
	 *
	 * Allow B to continue.
	 *
	 * B will verify that:
	 *
	 *     - it still uses the same CR3
	 *     - refs == 2
	 *     - A's userspace memory value still exists
	 * --------------------------------------------------------------
	 */

	/*
	 * DO NOT dereference task_a or task_b after publication.
	 */

	/*
	 * --------------------------------------------------------------
	 * STEP 6
	 *
	 * Wait for B to prove that A's reference was released
	 * and that the shared userspace memory survived.
	 *
	 * Unlike the previous test, this is NOT a 256-yield
	 * "timeout".
	 *
	 * B itself detects the exact lifetime transition:
	 *
	 *     refs 3 -> 2
	 *
	 * and only sets u12_task_b_passed after successfully
	 * reading A's value from the shared userspace page.
	 * --------------------------------------------------------------
	 */

	while (!u12_task_b_passed)
	{
		task_yield();
	}

	if (!u12_task_a_passed)
	{
		log_error(
			"U12.2: task A validation failed\n");

		halt_forever();
	}

	log_success(
		"U12.2: task B proved shared address-space survival\n");

	/*
	 * --------------------------------------------------------------
	 * STEP 7
	 *
	 * B has called task_exit(), but it may not have been
	 * completely reaped yet.
	 *
	 * Wait until both new zombies have completed cleanup.
	 *
	 * There is deliberately no arbitrary yield count here.
	 * --------------------------------------------------------------
	 */

	while (task_cleanup_total_reaped() <
		   reaped_before + 2u)
	{
		task_yield();
	}

	/*
	 * Both task references are now gone.
	 *
	 * Only this test function's original reference remains.
	 */
	references =
		address_space_reference_count(
			space);

	if (references != 1u)
	{
		log_error(
			"U12.2: after worker cleanup "
			"expected refs=1 got=%lu\n",
			(unsigned long)references);

		halt_forever();
	}

	/*
	 * Both temporary task_t objects must also be gone.
	 */
	size_t live_after =
		task_live_count();

	if (live_after !=
		live_before)
	{
		log_error(
			"U12.2: task leak "
			"live before=%lu after=%lu\n",
			(unsigned long)live_before,
			(unsigned long)live_after);

		halt_forever();
	}

	if (task_cleanup_pending_count() != 0)
	{
		log_error(
			"U12.2: cleanup queue still contains tasks\n");

		halt_forever();
	}

	/*
	 * --------------------------------------------------------------
	 * STEP 8
	 *
	 * Drop the final test-owner reference.
	 *
	 *     refs 1 -> 0
	 *
	 * This destroys the shared user address space.
	 * --------------------------------------------------------------
	 */

	if (paging_current_directory() !=
		kernel_directory)
	{
		log_error(
			"U12.2: kernel CR3 not restored "
			"before final release\n");

		halt_forever();
	}

	if (!address_space_release(
			space))
	{
		log_error(
			"U12.2: final address-space release failed\n");

		halt_forever();
	}

	/*
	 * space is invalid from here onward.
	 */

	log_success(
		"U12.2: both tasks reaped and "
		"final address space destroyed\n");

	log_success(
		"U12.2: shared address-space "
		"lifetime test PASSED\n");
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

static void u11_spawn_test(void)
{
	printf(
		"\n=== U11 PROCESS SPAWN TEST ===\n");

	/*
	 * process_spawn_user() currently prepares new address spaces
	 * while the canonical kernel CR3 is active.
	 */
	uintptr_t kernel_directory =
		paging_kernel_directory();

	if (kernel_directory == 0 ||
		paging_current_directory() !=
			kernel_directory)
	{
		printf(
			"U11: kernel CR3 is not active\n");

		halt_forever();
	}

	/*
	 * These are kernel-side strings.
	 *
	 * elf_load_user_image() copies them into the new process's
	 * private userspace stack, so these pointers are not passed
	 * directly to userspace.
	 */
	static const char *const argv[] =
		{
			"/bin/hello.nex",
			"one",
			"two"};

	const size_t argc =
		sizeof(argv) /
		sizeof(argv[0]);

	size_t live_before =
		task_live_count();

	uint64_t reaped_before =
		task_cleanup_total_reaped();

	printf(
		"U11: spawning /bin/hello.nex argc=%lu\n",
		(unsigned long)argc);

	task_id_t user_tid =
		process_spawn_user(
			"/bin/hello.nex",
			argc,
			argv);

	if (user_tid == 0)
	{
		log_error(
			"U11: process_spawn_user failed\n");

		halt_forever();
	}

	log_success(
		"U11: spawned userspace tid=%u\n",
		(unsigned)user_tid);

	/*
	 * Let the scheduler run hello.nex.
	 */
	task_yield();

	/*
	 * Wait for the userspace task to exit and for the reaper
	 * to completely destroy it.
	 *
	 * This is only a kernel bring-up test.
	 *
	 * Later this becomes proper process_wait()/waitpid().
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
			"U11: userspace task was not reaped exactly once\n");

		halt_forever();
	}

	if (pending_after != 0)
	{
		printf(
			"U11: userspace task cleanup did not finish\n");

		halt_forever();
	}

	if (live_after !=
		live_before)
	{
		printf(
			"U11: userspace task lifecycle leaked a task\n");

		printf(
			"U11: live before=%lu after=%lu\n",
			(unsigned long)live_before,
			(unsigned long)live_after);

		halt_forever();
	}

	printf(
		"U11: task %u exited and was reaped\n",
		(unsigned)user_tid);

	printf(
		"U11: process_spawn_user test PASSED\n");

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

	u12_shared_address_space_test();

	/*
	 * First prove that multiple tasks can safely share one
	 * address-space object.
	 */
	u12_shared_address_space_test();

	/*
	 * Then make sure normal ELF userspace spawning still works.
	 */
	u11_spawn_test();

	yield_forever();
}