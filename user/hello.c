#include <stddef.h>
#include <stdint.h>

#include "runtime.h"

#define SYS_GETTID 2u
#define SYS_THREAD_CREATE 6u

/*
 * Shared ELF data.
 *
 * The entire purpose of U12.4 is to prove that main and worker
 * execute with different stacks/tasks while accessing this SAME
 * userspace memory.
 */
static volatile uint32_t shared_value =
    0;

static volatile int32_t worker_tid =
    0;

/*
 * --------------------------------------------------------------------------
 * REGRESSION-ONLY SYSCALL HELPERS
 * --------------------------------------------------------------------------
 *
 * The shell-critical syscall ABI lives in runtime.c.
 *
 * These two helpers remain local only because U12.4 still needs its
 * historical gettid/thread-create regression.
 */

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

static int write_string(
    const char *string)
{
    return user_write_string(
        USER_STDOUT,
        string);
}

static int32_t gettid(void)
{
    return syscall0(
        SYS_GETTID);
}

static int32_t thread_create(
    void (*entry)(void))
{
    return syscall1(
        SYS_THREAD_CREATE,
        (uint32_t)(uintptr_t)entry);
}

static void thread_yield(void)
{
    user_yield();
}

static int32_t waitpid_nonblocking(
    uint32_t pid,
    int *status)
{
    return user_waitpid(
        pid,
        status);
}

static int32_t spawn_process(
    const char *path,
    uint32_t argc,
    const char *const argv[])
{
    return user_spawn(
        path,
        argc,
        argv);
}

static void thread_exit(
    int status)
    __attribute__((noreturn));

static void thread_exit(
    int status)
{
    user_exit(
        status);
}

/*
 * --------------------------------------------------------------------------
 * U12.4 WORKER THREAD
 * --------------------------------------------------------------------------
 *
 * The kernel enters this function directly with IRET.
 *
 * It therefore MUST NOT return.
 */
static void worker_thread(void)
    __attribute__((noreturn));

static void worker_thread(void)
{
    static const char started[] =
        "user: worker entered ring3\n";

    static const char shared_ok[] =
        "user: worker observed shared value=123\n";

    static const char updated[] =
        "user: worker changed shared value=456\n";

    static const char failed[] =
        "user: worker shared-memory test FAILED\n";

    worker_tid =
        gettid();

    if (worker_tid <= 0)
    {
        thread_exit(
            20);
    }

    if (write_string(
            started) != 0)
    {
        thread_exit(
            21);
    }

    /*
     * Main wrote this before SYS_THREAD_CREATE.
     *
     * If we share the same ELF address space we MUST see it.
     */
    if (shared_value !=
        123u)
    {
        (void)write_string(
            failed);

        thread_exit(
            22);
    }

    if (write_string(
            shared_ok) != 0)
    {
        thread_exit(
            23);
    }

    /*
     * Main must subsequently observe this write.
     */
    shared_value =
        456u;

    if (write_string(
            updated) != 0)
    {
        thread_exit(
            24);
    }

    thread_exit(
        0);
}

/*
 * --------------------------------------------------------------------------
 * MAIN ELF THREAD
 * ------sssssss--------------------------------------------------------------------
 */

