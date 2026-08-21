#include <stddef.h>
#include <stdint.h>

#define SYS_GETTID        2u
#define SYS_DEBUG_WRITE   4u
#define SYS_EXIT          5u
#define SYS_THREAD_CREATE 6u
#define SYS_YIELD         7u


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
 * SYSCALL HELPERS
 * --------------------------------------------------------------------------
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


static size_t string_length(
    const char *string)
{
    size_t length =
        0;

    while (string[length] !=
           '\0')
    {
        ++length;
    }

    return length;
}


static int write_string(
    const char *string)
{
    size_t length =
        string_length(
            string);

    int32_t result =
        syscall2(
            SYS_DEBUG_WRITE,
            (uint32_t)
                (uintptr_t)string,
            (uint32_t)length);

    return result ==
            (int32_t)length
        ? 0
        : -1;
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
        (uint32_t)
            (uintptr_t)entry);
}


static void thread_yield(void)
{
    (void)syscall0(
        SYS_YIELD);
}


static void thread_exit(
    int status)
    __attribute__((noreturn));

static void thread_exit(
    int status)
{
    (void)syscall1(
        SYS_EXIT,
        (uint32_t)status);

    /*
     * SYS_EXIT must never return.
     */
    for (;;)
    {
        __asm__ volatile(
            "ud2");
    }
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
        "U12.4 user: worker entered ring3\n";

    static const char shared_ok[] =
        "U12.4 user: worker observed shared value=123\n";

    static const char updated[] =
        "U12.4 user: worker changed shared value=456\n";

    static const char failed[] =
        "U12.4 user: worker shared-memory test FAILED\n";

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
 * --------------------------------------------------------------------------
 */

int main(
    int argc,
    char **argv)
{
    static const char greeting[] =
        "Hello from ELF userspace!\n";

    static const char argc_ok[] =
        "U10 user: argc=3\n";

    static const char argv0_prefix[] =
        "U10 user: argv[0]=";

    static const char argv1_prefix[] =
        "U10 user: argv[1]=";

    static const char argv2_prefix[] =
        "U10 user: argv[2]=";

    static const char newline[] =
        "\n";

    static const char starting[] =
        "U12.4 user: creating worker thread\n";

    static const char created[] =
        "U12.4 user: worker has different TID\n";

    static const char shared_ok[] =
        "U12.4 user: main observed worker value=456\n";

    static const char passed[] =
        "U12.4 user: MULTITHREADING PASSED\n";


    /*
     * Keep the existing ELF argc/argv regression test.
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
     * ------------------------------------------------------------------
     * U12.4
     * ------------------------------------------------------------------
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

    /*
     * Wait until worker proves shared address-space visibility.
     *
     * Yield rather than burning the current CPU.
     */
    while (shared_value !=
           456u)
    {
        thread_yield();
    }

    /*
     * The kernel returned the newly allocated TID.
     * The worker independently obtains gettid().
     *
     * Both must agree and must differ from main.
     */
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

    /*
     * crt0 converts this into SYS_EXIT(0).
     */
    return 0;
}