#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/address_space.h>
#include <kernel/heap.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>


#define PROCESS_MAX_HANDLES \
    64u


struct process
{
    /*
     * Identity.
     */
    process_id_t id;
    process_id_t parent_id;


    /*
     * Lifetime.
     *
     * reference_count is protected by process_registry_lock.
     */
    uint32_t reference_count;

    bool immortal;

    process_state_t state;

    process_termination_reason_t
        termination_reason;

    int exit_status;


    /*
     * Process-owned address space.
     *
     * Dynamic processes retain an independent reference.
     *
     * The immortal kernel process points at the immortal kernel
     * address space.
     */
    address_space_t *address_space;


    /*
     * Execution membership/accounting.
     */
    size_t live_threads;

    process_accounting_t accounting;


    /*
     * Temporary generic handle implementation.
     *
     * This deliberately remains private so we can replace it later
     * with real kernel-object handles without ABI damage.
     */
    bool handles[PROCESS_MAX_HANDLES];


    /*
     * Protect mutable fields belonging to this process.
     *
     * reference_count and registry links are instead protected by
     * process_registry_lock.
     */
    spinlock_t lock;


    /*
     * Global process-registry linkage.
     */
    struct process *registry_previous;
    struct process *registry_next;
};


/*
 * --------------------------------------------------------------------------
 * GLOBAL PROCESS REGISTRY
 * --------------------------------------------------------------------------
 */

static process_t kernel_process;

static process_t *process_registry_head =
    NULL;

static process_id_t next_process_id =
    1u;

static size_t live_processes =
    0u;

static size_t live_handles =
    0u;

static bool process_system_initialized =
    false;


/*
 * This lock protects:
 *
 *     process registry
 *     PID allocation
 *     reference counts
 *     global process count
 *     global handle count
 */
static spinlock_t process_registry_lock =
    SPINLOCK_INITIALIZER;


/*
 * --------------------------------------------------------------------------
 * REGISTRY HELPERS
 * --------------------------------------------------------------------------
 */

static void process_registry_insert_locked(
    process_t *process)
{
    if (process == NULL)
        return;

    process->registry_previous =
        NULL;

    process->registry_next =
        process_registry_head;

    if (process_registry_head != NULL)
    {
        process_registry_head->
            registry_previous =
                process;
    }

    process_registry_head =
        process;
}


static void process_registry_remove_locked(
    process_t *process)
{
    if (process == NULL)
        return;

    if (process->registry_previous != NULL)
    {
        process->registry_previous->
            registry_next =
                process->registry_next;
    }
    else
    {
        process_registry_head =
            process->registry_next;
    }

    if (process->registry_next != NULL)
    {
        process->registry_next->
            registry_previous =
                process->registry_previous;
    }

    process->registry_previous =
        NULL;

    process->registry_next =
        NULL;
}


static process_t *process_registry_find_locked(
    process_id_t id)
{
    if (id == PROCESS_ID_INVALID)
        return NULL;

    for (process_t *process =
             process_registry_head;
         process != NULL;
         process =
             process->registry_next)
    {
        if (process->id == id)
            return process;
    }

    return NULL;
}


/*
 * --------------------------------------------------------------------------
 * INITIALIZATION
 * --------------------------------------------------------------------------
 */

