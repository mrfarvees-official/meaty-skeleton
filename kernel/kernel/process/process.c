#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/heap.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>

#define PROCESS_MAX_HANDLES 64u

struct process
{
    process_id_t id;
    bool live;
    bool handles[PROCESS_MAX_HANDLES];
};

static process_t kernel_process;
static process_id_t next_process_id = 1u;
static size_t live_processes;
static size_t live_handles;
static spinlock_t process_lock = SPINLOCK_INITIALIZER;

static void process_initialize_record(process_t *process, bool standard_handles)
{
    memset(process, 0, sizeof(*process));
    process->id = next_process_id++;
    process->live = true;

    if (standard_handles)
    {
        for (size_t i = 0; i < PROCESS_STANDARD_HANDLE_COUNT; ++i)
            process->handles[i] = true;

        live_handles += PROCESS_STANDARD_HANDLE_COUNT;
    }

    ++live_processes;
}

void process_initialize(void)
{
    uint32_t flags = spin_lock_irqsave(&process_lock);

    if (live_processes == 0)
        process_initialize_record(&kernel_process, true);

    spin_unlock_irqrestore(&process_lock, flags);
}

process_t *process_create(void)
{
    process_t *process = kmalloc(sizeof(*process));

    if (process == NULL)
        return NULL;

    uint32_t flags = spin_lock_irqsave(&process_lock);
    process_initialize_record(process, false);
    spin_unlock_irqrestore(&process_lock, flags);

    return process;
}

void process_destroy(process_t *process)
{
    if (process == NULL || process == &kernel_process)
        return;

    uint32_t flags = spin_lock_irqsave(&process_lock);

    if (!process->live)
    {
        spin_unlock_irqrestore(&process_lock, flags);
        return;
    }

    for (size_t i = 0; i < PROCESS_MAX_HANDLES; ++i)
    {
        if (process->handles[i])
            --live_handles;
    }

    process->live = false;
    --live_processes;
    spin_unlock_irqrestore(&process_lock, flags);

    kfree(process);
}

handle_t process_handle_open(process_t *process)
{
    if (process == NULL)
        return PROCESS_HANDLE_INVALID;

    uint32_t flags = spin_lock_irqsave(&process_lock);

    if (!process->live)
    {
        spin_unlock_irqrestore(&process_lock, flags);
        return PROCESS_HANDLE_INVALID;
    }

    for (size_t i = PROCESS_STANDARD_HANDLE_COUNT;
         i < PROCESS_MAX_HANDLES; ++i)
    {
        if (!process->handles[i])
        {
            process->handles[i] = true;
            ++live_handles;
            spin_unlock_irqrestore(&process_lock, flags);
            return (handle_t)(i + 1u);
        }
    }

    spin_unlock_irqrestore(&process_lock, flags);
    return PROCESS_HANDLE_INVALID;
}

bool process_handle_close(process_t *process, handle_t handle)
{
    if (process == NULL || handle == PROCESS_HANDLE_INVALID ||
        handle > PROCESS_MAX_HANDLES)
        return false;

    size_t index = (size_t)handle - 1u;
    uint32_t flags = spin_lock_irqsave(&process_lock);

    if (!process->live || !process->handles[index])
    {
        spin_unlock_irqrestore(&process_lock, flags);
        return false;
    }

    process->handles[index] = false;
    --live_handles;
    spin_unlock_irqrestore(&process_lock, flags);
    return true;
}

size_t process_live_count(void)
{
    uint32_t flags = spin_lock_irqsave(&process_lock);
    size_t count = live_processes;
    spin_unlock_irqrestore(&process_lock, flags);
    return count;
}

size_t process_handle_live_count(void)
{
    uint32_t flags = spin_lock_irqsave(&process_lock);
    size_t count = live_handles;
    spin_unlock_irqrestore(&process_lock, flags);
    return count;
}
