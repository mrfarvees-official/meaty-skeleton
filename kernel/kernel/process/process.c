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

typedef struct process_child_link
{
    process_t *child;

    struct process_child_link *previous;
    struct process_child_link *next;

} process_child_link_t;

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
     */
    address_space_t *address_space;

    /*
     * Execution membership/accounting.
     */
    size_t live_threads;

    process_accounting_t accounting;

    /*
     * ----------------------------------------------------------
     * Child ownership.
     * ----------------------------------------------------------
     *
     * Every entry owns one retained reference to child.
     *
     * Protected by this process's lock.
     */
    process_child_link_t *children;

    size_t child_count;

    /*
     * Temporary generic handle implementation.
     */
    bool handles[PROCESS_MAX_HANDLES];

    /*
     * Protect mutable process-local fields:
     *
     *     state
     *     termination information
     *     thread accounting
     *     children
     *     handle/accounting fields
     *
     * reference_count and registry links remain protected by
     * process_registry_lock.
     */
    spinlock_t lock;

    /*
     * Global registry linkage.
     */
    struct process *registry_previous;
    struct process *registry_next;

    /*
     * Canonical absolute current working directory.
     *
     * Protected by process->lock.
     */
    char cwd[PROCESS_PATH_MAX];
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
        process_registry_head->registry_previous =
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
        process->registry_previous->registry_next =
            process->registry_next;
    }
    else
    {
        process_registry_head =
            process->registry_next;
    }

    if (process->registry_next != NULL)
    {
        process->registry_next->registry_previous =
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

static bool process_path_apply(
    char *buffer,
    size_t capacity,
    size_t *length,
    const char *path)
{
    if (buffer == NULL ||
        capacity < 2u ||
        length == NULL ||
        path == NULL)
    {
        return false;
    }

    const char *cursor =
        path;

    while (*cursor != '\0')
    {
        /*
         * Collapse repeated '/'.
         */
        while (*cursor == '/')
            ++cursor;

        if (*cursor == '\0')
            break;

        const char *component =
            cursor;

        while (*cursor != '\0' &&
               *cursor != '/')
        {
            ++cursor;
        }

        size_t component_length =
            (size_t)(cursor - component);

        /*
         * Ignore ".".
         */
        if (component_length == 1u &&
            component[0] == '.')
        {
            continue;
        }

        /*
         * Resolve ".." without escaping above root.
         */
        if (component_length == 2u &&
            component[0] == '.' &&
            component[1] == '.')
        {
            while (*length > 1u &&
                   buffer[*length - 1u] != '/')
            {
                --(*length);
            }

            if (*length > 1u)
                --(*length);

            buffer[*length] =
                '\0';

            continue;
        }

        size_t separator_length =
            (*length > 1u)
                ? 1u
                : 0u;

        if (*length +
                separator_length +
                component_length +
                1u >
            capacity)
        {
            return false;
        }

        if (*length > 1u)
        {
            buffer[*length] =
                '/';

            ++(*length);
        }

        memcpy(
            buffer + *length,
            component,
            component_length);

        *length +=
            component_length;

        buffer[*length] =
            '\0';
    }

    return true;
}

bool process_get_cwd(
    process_t *process,
    char *buffer,
    size_t capacity)
{
    if (process == NULL ||
        buffer == NULL ||
        capacity == 0u)
    {
        return false;
    }

    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);

    size_t length =
        strlen(process->cwd);

    if (length + 1u >
        capacity)
    {
        spin_unlock_irqrestore(
            &process->lock,
            flags);

        return false;
    }

    memcpy(
        buffer,
        process->cwd,
        length + 1u);

    spin_unlock_irqrestore(
        &process->lock,
        flags);

    return true;
}

