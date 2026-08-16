#include <stdio.h>

int fflush(FILE *stream)
{
    /*
     * Current stdio is completely unbuffered.
     *
     * Every successful output operation has already reached the
     * terminal or VFS before it returns, so there is no pending
     * output to drain.
     *
     * fflush(NULL) therefore succeeds as well: all output streams
     * are already flushed.
     */
    if (stream == NULL)
        return 0;

    /*
     * For this implementation, only output-capable streams have
     * defined flushing behavior.
     */
    if (!(stream->flags & _IO_WRITE))
    {
        stream->flags |= _IO_ERROR;
        return EOF;
    }

    return 0;
}