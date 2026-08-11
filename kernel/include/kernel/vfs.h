#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct vnode vnode_t;
typedef struct file file_t;

typedef enum vnode_type
{
    VNODE_REGULAR = 0,
    VNODE_DIRECTORY,
    VNODE_CHAR_DEVICE,
    VNODE_BLOCK_DEVICE,
    VNODE_SYMLINK
} vnode_type_t;

typedef struct vnode_ops
{
    /*
     * Find one child inside a directory.
     *
     * Example:
     *
     *     directory = /etc
     *     name      = "config"
     *
     * Returns the vnode for /etc/config.
     */
    int (*lookup)(vnode_t *directory, const char *name, vnode_t **result);

    /*
     * Read bytes from a regular file.
     */
    int (*read)(vnode_t *node, size_t offset, void *buffer, size_t size, size_t *bytes_read);

    /*
     * Write bytes to a regular file.
     *
     * We won't use this immediately,
     * but putting it into the interface now is useful.
     */
    int (*write)(vnode_t *node, size_t offset, const void *buffer, size_t size, size_t *bytes_written);
} vnode_ops_t;

struct vnode
{
    vnode_type_t        type;
    uint64_t            inode;
    uint64_t            size;

    /*
     * Filesystem-specific information.
     *
     * ext2 can eventually store an ext2 inode reference here.
     * ramfs can store a ramfs node pointer here.
     */
    void                *private_data;
    const vnode_ops_t   *ops;

    /*
     * VFS reference count
     */
    volatile uint32_t   ref_count;
};

struct file
{
    vnode_t     *vnode;
    size_t      offset;
    uint32_t    flags;
};

/*
 * Initial VFS setup
 */
void vfs_initialize(void);

/*
 * Install the root vnode.
 * 
 * Initially RAMFS will call this
 * Later ext2_mount() can call this instead.
 */
bool vfs_set_root(vnode_t *root);

/*
 * Resolve a pathname.
 *
 * Example:
 *
 *     /etc/config
 *
 * becomes:
 *
 *     root
 *       -> lookup("etc")
 *       -> lookup("config")
 */
int vfs_lookup(const char *path, vnode_t **result);

void vnode_ref(vnode_t *node);
void vnode_unref(vnode_t *node);

/*
 * File API
 */
int vfs_open(const char *path, uint32_t flags, file_t **result);

int vfs_read(file_t *file, void *buffer, size_t size, size_t *bytes_read);

int vfs_write(file_t *file, const void *buffer, size_t size, size_t *bytes_written);

void vfs_close(file_t *file);

/*
 * vnode reference management
 */

#define VFS_OPEN_READ   0x0001u
#define VFS_OPEN_WRITE  0x0002u

#endif