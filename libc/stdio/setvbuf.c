#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__is_libk)
#include <kernel/heap.h>
#endif

int setvbuf(
    FILE *stream,
    char *buffer,
    int mode,
    size_t size)
{
    if (stream == NULL)
        return -1;

    /*
     * This phase implements output buffering only.
     *
     * Do not pretend input buffering exists yet.
     */
    if (!(stream->flags & _IO_WRITE))
        return -1;

    if (mode != _IOFBF &&
        mode != _IOLBF &&
        mode != _IONBF)
    {
        return -1;
    }

    if (mode != _IONBF &&
        size == 0)
    {
        return -1;
    }

    /*
     * Do not discard pending output while changing buffering
     * policy.
     */
    if (stream->write_buffer_used != 0)
    {
        if (fflush(stream) == EOF)
            return -1;
    }

#if defined(__is_libk)

    unsigned char *new_buffer = NULL;
    int new_buffer_owned = 0;

    /*
     * Fully-buffered and line-buffered streams require storage.
     *
     * If the caller supplies storage, the caller owns it.
     * Otherwise libc allocates it.
     */
    if (mode == _IOFBF ||
        mode == _IOLBF)
    {
        if (buffer != NULL)
        {
            new_buffer =
                (unsigned char *)buffer;
        }
        else
        {
            new_buffer =
                kmalloc(size);

            if (new_buffer == NULL)
                return -1;

            new_buffer_owned = 1;
        }
    }

    /*
     * Release the previous library-owned buffer only after the
     * replacement has been successfully prepared.
     */
    if ((stream->flags &
         _IO_BUFFER_OWNED) &&
        stream->write_buffer != NULL)
    {
        kfree(
            stream->write_buffer);
    }

    stream->flags &=
        ~_IO_BUFFER_OWNED;

    stream->write_buffer =
        new_buffer;

    if (mode == _IONBF)
    {
        stream->write_buffer_size = 0;
    }
    else
    {
        stream->write_buffer_size = size;
    }

    stream->write_buffer_used = 0;
    stream->buffering_mode = mode;

    if (new_buffer_owned)
    {
        stream->flags |=
            _IO_BUFFER_OWNED;
    }

    return 0;

#else

    (void)buffer;
    (void)size;

    return -1;

#endif
}