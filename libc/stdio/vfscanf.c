#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>

/**
 * First scanf implementation.
 *
 * Support conversations:
 *
 *      %d
 *      %u
 *      %x
 *      %c
 *      %s
 *      %%
 *
 * Supported field widths:
 *
 *      %5d
 *      %5u
 *      %5x
 *      %5c
 *      %5s
 *
 * Length modifiers, assignment suppression, floating point,
 * scansets, %n, and pointer conversations are intentionally
 * not implemented yet.
 */

static int scan_is_space(int c)
{
    return c == ' ' ||
           c == '\t' ||
           c == '\f' ||
           c == '\n' ||
           c == '\r' ||
           c == '\v';
}

static int scan_digit_value(int c, int base)
{
    int value;

    if (c >= '0' && c <= '9')
        value = c - '0';
    else if (c >= 'a' && c <= 'f')
        value = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        value = c - 'A' + 10;
    else
        return -1;

    if (value >= base)
        return -1;

    return value;
}

/**
 * Consume input whitespace and leave the first non-whitespace
 * character available to next operation.
 * 
 * Returns of EOF if EOF is encountered before a non-whitespace
 * character is found. 
 */
static int scan_skip_space(FILE *stream)
{
    int c;

    for (;;)
    {
        c = fgetc(stream);

        if (c == EOF)
            return EOF;
        
        if (!scan_is_space(c))
        {
            ungetc(c, stream);
            return 0;
        }
    }
}

/**
 * Read an integer conversion.
 * 
 * base: 
 *      10 for %d and %u
 *      16 for %x
 * 
 * is_signed:
 *      non-zero for %d
 *      zero for %u and %x
 * 
 * Returns:
 *      1 assignment succeeded
 *      0 matching failure
 *      EOF input failure before a usable input character
 */
static int scan_integer(
    FILE *stream,
    int base,
    int is_signed,
    int width,
    va_list *args)
{
    unsigned int value = 0;
    int negative = 0;
    int digits = 0;
    int consumed = 0;
    int unlimited;
    int c;
    int digit;

    unlimited = (width == 0);

    if (scan_skip_space(stream) == EOF)
        return EOF;

    /**
     * Optional sign.
     * 
     * A sign counts against field width.
     */
    if (unlimited || consumed < width)
    {
        c = fgetc(stream);

        if (c == EOF)
            return EOF;

        if (c == '+' || c == '-')
        {
            negative = (c == '-');
            consumed++;
        }
        else
            ungetc(c, stream);
    }

    while (unlimited || consumed < width)
    {
        c = fgetc(stream);

        if (c == EOF)
            break;

        digit = scan_digit_value(c, base);

        if (digit < 0)
        {
            /**
             * This is the first character not belonging to the 
             * numeric input item. Leave it for next read.
             */
            ungetc(c, stream);
            break;
        }

        value = value * (unsigned int)base + (unsigned int)digit;

        digits++;
        consumed++;
    }

    if (digits == 0)
    {
        /**
         * If a sign was consumed but no digit followed, this is a
         * matching failure. The sign is part of the failed input 
         * item and is not pushed back.
         */
        return 0;
    }

    if (is_signed)
    {
        int *destination = va_arg(*args, int *);

        if (negative)
            *destination = (int)(0U - value);
        else
            *destination = (int)value;
    }
    else
    {
        unsigned int *destination = va_arg(*args,  unsigned int*);

        if (negative)
            *destination = 0U - value;
        else
            *destination = value;
    }

    return 1;
}

static int scan_string(
    FILE *stream,
    int width,
    va_list *args)
{
    char *destination;
    int count = 0;
    int unlimited;
    int c;

    unlimited = (width == 0);

    if (scan_skip_space(stream) == EOF)
        return EOF;

    destination = va_arg(*args, char *);

    while (unlimited || count < width)
    {
        c = fgetc(stream);

        if (c == EOF)
            break;

        if (scan_is_space(c))
        {
            ungetc(c, stream);
            break;
        }

        destination[count++] = (char)c;
    }

    if (count == 0)
        return 0;

    destination[count] = '\0';

    return 1;
}