int main(
    int argc,
    char **argv)
{
    static const char greeting[] =
        "Hello from ELF userspace!\n";

    static const char argc_ok[] =
        "user: argc=3\n";

    static const char argv0_prefix[] =
        "user: argv[0]=";

    static const char argv1_prefix[] =
        "user: argv[1]=";

    static const char argv2_prefix[] =
        "user: argv[2]=";

    static const char newline[] =
        "\n";

    static const char waitpid_starting[] =
        "user: testing waitpid syscall\n";

    static const char waitpid_nonchild_ok[] =
        "user: non-child waitpid returned false\n";

    static const char waitpid_bad_address_ok[] =
        "user: invalid status pointer rejected\n";

    static const char waitpid_passed[] =
        "user: WAITPID SYSCALL PLUMBING PASSED\n";

    static const char spawn_starting[] =
        "P1F user: spawning child process\n";

    static const char spawn_created[] =
        "P1F user: child process created\n";

    static const char spawn_collected[] =
        "P1F user: child collected status=73\n";

    static const char spawn_second_wait[] =
        "P1F user: second waitpid returned false\n";

    static const char spawn_passed[] =
        "P1F user: USERSPACE SPAWN/WAIT PASSED\n";

    static const char starting[] =
        "user: creating worker thread\n";

    static const char created[] =
        "user: worker has different TID\n";

    static const char shared_ok[] =
        "user: main observed worker value=456\n";

    static const char passed[] =
        "user: MULTITHREADING PASSED\n";

    /*
     * Existing argc/argv test.
     */
    if (write_string(
            greeting) != 0)
    {
        return 1;
    }

    if (argc != 3 ||
        argv == NULL ||
        argv[0] == NULL ||
        argv[1] == NULL ||
        argv[2] == NULL ||
        argv[3] != NULL)
    {
        return 2;
    }

    if (write_string(
            argc_ok) != 0)
    {
        return 3;
    }

    if (write_string(
            argv0_prefix) != 0 ||
        write_string(
            argv[0]) != 0 ||
        write_string(
            newline) != 0)
    {
        return 4;
    }

    if (write_string(
            argv1_prefix) != 0 ||
        write_string(
            argv[1]) != 0 ||
        write_string(
            newline) != 0)
    {
        return 5;
    }

    if (write_string(
            argv2_prefix) != 0 ||
        write_string(
            argv[2]) != 0 ||
        write_string(
            newline) != 0)
    {
        return 6;
    }

    /*
     * ----------------------------------------------------------
     * P1E regression
     * ----------------------------------------------------------
     */
    if (write_string(
            waitpid_starting) != 0)
    {
        return 40;
    }

    int wait_status =
        12345;

    int32_t wait_result =
        waitpid_nonblocking(
            1u,
            &wait_status);

    if (wait_result != 0 ||
        wait_status != 12345)
    {
        return 41;
    }

    if (write_string(
            waitpid_nonchild_ok) != 0)
    {
        return 42;
    }

    wait_result =
        waitpid_nonblocking(
            1u,
            (int *)(uintptr_t)1u);

    if (wait_result != -3)
    {
        return 43;
    }

    if (write_string(
            waitpid_bad_address_ok) != 0)
    {
        return 44;
    }

    wait_result =
        waitpid_nonblocking(
            1u,
            NULL);

    if (wait_result != 0)
    {
        return 45;
    }

    if (write_string(
            waitpid_passed) != 0)
    {
        return 46;
    }

    /*
     * ----------------------------------------------------------
     * P1F
     *
     * PID 2 creates a genuine child userspace process.
     * ----------------------------------------------------------
     */
    if (write_string(
            spawn_starting) != 0)
    {
        return 50;
    }

    static const char child_path[] =
        "/bin/spawn-child.nex";

    static const char child_arg1[] =
        "alpha";

    static const char child_arg2[] =
        "beta";

    const char *child_argv[] =
        {
            child_path,
            child_arg1,
            child_arg2,
            NULL};

    int32_t child_pid =
        spawn_process(
            child_path,
            3u,
            child_argv);

    if (child_pid <= 0)
    {
        return 51;
    }

    if (write_string(
            spawn_created) != 0)
    {
        return 52;
    }

    wait_status =
        -1;

    for (;;)
    {
        wait_result =
            waitpid_nonblocking(
                (uint32_t)child_pid,
                &wait_status);

        if (wait_result < 0)
        {
            return 53;
        }

        if (wait_result == 1)
        {
            break;
        }

        thread_yield();
    }

    if (wait_status !=
        73)
    {
        return 54;
    }

    if (write_string(
            spawn_collected) != 0)
    {
        return 55;
    }

    /*
     * Collection is one-shot.
     */
    wait_status =
        12345;

    wait_result =
        waitpid_nonblocking(
            (uint32_t)child_pid,
            &wait_status);

    if (wait_result != 0 ||
        wait_status != 12345)
    {
        return 56;
    }

    if (write_string(
            spawn_second_wait) != 0)
    {
        return 57;
    }

    if (write_string(
            spawn_passed) != 0)
    {
        return 58;
    }

    /*
     * ----------------------------------------------------------
     * U12.4 regression
     * ----------------------------------------------------------
     */
    int32_t main_tid =
        gettid();

    if (main_tid <= 0)
    {
        return 30;
    }

    shared_value =
        123u;

    worker_tid =
        0;

    if (write_string(
            starting) != 0)
    {
        return 31;
    }

    int32_t created_tid =
        thread_create(
            worker_thread);

    if (created_tid <= 0)
    {
        return 32;
    }

    while (shared_value !=
           456u)
    {
        thread_yield();
    }

    if (worker_tid <= 0 ||
        worker_tid !=
            created_tid ||
        worker_tid ==
            main_tid)
    {
        return 33;
    }

    if (write_string(
            created) != 0)
    {
        return 34;
    }

    if (shared_value !=
        456u)
    {
        return 35;
    }

    if (write_string(
            shared_ok) != 0)
    {
        return 36;
    }

    if (write_string(
            passed) != 0)
    {
        return 37;
    }

    return 0;
}