bool process_resolve_path(
    process_t *process,
    const char *path,
    char *buffer,
    size_t capacity)
{
    if (process == NULL ||
        path == NULL ||
        path[0] == '\0' ||
        buffer == NULL ||
        capacity < 2u)
    {
        return false;
    }

    size_t length;

    if (path[0] == '/')
    {
        buffer[0] =
            '/';

        buffer[1] =
            '\0';

        length =
            1u;
    }
    else
    {
        if (!process_get_cwd(
                process,
                buffer,
                capacity))
        {
            return false;
        }

        length =
            strlen(buffer);

        if (length == 0u ||
            buffer[0] != '/')
        {
            return false;
        }
    }

    return process_path_apply(
        buffer,
        capacity,
        &length,
        path);
}

bool process_set_cwd(
    process_t *process,
    const char *path)
{
    if (process == NULL ||
        path == NULL ||
        path[0] != '/')
    {
        return false;
    }

    char canonical[PROCESS_PATH_MAX];

    if (!process_resolve_path(
            process,
            path,
            canonical,
            sizeof(canonical)))
    {
        return false;
    }

    size_t length =
        strlen(canonical);

    uint32_t flags =
        spin_lock_irqsave(
            &process->lock);

    memcpy(
        process->cwd,
        canonical,
        length + 1u);

    spin_unlock_irqrestore(
        &process->lock,
        flags);

    return true;
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

    /*
     * PID 1 is the root of the initial process hierarchy.
     *
     * Every process must always have a valid canonical absolute cwd.
     * The first userspace process inherits from PID 1, so this must
     * be initialized before PID 1 is published.
     */
    kernel_process.cwd[0] =
        '/';

    kernel_process.cwd[1] =
        '\0';

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
     * Later this can become userspace init without changing
     * the process ownership model.
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

    kernel_process.children =
        NULL;

    kernel_process.child_count =
        0u;

    kernel_process.live_threads =
        0u;

    /*
     * Preserve the existing standard-handle accounting.
     *
     * These are still accounting placeholders rather than
     * the VFS-backed fd table.
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

    kernel_process.accounting.runtime_ticks =
        0u;

    kernel_process.accounting.peak_threads =
        0u;

    kernel_process.registry_previous =
        NULL;

    kernel_process.registry_next =
        NULL;

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
     * The process itself owns one independent reference
     * to its address space.
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

    process->cwd[0] =
        '/';

    process->cwd[1] =
        '\0';

    process->children =
        NULL;

    process->child_count =
        0u;

    /*
     * ----------------------------------------------------------
     * Publish process into registry and assign PID.
     * ----------------------------------------------------------
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

    /*
     * ----------------------------------------------------------
     * Attach to real parent.
     *
     * parent_id == 0 means intentionally parentless.
     *
     * Normal userspace spawn should pass a valid parent PID.
     * ----------------------------------------------------------
     */
    if (parent_id !=
        PROCESS_ID_INVALID)
    {
        process_t *parent =
            process_acquire_by_id(
                parent_id);

        if (parent == NULL)
        {
            /*
             * Process was already registered, so clean it up
             * through normal reference destruction.
             */
            process_release(
                process);

            return NULL;
        }

        char inherited_cwd[PROCESS_PATH_MAX];

        if (!process_get_cwd(
                parent,
                inherited_cwd,
                sizeof(inherited_cwd)) ||
            !process_set_cwd(
                process,
                inherited_cwd))
        {
            process_release(
                parent);

            process_release(
                process);

            return NULL;
        }

        if (!process_add_child(
                parent,
                process))
        {
            process_release(
                parent);

            process_release(
                process);

            return NULL;
        }

        /*
         * process_add_child() gave parent its own retained
         * reference to the child.
         *
         * Drop only our temporary parent lookup reference.
         */
        process_release(
            parent);
    }

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

    if (process->children != NULL ||
        process->child_count != 0u)
    {
        kernel_panic(
            "PROCESS: destroying process with owned children");
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
    return (handle_t)(selected + 1u);
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

bool process_add_child(
    process_t *parent,
    process_t *child)
{
    if (parent == NULL ||
        child == NULL ||
        parent == child)
    {
        return false;
    }

    process_id_t parent_id =
        process_id(
            parent);

    process_id_t child_parent_id =
        process_parent_id(
            child);

    /*
     * The metadata relationship must already agree.
     */
    if (parent_id == PROCESS_ID_INVALID ||
        child_parent_id != parent_id)
    {
        return false;
    }

    /*
     * Allocate bookkeeping before changing ownership.
     */
    process_child_link_t *link =
        kmalloc(
            sizeof(*link));

    if (link == NULL)
        return false;

    memset(
        link,
        0,
        sizeof(*link));

    /*
     * Parent owns one real process reference.
     */
    if (!process_retain(
            child))
    {
        kfree(
            link);

        return false;
    }

    link->child =
        child;

    uint32_t flags =
        spin_lock_irqsave(
            &parent->lock);

    /*
     * Don't attach children to a dying/dead parent.
     */
    if (parent->state ==
            PROCESS_EXITING ||
        parent->state ==
            PROCESS_ZOMBIE ||
        parent->state ==
            PROCESS_DEAD)
    {
        spin_unlock_irqrestore(
            &parent->lock,
            flags);

        process_release(
            child);

        kfree(
            link);

        return false;
    }

    /*
     * Prevent duplicate child ownership.
     */
    for (process_child_link_t *current =
             parent->children;
         current != NULL;
         current = current->next)
    {
        if (current->child == child ||
            process_id(
                current->child) ==
                process_id(child))
        {
            spin_unlock_irqrestore(
                &parent->lock,
                flags);

            process_release(
                child);

            kfree(
                link);

            return false;
        }
    }

    link->previous =
        NULL;

    link->next =
        parent->children;

    if (parent->children != NULL)
    {
        parent->children->previous =
            link;
    }

    parent->children =
        link;

    ++parent->child_count;

    spin_unlock_irqrestore(
        &parent->lock,
        flags);

    return true;
}

bool process_remove_child(
    process_t *parent,
    process_id_t child_id)
{
    if (parent == NULL ||
        child_id == PROCESS_ID_INVALID)
    {
        return false;
    }

    process_child_link_t *removed =
        NULL;

    uint32_t flags =
        spin_lock_irqsave(
            &parent->lock);

    for (process_child_link_t *current =
             parent->children;
         current != NULL;
         current = current->next)
    {
        if (process_id(
                current->child) !=
            child_id)
        {
            continue;
        }

        if (current->previous != NULL)
        {
            current->previous->next =
                current->next;
        }
        else
        {
            parent->children =
                current->next;
        }

        if (current->next != NULL)
        {
            current->next->previous =
                current->previous;
        }

        if (parent->child_count == 0u)
        {
            spin_unlock_irqrestore(
                &parent->lock,
                flags);

            kernel_panic(
                "PROCESS: child-count underflow");
        }

        --parent->child_count;

        current->previous =
            NULL;

        current->next =
            NULL;

        removed =
            current;

        break;
    }

    spin_unlock_irqrestore(
        &parent->lock,
        flags);

    if (removed == NULL)
        return false;

    /*
     * Drop parent's ownership reference outside spinlock.
     */
    process_release(
        removed->child);

    kfree(
        removed);

    return true;
}

size_t process_child_count(
    process_t *parent)
{
    if (parent == NULL)
        return 0u;

    uint32_t flags =
        spin_lock_irqsave(
            &parent->lock);

    size_t count =
        parent->child_count;

    spin_unlock_irqrestore(
        &parent->lock,
        flags);

    return count;
}

process_t *process_acquire_zombie_child(
    process_t *parent,
    process_id_t child_id)
{
    if (parent == NULL ||
        child_id == PROCESS_ID_INVALID)
    {
        return NULL;
    }

    process_t *child =
        NULL;

    /*
     * The parent-owned child reference guarantees that a linked
     * child remains alive.
     *
     * IMPORTANT:
     *
     * Take our independent reference BEFORE releasing parent->lock.
     *
     * Otherwise another CPU could remove the child from the parent
     * immediately after we drop parent->lock, release the parent's
     * final ownership reference, and destroy the child before we
     * retain or inspect it.
     */
    uint32_t flags =
        spin_lock_irqsave(
            &parent->lock);

    for (process_child_link_t *current =
             parent->children;
         current != NULL;
         current = current->next)
    {
        if (current->child == NULL)
            continue;

        if (process_id(
                current->child) !=
            child_id)
        {
            continue;
        }

        /*
         * While parent->lock is held the link cannot be removed,
         * so the parent's retained reference keeps this object alive.
         */
        if (process_retain(
                current->child))
        {
            child =
                current->child;
        }

        break;
    }

    spin_unlock_irqrestore(
        &parent->lock,
        flags);

    if (child == NULL)
        return NULL;

    /*
     * We now own an independent reference, so the child can safely
     * be inspected even if another CPU removes it from the parent
     * immediately after parent->lock was released.
     */
    if (process_state(
            child) !=
        PROCESS_ZOMBIE)
    {
        process_release(
            child);

        return NULL;
    }

    return child;
}

bool process_waitpid(
    process_t *parent,
    process_id_t child_pid,
    int *status)
{
    if (parent == NULL ||
        child_pid == PROCESS_ID_INVALID)
    {
        return false;
    }

    process_child_link_t *removed =
        NULL;

    int child_status =
        0;

    /*
     * The parent lock protects:
     *
     *     - membership in the child list
     *     - the parent-owned child reference
     *
     * Keeping parent->lock held while checking the child's
     * lifecycle prevents another CPU from concurrently collecting
     * or removing this child underneath us.
     */
    uint32_t parent_flags =
        spin_lock_irqsave(
            &parent->lock);

    for (process_child_link_t *current =
             parent->children;
         current != NULL;
         current = current->next)
    {
        process_t *child =
            current->child;

        if (child == NULL)
            continue;

        if (process_id(
                child) !=
            child_pid)
        {
            continue;
        }

        /*
         * Lock order for this operation:
         *
         *     parent->lock
         *         -> child->lock
         *
         * We must inspect state and exit_status together.
         *
         * The parent still owns a retained reference through
         * 'current', so child cannot disappear while these locks
         * are held.
         */
        uint32_t child_flags =
            spin_lock_irqsave(
                &child->lock);

        if (child->state !=
            PROCESS_ZOMBIE)
        {
            spin_unlock_irqrestore(
                &child->lock,
                child_flags);

            spin_unlock_irqrestore(
                &parent->lock,
                parent_flags);

            return false;
        }

        child_status =
            child->exit_status;

        spin_unlock_irqrestore(
            &child->lock,
            child_flags);

        /*
         * Child is a zombie and still belongs to this parent.
         *
         * Remove the ownership link while parent->lock is still
         * held so two CPUs cannot successfully collect the same
         * child.
         */
        if (current->previous != NULL)
        {
            current->previous->next =
                current->next;
        }
        else
        {
            parent->children =
                current->next;
        }

        if (current->next != NULL)
        {
            current->next->previous =
                current->previous;
        }

        if (parent->child_count == 0u)
        {
            spin_unlock_irqrestore(
                &parent->lock,
                parent_flags);

            kernel_panic(
                "PROCESS: child-count underflow in waitpid");
        }

        --parent->child_count;

        current->previous =
            NULL;

        current->next =
            NULL;

        removed =
            current;

        break;
    }

    spin_unlock_irqrestore(
        &parent->lock,
        parent_flags);

    /*
     * No matching child means either:
     *
     *     - PID was never our child
     *     - another waiter already collected it
     */
    if (removed == NULL)
        return false;

    /*
     * Do not touch the child after dropping the ownership
     * reference unless we own another independent reference.
     *
     * Save everything needed before this point.
     */
    if (status != NULL)
    {
        *status =
            child_status;
    }

    /*
     * The child link owned exactly one process reference.
     *
     * Drop it outside all process-local spinlocks because this may
     * become the final reference and trigger process/address-space
     * destruction.
     */
    process_release(
        removed->child);

    removed->child =
        NULL;

    kfree(
        removed);

    return true;
}