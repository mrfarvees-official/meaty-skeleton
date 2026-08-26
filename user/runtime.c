#include <stddef.h>
#include <stdint.h>

#include "runtime.h"

#define SYS_EXIT            5u
#define SYS_YIELD           7u
#define SYS_WAITPID         8u
#define SYS_SPAWN           9u
#define SYS_READ            10u
#define SYS_WRITE           11u
#define SYS_OPEN            12u
#define SYS_CLOSE           13u
#define SYS_KEY_EVENT       14u
#define SYS_CHDIR           15u
#define SYS_GETCWD          16u
#define SYS_READDIR         17u
#define SYS_MKDIR           18u

#define USER_STDIO_CHUNK    128u


static int32_t syscall0(
    uint32_t number)
{
    uint32_t result =
        number;

    __asm__ volatile(
        "int $0x80"
        : "+a"(result)
        :
        : "memory", "cc");

    return (int32_t)result;
}


static int32_t syscall1(
    uint32_t number,
    uint32_t arg0)
{
    uint32_t result =
        number;

    __asm__ volatile(
        "int $0x80"
        : "+a"(result)
        : "b"(arg0)
        : "memory", "cc");

    return (int32_t)result;
}


static int32_t syscall2(
    uint32_t number,
    uint32_t arg0,
    uint32_t arg1)
{
    uint32_t result =
        number;

    __asm__ volatile(
        "int $0x80"
        : "+a"(result)
        : "b"(arg0),
          "c"(arg1)
        : "memory", "cc");

    return (int32_t)result;
}


static int32_t syscall3(
    uint32_t number,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t arg2)
{
    uint32_t result =
        number;

    __asm__ volatile(
        "int $0x80"
        : "+a"(result)
        : "b"(arg0),
          "c"(arg1),
          "d"(arg2)
        : "memory", "cc");

    return (int32_t)result;
}


static size_t string_length(
    const char *string)
{
    if (string == NULL)
        return 0u;

    size_t length =
        0u;

    while (string[length] !=
           '\0')
    {
        ++length;
    }

    return length;
}


int32_t user_key_event(void)
{
    return syscall0(
        SYS_KEY_EVENT);
}


int32_t user_read(
    int fd,
    void *buffer,
    size_t count)
{
    return syscall3(
        SYS_READ,
        (uint32_t)fd,
        (uint32_t)(uintptr_t)buffer,
        (uint32_t)count);
}


int32_t user_write(
    int fd,
    const void *buffer,
    size_t count)
{
    return syscall3(
        SYS_WRITE,
        (uint32_t)fd,
        (uint32_t)(uintptr_t)buffer,
        (uint32_t)count);
}


int user_write_string(
    int fd,
    const char *string)
{
    if (string == NULL)
        return -1;

    size_t remaining =
        string_length(
            string);

    const char *position =
        string;

    while (remaining != 0u)
    {
        size_t chunk =
            remaining;

        if (chunk >
            USER_STDIO_CHUNK)
        {
            chunk =
                USER_STDIO_CHUNK;
        }

        int32_t result =
            user_write(
                fd,
                position,
                chunk);

        if (result <= 0)
            return -1;

        size_t written =
            (size_t)result;

        if (written >
            chunk)
        {
            return -1;
        }

        position +=
            written;

        remaining -=
            written;
    }

    return 0;
}


int32_t user_open(
    const char *path,
    uint32_t flags)
{
    return syscall2(
        SYS_OPEN,
        (uint32_t)(uintptr_t)path,
        flags);
}


int32_t user_close(
    int fd)
{
    return syscall1(
        SYS_CLOSE,
        (uint32_t)fd);
}


int32_t user_spawn(
    const char *path,
    uint32_t argc,
    const char *const argv[])
{
    return syscall3(
        SYS_SPAWN,
        (uint32_t)(uintptr_t)path,
        argc,
        (uint32_t)(uintptr_t)argv);
}


int32_t user_waitpid(
    uint32_t pid,
    int *status)
{
    return syscall2(
        SYS_WAITPID,
        pid,
        (uint32_t)(uintptr_t)status);
}


void user_yield(void)
{
    (void)syscall0(
        SYS_YIELD);
}


void user_exit(
    int status)
{
    (void)syscall1(
        SYS_EXIT,
        (uint32_t)status);

    for (;;)
    {
        __asm__ volatile(
            "ud2");
    }
}

int32_t user_chdir(
    const char *path)
{
    return syscall1(
        SYS_CHDIR,
        (uint32_t)(uintptr_t)path);
}

int32_t user_getcwd(
    char *buffer,
    size_t capacity)
{
    return syscall2(
        SYS_GETCWD,
        (uint32_t)(uintptr_t)buffer,
        (uint32_t)capacity);
}

int32_t user_readdir(
    int fd,
    user_dirent_t *entry)
{
    return syscall2(
        SYS_READDIR,
        (uint32_t)fd,
        (uint32_t)(uintptr_t)entry);
}

int32_t user_mkdir(
    const char *path)
{
    if (path == NULL)
        return -1;

    return syscall1(
        SYS_MKDIR,
        (uint32_t)(uintptr_t)
            path);
}