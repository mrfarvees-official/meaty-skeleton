#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <kernel/vfs.h>
#include <kernel/ramfs.h>

typedef struct ramfs_node
{
    const char          *name;
    vnode_t             vnode;
    const uint8_t       *data;
    size_t              data_size;
    struct ramfs_node   *children;
    size_t              child_count; 
} ramfs_node_t;

/*
 * Forward declaration
 */
static int ramfs_lookup(vnode_t *directory, const char *name, vnode_t **result);

static int ramfs_read(vnode_t *node, size_t offset, void *buffer, size_t size, size_t *bytes_read);

static const vnode_ops_t ramfs_directory_ops = 
{
    .lookup = ramfs_lookup,
    .read = NULL,
    .write = NULL
};

static const vnode_ops_t ramfs_file_ops = 
{
    .lookup = NULL,
    .read = ramfs_read,
    .write = NULL
};

/*
 * Test files
 */
static const uint8_t hello_data[] = 
    "Hello from VFS\n";
static const uint8_t config_data[] = 
    "host=myos\n"
    "smp=yes\n";

static ramfs_node_t etc_children[] =
{
    {
        .name = "config",
        .vnode =
        {
            .type = VNODE_REGULAR,
            .inode = 3,
            .size = sizeof(config_data) - 1,
            .private_data = NULL,
            .ops = &ramfs_file_ops,
            .ref_count = 0
        },
        .data = config_data,
        .data_size = sizeof(config_data) - 1,
        .children = NULL,
        .child_count = 0
    }
};

static ramfs_node_t root_children[] =
{
    {
        .name = "hello.txt",
        .vnode = 
        {
            .type = VNODE_REGULAR,
            .inode = 2,
            .size = sizeof(hello_data) - 1,
            .private_data = NULL,
            .ops = &ramfs_file_ops,
            .ref_count = 0
        },
        .data = hello_data,
        .data_size = sizeof(hello_data) - 1,
        .children = NULL,
        .child_count = 0
    },

    {
        .name = "etc",
        .vnode =
        {
            .type = VNODE_DIRECTORY,
            .inode = 4,
            .size = 0,
            .private_data = NULL,
            .ops = &ramfs_directory_ops,
            .ref_count = 0
        },
        .data = NULL,
        .data_size = 0,
        .children = etc_children,
        .child_count = sizeof(etc_children) / sizeof(etc_children[0])
    }
};

static ramfs_node_t root_node = 
{
    .name = "/",
    .vnode =
    {
        .type = VNODE_DIRECTORY,
        .inode = 1,
        .size = 0,
        .private_data = NULL,
        .ops = &ramfs_directory_ops,
        .ref_count = 0
    },
    .data = NULL,
    .data_size = 0,
    .children = root_children,
    .child_count = sizeof(root_children) / sizeof(root_children[0])
};

static int ramfs_lookup(vnode_t *directory, const char *name, vnode_t **result)
{
    if (directory == NULL || name == NULL || result == NULL) return -1;

    ramfs_node_t *directory_node = (ramfs_node_t*)directory->private_data;
    
    if (directory_node == NULL) return -1;

    for(size_t i = 0; i< directory_node->child_count; i++)
    {
        ramfs_node_t *child = &directory_node->children[i];

        if (strcmp(child->name, name) == 0)
        {
            vnode_ref(&child->vnode);
            *result = &child->vnode;
            return 0;
        }
    }

    return -1;
}

static int ramfs_read(vnode_t *node, size_t offset, void *buffer, size_t size, size_t *bytes_read)
{
    if (node == NULL || buffer == NULL || bytes_read == NULL) return -1;

    ramfs_node_t *ram_node = (ramfs_node_t*)node->private_data;

    if (ram_node == NULL) return -1;

    if (offset >= ram_node->data_size) 
    {
        *bytes_read = 0;
        return 0;
    }

    size_t available = ram_node->data_size - offset;
    size_t count = size < available ? size : available;
    memcpy(buffer, ram_node->data + offset, count);
    *bytes_read = count;

    return 0;
}

bool ramfs_initialize(void)
{
    /*
     * Connect vnode -> ramfs object.
     */
    root_node.vnode.private_data = &root_node;

    for (size_t i =0; i < root_node.child_count; i++)
    {
        ramfs_node_t *child = &root_node.children[i];
        child->vnode.private_data = child;
    }

    for (size_t i = 0; i < sizeof(etc_children) / sizeof(etc_children[0]); i++)
        etc_children[i].vnode.private_data = &etc_children[i];

    return vfs_set_root(&root_node.vnode);
}