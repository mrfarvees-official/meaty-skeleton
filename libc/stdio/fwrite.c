#include <stdio.h>
#include <stddef.h>
#include <string.h>

#if defined(__is_libk)
#include <kernel/fd.h>
#endif

size_t fwrite(
    const void *ptr,
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
        if (stream != NULL)
            stream->flags |= _IO_ERROR;

        return 0;
    }

    if (!(stream->flags & _IO_WRITE))
    {
        stream->flags |= _IO_ERROR;
        return 0;
    }

    if (nmemb >
        ((size_t)-1) / size)
    {
        stream->flags |= _IO_ERROR;
        return 0;
    }

    size_t total_bytes =
        size * nmemb;

#if defined(__is_libk)

    /*
     * Buffered regular-file path.
     */
    if (stream->fd >= KERNEL_FD_FIRST &&
        stream->write_buffer != NULL &&
        stream->write_buffer_size != 0)
    {
        const unsigned char *data =
            (const unsigned char *)ptr;

        size_t consumed = 0;

        while (consumed < total_bytes)
        {
            size_t available =
                stream->write_buffer_size -
                stream->write_buffer_used;

            if (available == 0)
            {
                if (fflush(stream) == EOF)
                    return consumed / size;

                available =
                    stream->write_buffer_size;
            }

            size_t remaining =
                total_bytes - consumed;

            size_t chunk =
                remaining < available
                    ? remaining
                    : available;

            memcpy(
                stream->write_buffer +
                    stream->write_buffer_used,
                data + consumed,
                chunk);

            stream->write_buffer_used +=
                chunk;

            consumed +=
                chunk;
        }

        return nmemb;
    }

    /*
     * Fallback direct file write.
     */
    if (stream->fd >= KERNEL_FD_FIRST)
    {
        size_t bytes_written = 0;

        if (kernel_fd_write(
                stream->fd,
                ptr,
                total_bytes,
                &bytes_written) != 0)
        {
            stream->flags |= _IO_ERROR;
            return 0;
        }

        if (bytes_written >
            total_bytes)
        {
            stream->flags |= _IO_ERROR;
            return 0;
        }

        if (bytes_written <
            total_bytes)
        {
            stream->flags |= _IO_ERROR;
        }

        return bytes_written / size;
    }

#endif

    /*
     * stdout/stderr fallback.
     */
    const unsigned char *data =
        (const unsigned char *)ptr;

    size_t completed = 0;

    for (size_t element = 0;
         element < nmemb;
         ++element)
    {
        for (size_t byte = 0;
             byte < size;
             ++byte)
        {
            size_t offset =
                element * size +
                byte;

            if (fputc(
                    data[offset],
                    stream) == EOF)
            {
                return completed;
            }
        }

        ++completed;
    }

    return completed;
}