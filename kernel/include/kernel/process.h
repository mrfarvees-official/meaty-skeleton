#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/address_space.h>


typedef uint32_t process_id_t;
typedef uint32_t handle_t;


#define PROCESS_ID_INVALID \
    ((process_id_t)0u)

#define PROCESS_HANDLE_INVALID \
    ((handle_t)0u)

#define PROCESS_STANDARD_HANDLE_COUNT \
    3u


/*
 * Process lifetime.
 *
 * NEW:
 *     object exists but has no running thread yet.
 *
 * RUNNING:
 *     at least one execution context belongs to it.
 *
 * EXITING:
 *     termination has been requested.
 *
 * ZOMBIE:
 *     no execution contexts remain, but lifetime information
 *     is retained for future waitpid()/task-manager support.
 *
 * DEAD:
 *     final internal destruction state. A DEAD process is
 *     never visible through the registry.
 */
typedef enum process_state
{
    PROCESS_NEW = 0,
    PROCESS_RUNNING,
    PROCESS_EXITING,
    PROCESS_ZOMBIE,
    PROCESS_DEAD
} process_state_t;


/*
 * Why a process stopped.
 *
 * Only NORMAL is used initially.
 * The others intentionally reserve clean extension points for
 * kill(), userspace faults, etc.
 */
typedef enum process_termination_reason
{
    PROCESS_TERMINATION_NONE = 0,
    PROCESS_TERMINATION_NORMAL,
    PROCESS_TERMINATION_KILLED,
    PROCESS_TERMINATION_FAULT
} process_termination_reason_t;


/*
 * Accounting is kept separate from core identity/lifetime fields.
 *
 * Not every field needs to be actively maintained immediately.
 */
typedef struct process_accounting
{
    uint64_t runtime_ticks;

    size_t peak_threads;

    size_t current_handles;
    size_t peak_handles;
} process_accounting_t;


/*
 * Safe diagnostic representation.
 *
 * A future task manager should consume snapshots like this rather
 * than receiving kernel process_t pointers.
 */
typedef struct process_info
{
    process_id_t id;
    process_id_t parent_id;

    process_state_t state;
    process_termination_reason_t termination_reason;

    size_t thread_count;
    size_t handle_count;

    uint64_t runtime_ticks;

    /*
     * Useful for kernel diagnostics.
     *
     * Do not necessarily expose this raw value to unprivileged
     * userspace later.
     */
    uintptr_t page_directory;
} process_info_t;


typedef struct process process_t;


/*
 * Initialize process registry and immortal kernel process.
 *
 * Must run after address_space_initialize().
 */
void process_initialize(void);


/*
 * Create and register a new process.
 *
 * address_space is BORROWED by the caller.
 * The process obtains its own independent reference.
 *
 * Returned process contains one creator-owned reference.
 */
process_t *process_create(
    address_space_t *address_space,
    process_id_t parent_id);


/*
 * Reference management.
 *
 * Any long-lived user of process_t must own a reference.
 */
bool process_retain(
    process_t *process);

void process_release(
    process_t *process);


/*
 * Lookup by PID.
 *
 * On success this returns a RETAINED reference.
 * Caller MUST call process_release().
 */
process_t *process_acquire_by_id(
    process_id_t id);


/*
 * Basic accessors.
 */
process_id_t process_id(
    const process_t *process);

process_id_t process_parent_id(
    process_t *process);

process_state_t process_state(
    process_t *process);


/*
 * BORROWED address-space pointer.
 *
 * Lifetime is guaranteed only while caller retains process.
 */
address_space_t *process_address_space(
    process_t *process);


/*
 * Execution accounting.
 *
 * Phase P1B will connect these to task creation/reaping.
 */
bool process_thread_attach(
    process_t *process);

void process_thread_detach(
    process_t *process);

size_t process_thread_count(
    process_t *process);


/*
 * Scheduler accounting hook.
 *
 * Nothing needs to call this yet.
 */
void process_account_runtime(
    process_t *process,
    uint64_t ticks);


/*
 * Safe diagnostic snapshot.
 */
bool process_snapshot(
    process_t *process,
    process_info_t *info);

bool process_snapshot_by_id(
    process_id_t id,
    process_info_t *info);


/*
 * Temporary generic-handle infrastructure.
 *
 * Keep this API stable for now. The internal boolean representation
 * can later become:
 *
 *     object pointer
 *     type
 *     rights
 *
 * without changing callers.
 */
handle_t process_handle_open(
    process_t *process);

bool process_handle_close(
    process_t *process,
    handle_t handle);


/*
 * System-wide diagnostics.
 */
size_t process_live_count(void);

size_t process_handle_live_count(void);


#endif