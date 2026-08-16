#include <stdio.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/fd.h>
#include <kernel/heap.h>
#endif

int fclose(FILE *stream)
{
#if defined(__is_libk)

    int result;

    if (stream == NULL)
        return EOF;

    /**
     * stdin, stdout, and stderr are static FILE objects from
     * stream.c They must not be freed here.
     * 
     * This first fclose implementation is for dynamically opened
     * regular-file streams only.
     */
    if (stream == stdin ||
        stream == stdout ||
        stream == stderr)
        return EOF;

    result = kernel_fd_close(stream->fd);

    if (result != 0)
    {
        stream->flags |= _IO_ERROR;
        return EOF;
    }

    kfree(stream);

    return 0;

#else

    (void)stream;

    return EOF;

#endif
}