void process_initialize(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &process_registry_lock);

    if (process_system_initialized)
    {
        spin_unlock_irqrestore(
            &process_registry_lock,
            flags);

        return;
    }

    memset(
        &kernel_process,
        0,
        sizeof(kernel_process));

    spinlock_initialize(
        &kernel_process.lock);

    address_space_t *kernel_space =
        address_space_kernel();

    if (kernel_space == NULL)
    {
        spin_unlock_irqrestore(
            &process_registry_lock,
            flags);

        kernel_panic(
            "PROCESS: kernel address space unavailable");
    }


    /*
     * PID 1 is reserved for the immortal kernel process.
     *
     * Later you may decide PID 1 should instead become userspace
     * init. That policy can change without redesigning process_t.
     */
    kernel_process.id =
        1u;

    kernel_process.parent_id =
        PROCESS_ID_INVALID;

    kernel_process.reference_count =
        1u;

    kernel_process.immortal =
        true;

    kernel_process.state =
        PROCESS_RUNNING;

    kernel_process.termination_reason =
        PROCESS_TERMINATION_NONE;

    kernel_process.exit_status =
        0;

    kernel_process.address_space =
        kernel_space;


    /*
     * Preserve the existing standard-handle accounting.
     *
     * These are placeholders for now, not real fd 0/1/2.
     */
    for (size_t i = 0;
         i < PROCESS_STANDARD_HANDLE_COUNT;
         ++i)
    {
        kernel_process.handles[i] =
            true;
    }

    kernel_process.accounting.current_handles =
        PROCESS_STANDARD_HANDLE_COUNT;

    kernel_process.accounting.peak_handles =
        PROCESS_STANDARD_HANDLE_COUNT;


    process_registry_insert_locked(
        &kernel_process);

    live_processes =
        1u;

    live_handles =
        PROCESS_STANDARD_HANDLE_COUNT;

    next_process_id =
        2u;

    process_system_initialized =
        true;


    spin_unlock_irqrestore(
        &process_registry_lock,
        flags);
}


/*
 * --------------------------------------------------------------------------
 * CREATION
 * --------------------------------------------------------------------------
 */

process_t *process_create(
    address_space_t *address_space,
    process_id_t parent_id)
{
    if (address_space == NULL)
        return NULL;


    /*
     * The process itself owns one independent reference.
     */
    if (!address_space_retain(
            address_space))
    {
        return NULL;
    }


    process_t *process =
        kmalloc(
            sizeof(*process));

    if (process == NULL)
    {
        address_space_release(
            address_space);

        return NULL;
    }


    memset(
        process,
        0,
        sizeof(*process));

    spinlock_initialize(
        &process->lock);


    process->parent_id =
        parent_id;

    process->reference_count =
        1u;

    process->immortal =
        false;

    process->state =
        PROCESS_NEW;

    process->termination_reason =
        PROCESS_TERMINATION_NONE;

    process->exit_status =
        0;

    process->address_space =
        address_space;


    /*
     * Publish identity and registry membership atomically.
     */
    uint32_t flags =
        spin_lock_irqsave(
            &process_registry_lock);


    if (!process_system_initialized)
    {
        spin_unlock_irqrestore(
            &process_registry_lock,
            flags);

        address_space_release(
            address_space);

        kfree(
            process);

        return NULL;
    }


    /*
     * PID zero is permanently invalid.
     *
     * Running out of a 32-bit PID space indicates a kernel-lifetime
     * accounting problem at this stage, so fail hard rather than
     * silently reusing an active PID.
     */
    if (next_process_id ==
        PROCESS_ID_INVALID)
    {
        spin_unlock_irqrestore(
            &process_registry_lock,
            flags);

        address_space_release(
            address_space);

        kfree(
            process);

        kernel_panic(
            "PROCESS: PID space exhausted");
    }


    process->id =
        next_process_id++;


    process_registry_insert_locked(
        process);

    ++live_processes;


    spin_unlock_irqrestore(
        &process_registry_lock,
        flags);


    return process;
}


/*
 * --------------------------------------------------------------------------
 * REFERENCE MANAGEMENT
 * --------------------------------------------------------------------------
 */

bool process_retain(
    process_t *process)
{
    if (process == NULL)
        return false;


    uint32_t flags =
        spin_lock_irqsave(
            &process_registry_lock);


    if (process->immortal)
    {
        spin_unlock_irqrestore(
            &process_registry_lock,
            flags);

        return true;
    }


    if (process->reference_count == 0u ||
        process->state == PROCESS_DEAD)
    {
        spin_unlock_irqrestore(
            &process_registry_lock,
            flags);

        return false;
    }


    ++process->reference_count;


    spin_unlock_irqrestore(
        &process_registry_lock,
        flags);


    return true;
}


