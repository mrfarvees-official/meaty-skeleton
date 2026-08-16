#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <kernel/vfs.h>
#include <kernel/heap.h>

static vnode_t *vfs_root = NULL;

/*
 * Return one pathname component.
 *
 * "/usr/bin/test"
 *
 * first call:
 *      usr
 *
 * second:
 *      bin
 *
 * third:
 *      test
 */
static const char *vfs_next_component(const char *path, char *component, size_t component_size)
{
    size_t length = 0;

    /*
     * Skip '/'
     */
    while (*path == '/')
        path++;

    if (*path == '\0')
    {
        component[0] = '\0';
        return path;
    }

    while (*path != '\0' && *path != '/')
    {
        if (length + 1 >= component_size)
            return NULL;
        component[length++] = *path++;
    }

    component[length] = '\0';

    return path;
}

void vfs_initialize(void)
{
    vfs_root = NULL;
}

void vnode_ref(vnode_t *node)
{
    if (node == NULL)
        return;

    __atomic_add_fetch(&node->ref_count, 1, __ATOMIC_RELAXED);
}

void vnode_unref(vnode_t *node)
{
    if (node == NULL)
        return;

    /*
     * We're not freeing vnode objects here yet because the
     * filesytem owns them.
     *
     * Later inode/vnode cache can handle destruction
     */
    __atomic_sub_fetch(&node->ref_count, 1, __ATOMIC_RELEASE);
}

bool vfs_set_root(vnode_t *root)
{
    if (root == NULL)
        return false;

    if (root->type != VNODE_DIRECTORY)
        return false;

    /*
     * For now there may only be one root filesystem
     */
    if (vfs_root != NULL)
        return false;

    vnode_ref(root);

    vfs_root = root;

    return true;
}

static int vfs_lookup_parent(
    const char *path,
    vnode_t **parent_result,
    char *name,
    size_t name_size)
{
    if (path == NULL ||
        parent_result == NULL ||
        name == NULL ||
        name_size == 0)
    {
        return -1;
    }

    if (path[0] != '/')
        return -1;

    vnode_t *current =
        vfs_root;

    vnode_ref(current);

    const char *cursor =
        path;

    char component[256];

    for (;;)
    {
        cursor = vfs_next_component(
            cursor,
            component,
            sizeof(component));

        if (cursor == NULL)
        {
            vnode_unref(current);
            return -1;
        }

        if (component[0] == '\0')
        {
            vnode_unref(current);
            return -1;
        }

        /*
         * Look ahead to determine whether component is the
         * final pathname element.
         */
        const char *next_cursor =
            cursor;

        char next_component[256];

        next_cursor =
            vfs_next_component(
                next_cursor,
                next_component,
                sizeof(next_component));

        if (next_cursor == NULL)
        {
            vnode_unref(current);
            return -1;
        }

        if (next_component[0] == '\0')
        {
            size_t length =
                strlen(component);

            if (length + 1 > name_size)
            {
                vnode_unref(current);
                return -1;
            }

            memcpy(
                name,
                component,
                length + 1);

            *parent_result =
                current;

            return 0;
        }

        if (current->type !=
            VNODE_DIRECTORY)
        {
            vnode_unref(current);
            return -1;
        }

        if (current->ops == NULL ||
            current->ops->lookup == NULL)
        {
            vnode_unref(current);
            return -1;
        }

        vnode_t *next = NULL;

        if (current->ops->lookup(
                current,
                component,
                &next) != 0)
        {
            vnode_unref(current);
            return -1;
        }

        vnode_unref(current);

        current = next;
    }
}

