#include <stdio.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/fd.h>
#endif

size_t fwrite(
    const void *ptr,
    size_t size,
    size_t nmemb,
    FILE *stream)
{
    if (size == 0 || nmemb == 0) 
        return 0;

    if (ptr == NULL || stream == NULL)
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

    /**
     * Prevent overflow when calculating the complete byte count.
     */
    if (nmemb > ((size_t)-1) / size)
    {
        stream->flags |= _IO_ERROR;
        return 0;
    }

    size_t total_bytes = size * nmemb;

#if defined(__is_libk)

    /**
     * Regular VFS-backed files can transfer the entire fwrite()
     * request in one kernel fd write
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

        /**
         * The kernel should never report more bytes than requested.
         */
        if (bytes_written > total_bytes)
        {
            stream->flags |= _IO_ERROR;
            return 0;
        }

        /**
         * fwrite() returns complete elements, not bytes.
         * 
         * A short write is considered an output error in the 
         * current unbuffered stdio implementation.
         */
        if (bytes_written < total_bytes)
        {
            stream->flags |= _IO_ERROR;
        }

        return bytes_written / size;
    }

#endif

    /**
     * stdout/stderr still use the character output path.
     * 
     * Keep this fallback rather than mixing terminal details into
     * fwrite().
     */
    const unsigned char *data = (const unsigned char *)ptr;

    size_t completed = 0;

    for (size_t element = 0; element < nmemb; ++element)
    {
        for (size_t byte = 0; byte < size; ++byte)
        {
            size_t offset = element * size + byte;

            if (fputc(data[offset], stream) == EOF)
                return EOF;
        }

        ++completed;
    }

    return completed;
}