void process_release(
    process_t *process)
{
    if (process == NULL)
        return;


    address_space_t *address_space =
        NULL;

    bool destroy =
        false;


    uint32_t flags =
        spin_lock_irqsave(
            &process_registry_lock);


    if (process->immortal)
    {
        spin_unlock_irqrestore(
            &process_registry_lock,
            flags);

        return;
    }


    if (process->reference_count == 0u)
    {
        spin_unlock_irqrestore(
            &process_registry_lock,
            flags);

        kernel_panic(
            "PROCESS: reference-count underflow");
    }


    --process->reference_count;


    if (process->reference_count == 0u)
    {
        /*
         * No new process_acquire_by_id() can observe this object
         * after removal while registry lock is held.
         */
        process_registry_remove_locked(
            process);


        if (live_processes == 0u)
        {
            spin_unlock_irqrestore(
                &process_registry_lock,
                flags);

            kernel_panic(
                "PROCESS: live-process count underflow");
        }


        --live_processes;


        /*
         * Remove any temporary handle accounting that still belongs
         * to this object.
         */
        if (process->accounting.current_handles >
            live_handles)
        {
            spin_unlock_irqrestore(
                &process_registry_lock,
                flags);

            kernel_panic(
                "PROCESS: handle accounting underflow");
        }


        live_handles -=
            process->accounting.current_handles;


        process->state =
            PROCESS_DEAD;


        address_space =
            process->address_space;

        process->address_space =
            NULL;


        destroy =
            true;
    }


    spin_unlock_irqrestore(
        &process_registry_lock,
        flags);


    if (!destroy)
        return;


    /*
     * Never release potentially complicated resources while holding
     * the global registry spinlock.
     */
    if (address_space != NULL)
    {
        if (!address_space_release(
                address_space))
        {
            kernel_panic(
                "PROCESS: failed releasing address space");
        }
    }


    kfree(
        process);
}


/*
 * --------------------------------------------------------------------------
 * LOOKUP
 * --------------------------------------------------------------------------
 */

process_t *process_acquire_by_id(
    process_id_t id)
{
    if (id == PROCESS_ID_INVALID)
        return NULL;


    uint32_t flags =
        spin_lock_irqsave(
            &process_registry_lock);


    process_t *process =
        process_registry_find_locked(
            id);


    if (process == NULL ||
        process->state == PROCESS_DEAD)
    {
        spin_unlock_irqrestore(
            &process_registry_lock,
            flags);

        return NULL;
    }


    if (!process->immortal)
    {
        if (process->reference_count == 0u)
        {
            spin_unlock_irqrestore(
                &process_registry_lock,
                flags);

            return NULL;
        }

        ++process->reference_count;
    }


    spin_unlock_irqrestore(
        &process_registry_lock,
        flags);


    return process;
}


/*
 * --------------------------------------------------------------------------
 * ACCESSORS
 * --------------------------------------------------------------------------
 */

process_id_t process_id(
    const process_t *process)
{
    if (process == NULL)
        return PROCESS_ID_INVALID;

    return process->id;
}


process_id_t process_parent_id(
    process_t *process)
{
    if (process == NULL)
        return PROCESS_ID_INVALID;


    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);

    process_id_t id =
        process->parent_id;

    spin_unlock_irqrestore(
        &process->lock,
        flags);

    return id;
}


process_state_t process_state(
    process_t *process)
{
    if (process == NULL)
        return PROCESS_DEAD;


    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);

    process_state_t state =
        process->state;

    spin_unlock_irqrestore(
        &process->lock,
        flags);

    return state;
}