int vfs_lookup(const char *path, vnode_t **result)
{
    if (path == NULL || result == NULL)
        return -1;

    if (vfs_root == NULL)
        return -1;

    /*
     * For now only absolute paths are supported
     */
    if (path[0] != '/')
        return -1;

    vnode_t *current = vfs_root;
    vnode_ref(current);

    /*
     * "/" directly resolves to root.
     */
    if (path[1] == '\0')
    {
        *result = current;
        return 0;
    }

    char component[256];

    const char *cursor = path;

    for (;;)
    {
        cursor = vfs_next_component(cursor, component, sizeof(component));

        if (cursor == NULL)
        {
            vnode_unref(current);
            return -1;
        }

        if (component[0] == '\0')
            break;

        if (current->type != VNODE_DIRECTORY)
        {
            vnode_unref(current);
            return -1;
        }

        if (current->ops == NULL || current->ops->lookup == NULL)
        {
            vnode_unref(current);
            return -1;
        }

        vnode_t *next = NULL;

        if (current->ops->lookup(current, component, &next) != 0)
        {
            vnode_unref(current);
            return -1;
        }

        if (next == NULL)
        {
            vnode_unref(current);
            return -1;
        }

        vnode_unref(current);
        current = next;
    }

    *result = current;
    return 0;
}

int vfs_open(const char *path, uint32_t flags, file_t **result)
{
    if (path == NULL || result == NULL)
        return -1;

    vnode_t *node = NULL;

    if (vfs_lookup(path, &node) != 0)
    {
        if ((flags & VFS_OPEN_CREATE) == 0)
            return -1;

        vnode_t *parent = NULL;
        char name[256];

        if (vfs_lookup_parent(
                path,
                &parent,
                name,
                sizeof(name)) != 0)
        {
            return -1;
        }

        if (parent->type != VNODE_DIRECTORY ||
            parent->ops == NULL ||
            parent->ops->create == NULL)
        {
            vnode_unref(parent);
            return -1;
        }

        int create_result =
            parent->ops->create(
                parent,
                name,
                &node);

        vnode_unref(parent);

        if (create_result != 0 ||
            node == NULL)
        {
            return -1;
        }
    }

    /*
     * For now don't allow opening directories as files.
     */
    if (node->type != VNODE_REGULAR)
    {
        vnode_unref(node);
        return -1;
    }

    if (flags & VFS_OPEN_TRUNC)
    {
        /*
         * Truncation only makes sense for a writable open.
         */
        if ((flags & VFS_OPEN_WRITE) == 0)
        {
            vnode_unref(node);
            return -1;
        }

        if (node->ops == NULL ||
            node->ops->truncate == NULL)
        {
            vnode_unref(node);
            return -1;
        }

        if (node->ops->truncate(
                node,
                0) != 0)
        {
            vnode_unref(node);
            return -1;
        }
    }

    file_t *file = kmalloc(sizeof(file_t));

    if (file == NULL)
    {
        vnode_unref(node);
        return -1;
    }

    file->vnode = node;
    file->offset = 0;
    file->flags = flags;

    *result = file;

    return 0;
}

int vfs_read(file_t *file, void *buffer, size_t size, size_t *bytes_read)
{
    if (file == NULL || buffer == NULL || bytes_read == NULL)
        return -1;

    *bytes_read = 0;

    if ((file->flags & VFS_OPEN_READ) == 0)
        return -1;

    vnode_t *node = file->vnode;

    if (node == NULL || node->ops == NULL || node->ops->read == NULL)
        return -1;

    size_t count = 0;

    int result = node->ops->read(
        node,
        file->offset,
        buffer,
        size,
        &count);

    if (result != 0)
        return result;

    file->offset += count;

    *bytes_read = count;

    return 0;
}

int vfs_write(file_t *file, const void *buffer, size_t size, size_t *bytes_written)
{
    if (file == NULL || buffer == NULL || bytes_written == NULL)
        return -1;

    *bytes_written = 0;

    if ((file->flags & VFS_OPEN_WRITE) == 0)
        return -1;

    vnode_t *node = file->vnode;

    if (node == NULL || node->ops == NULL || node->ops->write == NULL)
        return -1;

    size_t count = 0;

    if (file->flags & VFS_OPEN_APPEND)
        file->offset = node->size;

    int result =
        node->ops->write(
            node,
            file->offset,
            buffer,
            size,
            &count);

    if (result != 0)
        return result;

    file->offset += count;

    *bytes_written = count;

    return 0;
}

void vfs_close(file_t *file)
{
    if (file == NULL)
        return;

    vnode_unref(file->vnode);

    kfree(file);
}