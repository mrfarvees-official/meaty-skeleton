#include <stddef.h>
#include <stdint.h>

#include <kernel/fd.h>
#include <kernel/spinlock.h>
#include <kernel/vfs.h>

typedef struct kernel_fd_entry
{
    file_t *file;
} kernel_fd_entry_t;

static kernel_fd_entry_t fd_table[KERNEL_FD_MAX];
static spinlock_t fd_lock = SPINLOCK_INITIALIZER;

static int kernel_fd_valid(int fd)
{
    return fd >= KERNEL_FD_FIRST &&
           fd < KERNEL_FD_MAX;
}

int kernel_fd_open(
    const char *path,
    uint32_t flags)
{
    uint32_t vfs_flags = 0;
    file_t *file = NULL;

    if (path == NULL)
        return -1;

    if (flags & KERNEL_FD_READ)
        vfs_flags |= VFS_OPEN_READ;

    if (flags & KERNEL_FD_WRITE)
        vfs_flags |= VFS_OPEN_WRITE;

    if (vfs_flags == 0)
        return -1;

    if (vfs_open(path, vfs_flags, &file) != 0)
        return -1;

    uint32_t irq_flags = spin_lock_irqsave(&fd_lock);
    
    for (int fd = KERNEL_FD_FIRST; fd < KERNEL_FD_MAX; fd++)
    {
        if (fd_table[fd].file == NULL)
        {
            fd_table[fd].file = file;

            spin_unlock_irqrestore(&fd_lock, irq_flags);

            return fd;
        }
    }

    spin_unlock_irqrestore(&fd_lock, irq_flags);

    vfs_close(file);

    return -1;
}

int kernel_fd_read(
    int fd,
    void *buffer,
    size_t size,
    size_t *bytes_read)
{
    file_t *file;

    if (!kernel_fd_valid(fd) || buffer == NULL || bytes_read == NULL)
        return -1;

    uint32_t irq_flags = spin_lock_irqsave(&fd_lock);

    file = fd_table[fd].file;

    spin_unlock_irqrestore(&fd_lock, irq_flags);

    if (file == NULL)
        return -1;

    return vfs_read(
        file,
        buffer,
        size,
        bytes_read);
}

int kernel_fd_write(
    int fd,
    const void *buffer,
    size_t size,
    size_t *bytes_written)
{
    file_t *file;

    if (!kernel_fd_valid(fd) || buffer == NULL || bytes_written == NULL)
        return -1;

    uint32_t irq_flags = spin_lock_irqsave(&fd_lock);

    file = fd_table[fd].file;

    spin_unlock_irqrestore(&fd_lock, irq_flags);

    if (file == NULL)
        return -1;
    
    return vfs_write(
        file,
        buffer,
        size,
        bytes_written);
}

int kernel_fd_close(int fd)
{
    file_t *file;

    if (!kernel_fd_valid(fd))
        return -1;

    uint32_t irq_flags = spin_lock_irqsave(&fd_lock);

    file = fd_table[fd].file;

    if (file == NULL)
    {
        spin_unlock_irqrestore(&fd_lock, irq_flags);
        return -1;
    }

    fd_table[fd].file = NULL;

    spin_unlock_irqrestore(&fd_lock, irq_flags);

    vfs_close(file);

    return 0;
}