address_space_t *process_address_space(
    process_t *process)
{
    if (process == NULL)
        return NULL;

    /*
     * Borrowed pointer.
     *
     * Caller must retain process while using it.
     */
    return process->address_space;
}


/*
 * --------------------------------------------------------------------------
 * THREAD ACCOUNTING
 * --------------------------------------------------------------------------
 */

bool process_thread_attach(
    process_t *process)
{
    if (process == NULL)
        return false;


    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);


    if (process->state != PROCESS_NEW &&
        process->state != PROCESS_RUNNING)
    {
        spin_unlock_irqrestore(
            &process->lock,
            flags);

        return false;
    }


    ++process->live_threads;


    if (process->live_threads >
        process->accounting.peak_threads)
    {
        process->accounting.peak_threads =
            process->live_threads;
    }


    process->state =
        PROCESS_RUNNING;


    spin_unlock_irqrestore(
        &process->lock,
        flags);


    return true;
}


void process_thread_detach(
    process_t *process)
{
    if (process == NULL)
        return;


    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);


    if (process->live_threads == 0u)
    {
        spin_unlock_irqrestore(
            &process->lock,
            flags);

        kernel_panic(
            "PROCESS: thread-count underflow");
    }


    --process->live_threads;


    /*
     * Phase P1A has no waitpid() yet.
     *
     * Still transition to ZOMBIE now so the lifetime model is
     * already correct and does not need redesign later.
     */
    if (process->live_threads == 0u)
    {
        if (process->state ==
                PROCESS_RUNNING ||
            process->state ==
                PROCESS_EXITING)
        {
            process->state =
                PROCESS_ZOMBIE;

            if (process->termination_reason ==
                PROCESS_TERMINATION_NONE)
            {
                process->termination_reason =
                    PROCESS_TERMINATION_NORMAL;
            }
        }
    }


    spin_unlock_irqrestore(
        &process->lock,
        flags);
}


size_t process_thread_count(
    process_t *process)
{
    if (process == NULL)
        return 0u;


    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);

    size_t count =
        process->live_threads;

    spin_unlock_irqrestore(
        &process->lock,
        flags);

    return count;
}


/*
 * --------------------------------------------------------------------------
 * ACCOUNTING
 * --------------------------------------------------------------------------
 */

void process_account_runtime(
    process_t *process,
    uint64_t ticks)
{
    if (process == NULL ||
        ticks == 0u)
    {
        return;
    }


    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);


    process->accounting.runtime_ticks +=
        ticks;


    spin_unlock_irqrestore(
        &process->lock,
        flags);
}


/*
 * --------------------------------------------------------------------------
 * SNAPSHOTS
 * --------------------------------------------------------------------------
 */

bool process_snapshot(
    process_t *process,
    process_info_t *info)
{
    if (process == NULL ||
        info == NULL)
    {
        return false;
    }


    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);


    info->id =
        process->id;

    info->parent_id =
        process->parent_id;

    info->state =
        process->state;

    info->termination_reason =
        process->termination_reason;

    info->thread_count =
        process->live_threads;
        
    info->exit_status = 
        process->exit_status;

    info->handle_count =
        process->accounting.current_handles;

    info->runtime_ticks =
        process->accounting.runtime_ticks;

    info->page_directory =
        process->address_space != NULL
            ? address_space_page_directory(
                  process->address_space)
            : 0u;


    spin_unlock_irqrestore(
        &process->lock,
        flags);


    return true;
}


bool process_snapshot_by_id(
    process_id_t id,
    process_info_t *info)
{
    if (info == NULL)
        return false;


    process_t *process =
        process_acquire_by_id(
            id);

    if (process == NULL)
        return false;


    bool result =
        process_snapshot(
            process,
            info);


    process_release(
        process);


    return result;
}


/*
 * --------------------------------------------------------------------------
 * TEMPORARY HANDLE TABLE
 * --------------------------------------------------------------------------
 */

