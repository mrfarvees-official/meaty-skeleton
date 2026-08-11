#include <kernel/vfs.h>
#include <kernel/test.h>
#include <stdio.h>
#include <string.h>

void ramfs_test(void)
{
    file_t *file;

    if (vfs_open(
            "/hello.txt",
            VFS_OPEN_READ,
            &file) != 0)
    {
        printf("VFS: failed to open /hello.txt\n");
        return;
    }

    char buffer[128];

    size_t bytes_read;

    if (vfs_read(
            file,
            buffer,
            sizeof(buffer) - 1,
            &bytes_read) != 0)
    {
        printf("VFS: read failed\n");

        vfs_close(file);
        return;
    }

    buffer[bytes_read] = '\0';

    printf(
        "VFS /hello.txt:\n%s",
        buffer);

    vfs_close(file);

    file_t *config;
    if (vfs_open("/etc/config", VFS_OPEN_READ, &config) != 0)
    {
        printf("VFS: failed to open /etc/config\n");
        return;
    }
    
    if (vfs_read(config, buffer, sizeof(buffer) - 1, &bytes_read) != 0)
    {
        printf("VFS: read failed\n");
        vfs_close(config);
        return;
    }

    buffer[bytes_read] = '\0';

    printf("VFS /etc/config\n%s", buffer);
    vfs_close(config);
}