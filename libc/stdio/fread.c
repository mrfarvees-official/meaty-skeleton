#include <stdio.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/fd.h>
#endif

size_t fread(
    void *ptr,
    size_t size,
    size_t nmemb,
    FILE *stream)
{
    if (size == 0 ||
        nmemb == 0)
    {
        return 0;
    }

    if (ptr == NULL ||
        stream == NULL)
    {
        return 0;
    }

    if (!(stream->flags & _IO_READ))
    {
        stream->flags |= _IO_ERROR;
        return 0;
    }

    /*
     * Prevent overflow while calculating the requested byte count.
     */
    if (nmemb >
        ((size_t)-1) / size)
    {
        stream->flags |= _IO_ERROR;
        return 0;
    }

    unsigned char *destination =
        (unsigned char *)ptr;

    size_t total_bytes =
        size * nmemb;

    size_t bytes_read = 0;

    /*
     * Consume the one-byte stdio pushback first.
     *
     * The underlying fd offset was never moved backwards by
     * ungetc(), so this byte must not be requested from the fd
     * again.
     */
    if (stream->has_pushback)
    {
        destination[0] =
            (unsigned char)stream->pushback;

        stream->has_pushback = 0;
        bytes_read = 1;

        if (bytes_read == total_bytes)
            return bytes_read / size;
    }

#if defined(__is_libk)

    /*
     * VFS-backed regular files can read the remaining request
     * directly into the caller's buffer.
     */
    if (stream->fd >= KERNEL_FD_FIRST)
    {
        size_t kernel_bytes = 0;

        size_t remaining =
            total_bytes - bytes_read;

        if (kernel_fd_read(
                stream->fd,
                destination + bytes_read,
                remaining,
                &kernel_bytes) != 0)
        {
            stream->flags |= _IO_ERROR;

            return bytes_read / size;
        }

        if (kernel_bytes > remaining)
        {
            stream->flags |= _IO_ERROR;

            return bytes_read / size;
        }

        bytes_read += kernel_bytes;

        /*
         * For the current regular-file fd/VFS implementation,
         * a short successful read means the request reached EOF.
         *
         * Important:
         * an exact-size read does NOT set EOF. EOF is only observed
         * when the request cannot be completely satisfied.
         */
        if (kernel_bytes < remaining)
        {
            stream->flags |= _IO_EOF;
        }

        return bytes_read / size;
    }

#endif

    /*
     * stdin retains its existing character-at-a-time behavior.
     */
    while (bytes_read < total_bytes)
    {
        int c =
            fgetc(stream);

        if (c == EOF)
            break;

        destination[bytes_read++] =
            (unsigned char)c;
    }

    /*
     * fread() returns complete elements.
     *
     * A partial final element remains copied into ptr but is not
     * included in the return value.
     */
    return bytes_read / size;
}