handle_t process_handle_open(
    process_t *process)
{
    if (process == NULL)
        return PROCESS_HANDLE_INVALID;


    size_t selected =
        PROCESS_MAX_HANDLES;


    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);


    if (process->state ==
            PROCESS_DEAD ||
        process->state ==
            PROCESS_ZOMBIE)
    {
        spin_unlock_irqrestore(
            &process->lock,
            flags);

        return PROCESS_HANDLE_INVALID;
    }


    for (size_t i =
             PROCESS_STANDARD_HANDLE_COUNT;
         i < PROCESS_MAX_HANDLES;
         ++i)
    {
        if (!process->handles[i])
        {
            process->handles[i] =
                true;

            selected =
                i;

            ++process->accounting.current_handles;

            if (process->accounting.current_handles >
                process->accounting.peak_handles)
            {
                process->accounting.peak_handles =
                    process->accounting.current_handles;
            }

            break;
        }
    }


    spin_unlock_irqrestore(
        &process->lock,
        flags);


    if (selected ==
        PROCESS_MAX_HANDLES)
    {
        return PROCESS_HANDLE_INVALID;
    }


    flags =
        spin_lock_irqsave(
            &process_registry_lock);

    ++live_handles;

    spin_unlock_irqrestore(
        &process_registry_lock,
        flags);


    /*
     * Preserve your existing one-based generic handle numbering.
     */
    return (handle_t)(
        selected + 1u);
}


bool process_handle_close(
    process_t *process,
    handle_t handle)
{
    if (process == NULL ||
        handle == PROCESS_HANDLE_INVALID ||
        handle > PROCESS_MAX_HANDLES)
    {
        return false;
    }


    size_t index =
        (size_t)handle - 1u;


    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);


    if (!process->handles[index])
    {
        spin_unlock_irqrestore(
            &process->lock,
            flags);

        return false;
    }


    process->handles[index] =
        false;


    if (process->accounting.current_handles ==
        0u)
    {
        spin_unlock_irqrestore(
            &process->lock,
            flags);

        kernel_panic(
            "PROCESS: local handle-count underflow");
    }


    --process->accounting.current_handles;


    spin_unlock_irqrestore(
        &process->lock,
        flags);


    flags =
        spin_lock_irqsave(
            &process_registry_lock);


    if (live_handles == 0u)
    {
        spin_unlock_irqrestore(
            &process_registry_lock,
            flags);

        kernel_panic(
            "PROCESS: global handle-count underflow");
    }


    --live_handles;


    spin_unlock_irqrestore(
        &process_registry_lock,
        flags);


    return true;
}


/*
 * --------------------------------------------------------------------------
 * SYSTEM-WIDE DIAGNOSTICS
 * --------------------------------------------------------------------------
 */

size_t process_live_count(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &process_registry_lock);

    size_t count =
        live_processes;

    spin_unlock_irqrestore(
        &process_registry_lock,
        flags);

    return count;
}


size_t process_handle_live_count(void)
{
    uint32_t flags =
        spin_lock_irqsave(
            &process_registry_lock);

    size_t count =
        live_handles;

    spin_unlock_irqrestore(
        &process_registry_lock,
        flags);

    return count;
}

bool process_set_exit_status(
    process_t *process,
    int status)
{
    if (process == NULL)
        return false;

    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);

    /*
     * DEAD processes cannot be modified.
     */
    if (process->state ==
        PROCESS_DEAD)
    {
        spin_unlock_irqrestore(
            &process->lock,
            flags);

        return false;
    }

    /*
     * For now SYS_EXIT represents normal termination.
     *
     * We allow the status to be written while the process is
     * still RUNNING because the task has not yet reached the
     * reaper.
     */
    process->exit_status =
        status;

    process->termination_reason =
        PROCESS_TERMINATION_NORMAL;

    spin_unlock_irqrestore(
        &process->lock,
        flags);

    return true;
}