static int scan_characters(
    FILE *stream,
    int width,
    va_list *args)
{
    char *destination;
    int count = 0;
    int wanted;
    int c;

    /**
     * Unlike the other supported conversions, %c does NOT skip
     * leading whitespace
     */
    wanted = width ? width : 1;
    destination = va_arg(*args, char *);

    while (count < wanted)
    {
        c = fgetc(stream);

        if (c == EOF)
        {
            /**
             * %c requires the complete requested field.
             * Characters already consumed remain consumed, but
             * there is no assignment unless the field completed.
             */
            if (count == 0)
                return EOF;

            return 0;
        }

        destination[count++] = (char)c;
    }

    return 1;
}

int vfscanf(
    FILE *stream, 
    const char *format, 
    va_list args)
{
    int assignments = 0;
    int c;

    if (stream == NULL || format == NULL)
        return EOF;

    while (*format != '\0')
    {
        /**
         * Whitespace in the format consumes any amount of 
         * whitespace in the input.
         */
        if (scan_is_space((unsigned char)*format))
        {
            do 
            {
                format++;
            }
            while(scan_is_space((unsigned char)*format));

            for (;;)
            {
                c = fgetc(stream);

                if (c == EOF)
                    break;

                if (!scan_is_space(c))
                {
                    ungetc(c, stream);
                    break;
                }
            }

            continue;
        }

        /**
         * Ordinary format character.
         */
        if (*format != '%')
        {
            c = fgetc(stream);

            if (c == EOF)
                return assignments ? assignments : EOF;
            
            if (c != (unsigned char)*format)
            {
                ungetc(c, stream);
                return assignments;
            }

            format++;
            continue;
        }

        /**
         * Conversion specification.
         */
        format++;

        if (*format == '\0')
            return assignments;

        /**
         * Literal percent.
         */
        if (*format == '%')
        {
            c = fgetc(stream);

            if (c == EOF)
                return assignments ? assignments : EOF;

            if (c != '%')
            {
                ungetc(c, stream);
                return assignments;
            }

            format++;
            continue;
        }

        /**
         * Optional decimal field width
         */
        {
            int width = 0;
            int result;

            while (*format >= '0' && *format <= '9')
            {
                width = width * 10 + (*format - '0');
                format++;
            }

            if (*format == '\0')
                return assignments;

            switch(*format)
            {
                case 'd':
                    result = scan_integer(
                        stream,
                        10,
                        1,
                        width, 
                        &args);
                    break;

                case 'u':
                    result = scan_integer(
                        stream,
                        10,
                        0,
                        width,
                        &args);
                    break;

                case 'x':
                    result = scan_integer(
                        stream,
                        16,
                        0,
                        width, 
                        &args);
                    break;
                
                case 'c':
                    result = scan_characters(
                        stream,
                        width,
                        &args);
                    break;

                case 's':
                    result = scan_string(
                        stream,
                        width,
                        &args);
                    break;

                default:
                    /**
                     * Unsupported converstion in the first phase 
                     * Stop rather than guessing about arguments.
                     */
                    return assignments;
            }

            if (result == EOF)
                return assignments ? assignments : EOF;
            
            if (result == 0)
                return assignments;

            assignments++;
            format++;
        }
    }

    return assignments;
}

int fscanf(FILE *stream, const char *format, ...)
{
    va_list args;
    int result;

    va_start(args, format);
    result = vfscanf(stream, format, args);
    va_end(args);

    return result;
}

int scanf(const char *format, ...)
{
    va_list args;
    int result;

    va_start(args, format);
    result = vfscanf(stdin, format, args);
    va_end(args);

    return result;
}