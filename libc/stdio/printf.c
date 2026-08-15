#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define FLOAT_BUFFER_SIZE 512
#define FLOAT_MAX_PRECISION 64

static char *__int_str(
	intmax_t i,
	char b[],
	int base,
	bool plusSignIfNeeded,
	bool spaceSignIfNeeded,
	int paddingNo,
	bool justify,
	bool zeroPad)
{
	char digit[32] = {0};
	memset(digit, 0, sizeof(digit));
	strcpy(digit, "0123456789");

	if (base == 16)
	{
		strcat(digit, "ABCDEF");
	}
	else if (base == 17)
	{
		strcat(digit, "abcdef");
		base = 16;
	}

	if (base < 2 || base > 16)
	{
		b[0] = '\0';
		return b;
	}

	if (paddingNo < 0)
	{
		paddingNo = 0;
	}

	if (paddingNo > 255)
	{
		paddingNo = 255;
	}

	char *p = b;
	uintmax_t value;

	if (i < 0)
	{
		*p++ = '-';

		/*
		 * This safely handles INTMAX_MIN.
		 * Directly writing i = -i could overflow.
		 */
		value = (uintmax_t)(-(i + 1)) + 1;
	}
	else
	{
		value = (uintmax_t)i;

		if (plusSignIfNeeded)
		{
			*p++ = '+';
		}
		else if (spaceSignIfNeeded)
		{
			*p++ = ' ';
		}
	}

	uintmax_t shifter = value;

	do
	{
		p++;
		shifter /= (uintmax_t)base;
	} while (shifter != 0);

	*p = '\0';

	do
	{
		*--p = digit[value % (uintmax_t)base];
		value /= (uintmax_t)base;
	} while (value != 0);

	int padding = paddingNo - (int)strlen(b);

	if (padding < 0)
	{
		padding = 0;
	}

	if (justify)
	{
		size_t length = strlen(b);

		while (padding-- > 0 && length < 255)
		{
			b[length++] = ' ';
		}

		b[length] = '\0';
	}
	else
	{
		char a[256] = {0};
		size_t output = 0;
		size_t input = 0;

		/*
		 * With zero padding, place the sign before the zeros.
		 *
		 * Correct:
		 *     -0042
		 *
		 * Incorrect:
		 *     00-42
		 */
		if (zeroPad &&
			(b[0] == '-' || b[0] == '+' || b[0] == ' '))
		{
			a[output++] = b[input++];
		}

		while (padding-- > 0 && output < 255)
		{
			a[output++] = zeroPad ? '0' : ' ';
		}

		while (b[input] != '\0' && output < 255)
		{
			a[output++] = b[input++];
		}

		a[output] = '\0';
		strcpy(b, a);
	}

	return b;
}

static char *__uint_str(
	uintmax_t i,
	char b[],
	int base,
	bool plusSignIfNeeded,
	bool spaceSignIfNeeded,
	int paddingNo,
	bool justify,
	bool zeroPad)
{
	char digit[32] = {0};
	memset(digit, 0, sizeof(digit));
	strcpy(digit, "0123456789");

	if (base == 16)
	{
		strcat(digit, "ABCDEF");
	}
	else if (base == 17)
	{
		strcat(digit, "abcdef");
		base = 16;
	}

	if (base < 2 || base > 16)
	{
		b[0] = '\0';
		return b;
	}

	if (paddingNo < 0)
	{
		paddingNo = 0;
	}

	if (paddingNo > 255)
	{
		paddingNo = 255;
	}

	char *p = b;

	/*
	 * Standard printf normally ignores '+' and space for unsigned
	 * conversions. These arguments are retained to match your
	 * existing function structure.
	 */
	(void)plusSignIfNeeded;
	(void)spaceSignIfNeeded;

	uintmax_t shifter = i;

	do
	{
		p++;
		shifter /= (uintmax_t)base;
	} while (shifter != 0);

	*p = '\0';

	do
	{
		*--p = digit[i % (uintmax_t)base];
		i /= (uintmax_t)base;
	} while (i != 0);

	int padding = paddingNo - (int)strlen(b);

	if (padding < 0)
	{
		padding = 0;
	}

	if (justify)
	{
		size_t length = strlen(b);

		while (padding-- > 0 && length < 255)
		{
			b[length++] = ' ';
		}

		b[length] = '\0';
	}
	else
	{
		char a[256] = {0};
		size_t output = 0;
		size_t input = 0;

		while (padding-- > 0 && output < 255)
		{
			a[output++] = zeroPad ? '0' : ' ';
		}

		while (b[input] != '\0' && output < 255)
		{
			a[output++] = b[input++];
		}

		a[output] = '\0';
		strcpy(b, a);
	}

	return b;
}

static void displayCharacter(FILE *stream, char c, int *count)
{
	if (fputc((unsigned char)c, stream) == EOF)
		return;
	*count += 1;
}

static void displayString(FILE *stream, const char *string, int *count)
{
	if (string == NULL)
	{
		string = "(null)";
	}

	for (size_t i = 0; string[i] != '\0'; ++i)
	{
		displayCharacter(stream, string[i], count);
	}
}

static void displayRepeatedCharacter(FILE *stream, char c, int amount, int *count)
{
	while (amount-- > 0)
	{
		displayCharacter(stream, c, count);
	}
}

static void displayPaddedFloat(
	FILE *stream,
	const char *text,
	int width,
	bool left_justify,
	bool zero_pad,
	int *count)
{
	int text_length = (int)strlen(text);
	int padding = width - text_length;

	if (padding < 0)
	{
		padding = 0;
	}

	if (!left_justify)
	{
		if (zero_pad &&
			(text[0] == '-' || text[0] == '+' || text[0] == ' '))
		{
			displayCharacter(stream, text[0], count);
			displayRepeatedCharacter(stream, '0', padding, count);
			displayString(stream, text + 1, count);
			return;
		}

		displayRepeatedCharacter(
			stream,
			zero_pad ? '0' : ' ',
			padding,
			count);
	}

	displayString(stream, text, count);

	if (left_justify)
	{
		displayRepeatedCharacter(stream, ' ', padding, count);
	}
}

static bool float_is_nan(long double value)
{
	return value != value;
}

static bool float_is_inf(long double value)
{
	return value != 0.0L && value + value == value;
}

static bool float_is_negative(long double value)
{
	/*
	 * Handles normal negative numbers.
	 * Negative zero handling depends on compiler support.
	 */
	return value < 0.0L;
}

static char float_digit(unsigned int digit, bool uppercase)
{
	if (digit < 10)
	{
		return (char)('0' + digit);
	}

	return (char)((uppercase ? 'A' : 'a') + digit - 10);
}

static char *append_unsigned_decimal(
	char *output,
	char *end,
	unsigned int value,
	int minimum_digits)
{
	char temporary[32];
	int count = 0;

	do
	{
		temporary[count++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0U && count < (int)sizeof(temporary));

	while (count < minimum_digits &&
		   count < (int)sizeof(temporary))
	{
		temporary[count++] = '0';
	}

	while (count > 0 && output < end)
	{
		*output++ = temporary[--count];
	}

	return output;
}

static bool round_digit_buffer(
	char *begin,
	char *last_digit,
	unsigned int base)
{
	char *position = last_digit;

	while (position >= begin)
	{
		if (*position == '.')
		{
			--position;
			continue;
		}

		unsigned int digit;

		if (*position >= '0' && *position <= '9')
		{
			digit = (unsigned int)(*position - '0');
		}
		else if (*position >= 'a' && *position <= 'f')
		{
			digit = (unsigned int)(*position - 'a') + 10U;
		}
		else if (*position >= 'A' && *position <= 'F')
		{
			digit = (unsigned int)(*position - 'A') + 10U;
		}
		else
		{
			--position;
			continue;
		}

		if (digit + 1U < base)
		{
			++*position;
			return false;
		}

		if (digit + 1U == base)
		{
			*position = '0';
			--position;
			continue;
		}

		--position;
	}

	return true;
}

static bool insert_leading_one(
	char *buffer,
	char **output,
	char *end)
{
	if (*output >= end)
	{
		return false;
	}

	for (char *position = *output; position > buffer; --position)
	{
		*position = position[-1];
	}

	*buffer = '1';
	++*output;
	return true;
}

static char *format_fixed_long_double(
	long double value,
	char *buffer,
	size_t buffer_size,
	int precision,
	bool alternate_form)
{
	if (buffer_size == 0)
	{
		return buffer;
	}

	if (precision < 0)
	{
		precision = 6;
	}

	if (precision > FLOAT_MAX_PRECISION)
	{
		precision = FLOAT_MAX_PRECISION;
	}

	char *output = buffer;
	char *end = buffer + buffer_size - 1;

	if (value > (long double)UINTMAX_MAX)
	{
		buffer[0] = '\0';
		return buffer;
	}

	uintmax_t integer = (uintmax_t)value;
	long double fractional_part = value - (long double)integer;

	char integer_digits[128];
	int integer_count = 0;

	do
	{
		integer_digits[integer_count++] =
			(char)('0' + integer % 10U);
		integer /= 10U;
	} while (integer != 0 &&
			 integer_count < (int)sizeof(integer_digits));

	while (integer_count > 0 && output < end)
	{
		*output++ = integer_digits[--integer_count];
	}

	if ((precision > 0 || alternate_form) && output < end)
	{
		*output++ = '.';
	}

	for (int index = 0; index < precision && output < end; ++index)
	{
		fractional_part *= 10.0L;

		unsigned int digit = (unsigned int)fractional_part;
		if (digit > 9U)
		{
			digit = 9U;
		}

		*output++ = (char)('0' + digit);
		fractional_part -= (long double)digit;
	}

	/*
	 * Generate one extra decimal digit and use it only for rounding.
	 * This avoids adding an inexact decimal rounding unit to the value.
	 */
	fractional_part *= 10.0L;
	unsigned int rounding_digit = (unsigned int)fractional_part;

	if (rounding_digit >= 5U && output > buffer)
	{
		bool carry = round_digit_buffer(buffer, output - 1, 10U);

		if (carry)
		{
			insert_leading_one(buffer, &output, end);
		}
	}

	*output = '\0';
	return buffer;
}

static char *format_scientific_long_double(
	long double value,
	char *buffer,
	size_t buffer_size,
	int precision,
	bool uppercase,
	bool alternate_form)
{
	if (buffer_size == 0)
	{
		return buffer;
	}

	if (precision < 0)
	{
		precision = 6;
	}

	if (precision > FLOAT_MAX_PRECISION)
	{
		precision = FLOAT_MAX_PRECISION;
	}

	char *output = buffer;
	char *end = buffer + buffer_size - 1;
	int exponent = 0;

	if (value != 0.0L)
	{
		while (value >= 10.0L && exponent < 10000)
		{
			value /= 10.0L;
			++exponent;
		}

		while (value < 1.0L && exponent > -10000)
		{
			value *= 10.0L;
			--exponent;
		}
	}

	char *digits_begin = output;

	unsigned int leading_digit = (unsigned int)value;
	if (leading_digit > 9U)
	{
		leading_digit = 9U;
	}

	if (output < end)
	{
		*output++ = (char)('0' + leading_digit);
	}

	value -= (long double)leading_digit;

	if ((precision > 0 || alternate_form) && output < end)
	{
		*output++ = '.';
	}

	for (int index = 0; index < precision && output < end; ++index)
	{
		value *= 10.0L;

		unsigned int digit = (unsigned int)value;
		if (digit > 9U)
		{
			digit = 9U;
		}

		*output++ = (char)('0' + digit);
		value -= (long double)digit;
	}

	value *= 10.0L;
	unsigned int rounding_digit = (unsigned int)value;

	if (rounding_digit >= 5U && output > digits_begin)
	{
		bool carry =
			round_digit_buffer(digits_begin, output - 1, 10U);

		if (carry)
		{
			/*
			 * 9.999... rounded across the leading digit.
			 * Keep the mantissa normalized as 1.000... and
			 * increase the decimal exponent.
			 */
			*digits_begin = '1';

			for (char *position = digits_begin + 1;
				 position < output;
				 ++position)
			{
				if (*position != '.')
				{
					*position = '0';
				}
			}

			++exponent;
		}
	}

	if (output < end)
	{
		*output++ = uppercase ? 'E' : 'e';
	}

	if (output < end)
	{
		if (exponent < 0)
		{
			*output++ = '-';
			exponent = -exponent;
		}
		else
		{
			*output++ = '+';
		}
	}

	output = append_unsigned_decimal(
		output,
		end,
		(unsigned int)exponent,
		2);

	*output = '\0';
	return buffer;
}

static void remove_trailing_fraction_zeros(char *buffer)
{
	char *exponent = strchr(buffer, 'e');

	if (exponent == NULL)
	{
		exponent = strchr(buffer, 'E');
	}

	char *fraction_end;

	if (exponent != NULL)
	{
		fraction_end = exponent - 1;
	}
	else
	{
		fraction_end = buffer + strlen(buffer) - 1;
	}

	while (fraction_end >= buffer && *fraction_end == '0')
	{
		--fraction_end;
	}

	if (fraction_end >= buffer && *fraction_end == '.')
	{
		--fraction_end;
	}

	char *destination = fraction_end + 1;

	if (exponent != NULL)
	{
		while (*exponent != '\0')
		{
			*destination++ = *exponent++;
		}
	}

	*destination = '\0';
}

static char *format_general_long_double(
	long double value,
	char *buffer,
	size_t buffer_size,
	int precision,
	bool uppercase,
	bool alternate_form)
{
	if (precision < 0)
	{
		precision = 6;
	}

	if (precision == 0)
	{
		precision = 1;
	}

	int exponent = 0;
	long double normalized = value;

	if (normalized != 0.0L)
	{
		while (normalized >= 10.0L && exponent < 10000)
		{
			normalized /= 10.0L;
			++exponent;
		}

		while (normalized < 1.0L && exponent > -10000)
		{
			normalized *= 10.0L;
			--exponent;
		}
	}

	if (exponent < -4 || exponent >= precision)
	{
		format_scientific_long_double(
			value,
			buffer,
			buffer_size,
			precision - 1,
			uppercase,
			alternate_form);
	}
	else
	{
		int fractional_precision = precision - exponent - 1;

		if (fractional_precision < 0)
		{
			fractional_precision = 0;
		}

		format_fixed_long_double(
			value,
			buffer,
			buffer_size,
			fractional_precision,
			alternate_form);
	}

	if (!alternate_form)
	{
		remove_trailing_fraction_zeros(buffer);
	}

	return buffer;
}

static char *format_hex_long_double(
	long double value,
	char *buffer,
	size_t buffer_size,
	int precision,
	bool uppercase,
	bool alternate_form)
{
	if (buffer_size == 0)
	{
		return buffer;
	}

	if (precision < 0)
	{
		precision = 16;
	}

	if (precision > FLOAT_MAX_PRECISION)
	{
		precision = FLOAT_MAX_PRECISION;
	}

	char *output = buffer;
	char *end = buffer + buffer_size - 1;
	int exponent = 0;

	if (value != 0.0L)
	{
		while (value >= 2.0L && exponent < 100000)
		{
			value /= 2.0L;
			++exponent;
		}

		while (value < 1.0L && exponent > -100000)
		{
			value *= 2.0L;
			--exponent;
		}
	}

	if (output < end)
	{
		*output++ = '0';
	}

	if (output < end)
	{
		*output++ = uppercase ? 'X' : 'x';
	}

	char *digits_begin = output;

	if (output < end)
	{
		*output++ = value == 0.0L ? '0' : '1';
	}

	if ((precision > 0 || alternate_form) && output < end)
	{
		*output++ = '.';
	}

	long double fraction =
		value == 0.0L ? 0.0L : value - 1.0L;

	for (int index = 0; index < precision && output < end; ++index)
	{
		fraction *= 16.0L;

		unsigned int digit = (unsigned int)fraction;
		if (digit > 15U)
		{
			digit = 15U;
		}

		*output++ = float_digit(digit, uppercase);
		fraction -= (long double)digit;
	}

	fraction *= 16.0L;
	unsigned int rounding_digit = (unsigned int)fraction;

	if (rounding_digit >= 8U && output > digits_begin)
	{
		bool carry =
			round_digit_buffer(digits_begin, output - 1, 16U);

		if (carry)
		{
			/*
			 * 0x1.ffff... rounded to 0x2.000...
			 * Renormalize it as 0x1.000...p+(exponent + 1).
			 */
			*digits_begin = '1';

			for (char *position = digits_begin + 1;
				 position < output;
				 ++position)
			{
				if (*position != '.')
				{
					*position = '0';
				}
			}

			++exponent;
		}
	}

	if (output < end)
	{
		*output++ = uppercase ? 'P' : 'p';
	}

	if (output < end)
	{
		if (exponent < 0)
		{
			*output++ = '-';
			exponent = -exponent;
		}
		else
		{
			*output++ = '+';
		}
	}

	output = append_unsigned_decimal(
		output,
		end,
		(unsigned int)exponent,
		1);

	*output = '\0';
	return buffer;
}

static char *format_long_double(
	long double value,
	char specifier,
	char *buffer,
	size_t buffer_size,
	int precision,
	bool precision_specified,
	bool alternate_form,
	bool plus_sign,
	bool space_sign)
{
	if (buffer_size == 0)
	{
		return buffer;
	}

	bool uppercase =
		specifier == 'F' ||
		specifier == 'E' ||
		specifier == 'G' ||
		specifier == 'A';

	char *output = buffer;
	size_t remaining = buffer_size;

	bool negative = float_is_negative(value);

	if (negative)
	{
		*output++ = '-';
		--remaining;
		value = -value;
	}
	else if (plus_sign)
	{
		*output++ = '+';
		--remaining;
	}
	else if (space_sign)
	{
		*output++ = ' ';
		--remaining;
	}

	if (float_is_nan(value))
	{
		strncpy(
			output,
			uppercase ? "NAN" : "nan",
			remaining);

		buffer[buffer_size - 1] = '\0';
		return buffer;
	}

	if (float_is_inf(value))
	{
		strncpy(
			output,
			uppercase ? "INF" : "inf",
			remaining);

		buffer[buffer_size - 1] = '\0';
		return buffer;
	}

	int actual_precision =
		precision_specified ? precision : -1;

	switch (specifier)
	{
	case 'f':
	case 'F':
		format_fixed_long_double(
			value,
			output,
			remaining,
			actual_precision,
			alternate_form);
		break;

	case 'e':
	case 'E':
		format_scientific_long_double(
			value,
			output,
			remaining,
			actual_precision,
			uppercase,
			alternate_form);
		break;

	case 'g':
	case 'G':
		format_general_long_double(
			value,
			output,
			remaining,
			actual_precision,
			uppercase,
			alternate_form);
		break;

	case 'a':
	case 'A':
		format_hex_long_double(
			value,
			output,
			remaining,
			actual_precision,
			uppercase,
			alternate_form);
		break;

	default:
		*output = '\0';
		break;
	}

	return buffer;
}

int vfprintf(FILE *stream, const char *format, va_list list)
{
	if (stream == NULL || format == NULL)
	{
		return -1;
	}

	int chars = 0;
	char intStrBuffer[256] = {0};

	for (int i = 0; format[i] != '\0'; ++i)
	{
		if (format[i] != '%')
		{
			displayCharacter(stream, format[i], &chars);
			continue;
		}

		++i;

		/*
		 * Handle a trailing '%' safely.
		 */
		if (format[i] == '\0')
		{
			displayCharacter(stream, '%', &chars);
			break;
		}

		char specifier = '\0';
		char length = '\0';

		int lengthSpec = 0;
		int precSpec = 0;

		bool precisionSpecified = false;
		bool leftJustify = false;
		bool zeroPad = false;
		bool spaceNoSign = false;
		bool altForm = false;
		bool plusSign = false;

		/*
		 * Parse flags.
		 */
		bool parsingFlags = true;

		while (parsingFlags)
		{
			switch (format[i])
			{
			case '-':
				leftJustify = true;
				++i;
				break;

			case '+':
				plusSign = true;
				++i;
				break;

			case '#':
				altForm = true;
				++i;
				break;

			case ' ':
				spaceNoSign = true;
				++i;
				break;

			case '0':
				zeroPad = true;
				++i;
				break;

			default:
				parsingFlags = false;
				break;
			}
		}

		/*
		 * Parse field width.
		 */
		while (isdigit((unsigned char)format[i]))
		{
			if (lengthSpec <= 25)
			{
				lengthSpec *= 10;
				lengthSpec += format[i] - '0';
			}
			else
			{
				lengthSpec = 255;
			}

			++i;
		}

		if (format[i] == '*')
		{
			lengthSpec = va_arg(list, int);
			++i;

			if (lengthSpec < 0)
			{
				leftJustify = true;

				if (lengthSpec == INT_MIN)
				{
					lengthSpec = 255;
				}
				else
				{
					lengthSpec = -lengthSpec;
				}
			}
		}

		if (lengthSpec > 255)
		{
			lengthSpec = 255;
		}

		/*
		 * '-' overrides '0'.
		 */
		if (leftJustify)
		{
			zeroPad = false;
		}

		/*
		 * Parse precision.
		 */
		if (format[i] == '.')
		{
			precisionSpecified = true;
			precSpec = 0;
			++i;

			while (isdigit((unsigned char)format[i]))
			{
				if (precSpec <= 25)
				{
					precSpec *= 10;
					precSpec += format[i] - '0';
				}
				else
				{
					precSpec = 255;
				}

				++i;
			}

			if (format[i] == '*')
			{
				precSpec = va_arg(list, int);
				++i;

				if (precSpec < 0)
				{
					precisionSpecified = false;
					precSpec = 0;
				}
			}

			if (precSpec > 255)
			{
				precSpec = 255;
			}
		}

		/*
		 * Parse length modifier.
		 *
		 * H means hh.
		 * q means ll.
		 */
		if (format[i] == 'h' ||
			format[i] == 'l' ||
			format[i] == 'j' ||
			format[i] == 'z' ||
			format[i] == 't' ||
			format[i] == 'L')
		{
			length = format[i];
			++i;

			if (length == 'h' && format[i] == 'h')
			{
				length = 'H';
				++i;
			}
			else if (length == 'l' && format[i] == 'l')
			{
				length = 'q';
				++i;
			}
		}

		specifier = format[i];

		memset(intStrBuffer, 0, sizeof(intStrBuffer));

		switch (specifier)
		{
		case '%':
		{
			displayCharacter(stream, '%', &chars);
			break;
		}

		case 'c':
		{
			char character = (char)va_arg(list, int);
			int padding = lengthSpec > 1 ? lengthSpec - 1 : 0;

			if (!leftJustify)
			{
				displayRepeatedCharacter(stream, ' ', padding, &chars);
			}

			displayCharacter(stream, character, &chars);

			if (leftJustify)
			{
				displayRepeatedCharacter(stream, ' ', padding, &chars);
			}

			break;
		}

		case 's':
		{
			const char *string = va_arg(list, const char *);

			if (string == NULL)
			{
				string = "(null)";
			}

			size_t stringLength = strlen(string);

			if (precisionSpecified &&
				(size_t)precSpec < stringLength)
			{
				stringLength = (size_t)precSpec;
			}

			int padding = lengthSpec - (int)stringLength;

			if (padding < 0)
			{
				padding = 0;
			}

			if (!leftJustify)
			{
				displayRepeatedCharacter(stream, ' ', padding, &chars);
			}

			for (size_t j = 0; j < stringLength; ++j)
			{
				displayCharacter(stream, string[j], &chars);
			}

			if (leftJustify)
			{
				displayRepeatedCharacter(stream, ' ', padding, &chars);
			}

			break;
		}

		case 'd':
		case 'i':
		{
			if (precisionSpecified)
			{
				zeroPad = false;
			}

			switch (length)
			{
			case 0:
			{
				int integer = va_arg(list, int);

				__int_str(
					integer,
					intStrBuffer,
					10,
					plusSign,
					spaceNoSign,
					lengthSpec,
					leftJustify,
					zeroPad);

				break;
			}

			case 'H':
			{
				signed char integer =
					(signed char)va_arg(list, int);

				__int_str(
					integer,
					intStrBuffer,
					10,
					plusSign,
					spaceNoSign,
					lengthSpec,
					leftJustify,
					zeroPad);

				break;
			}

			case 'h':
			{
				short integer =
					(short)va_arg(list, int);

				__int_str(
					integer,
					intStrBuffer,
					10,
					plusSign,
					spaceNoSign,
					lengthSpec,
					leftJustify,
					zeroPad);

				break;
			}

			case 'l':
			{
				long integer = va_arg(list, long);

				__int_str(
					integer,
					intStrBuffer,
					10,
					plusSign,
					spaceNoSign,
					lengthSpec,
					leftJustify,
					zeroPad);

				break;
			}

			case 'q':
			{
				long long integer =
					va_arg(list, long long);

				__int_str(
					integer,
					intStrBuffer,
					10,
					plusSign,
					spaceNoSign,
					lengthSpec,
					leftJustify,
					zeroPad);

				break;
			}

			case 'j':
			{
				intmax_t integer =
					va_arg(list, intmax_t);

				__int_str(
					integer,
					intStrBuffer,
					10,
					plusSign,
					spaceNoSign,
					lengthSpec,
					leftJustify,
					zeroPad);

				break;
			}

			case 'z':
			{
				intptr_t integer =
					va_arg(list, intptr_t);

				__int_str(
					integer,
					intStrBuffer,
					10,
					plusSign,
					spaceNoSign,
					lengthSpec,
					leftJustify,
					zeroPad);

				break;
			}

			case 't':
			{
				ptrdiff_t integer =
					va_arg(list, ptrdiff_t);

				__int_str(
					integer,
					intStrBuffer,
					10,
					plusSign,
					spaceNoSign,
					lengthSpec,
					leftJustify,
					zeroPad);

				break;
			}

			default:
				intStrBuffer[0] = '\0';
				break;
			}

			displayString(stream, intStrBuffer, &chars);
			break;
		}

		case 'u':
		case 'o':
		case 'x':
		case 'X':
		{
			int base = 10;

			if (specifier == 'o')
			{
				base = 8;
			}
			else if (specifier == 'x')
			{
				base = 17;
			}
			else if (specifier == 'X')
			{
				base = 16;
			}

			if (precisionSpecified)
			{
				zeroPad = false;
			}

			uintmax_t integer = 0;

			switch (length)
			{
			case 0:
				integer =
					va_arg(list, unsigned int);
				break;

			case 'H':
				integer =
					(unsigned char)va_arg(list, unsigned int);
				break;

			case 'h':
				integer =
					(unsigned short)va_arg(list, unsigned int);
				break;

			case 'l':
				integer =
					va_arg(list, unsigned long);
				break;

			case 'q':
				integer =
					va_arg(list, unsigned long long);
				break;

			case 'j':
				integer =
					va_arg(list, uintmax_t);
				break;

			case 'z':
				integer =
					va_arg(list, size_t);
				break;

			case 't':
				integer =
					va_arg(list, uintptr_t);
				break;

			default:
				integer = 0;
				break;
			}

			char prefix[3] = {0};
			int prefixLength = 0;

			if (altForm && integer != 0)
			{
				if (specifier == 'x')
				{
					strcpy(prefix, "0x");
					prefixLength = 2;
				}
				else if (specifier == 'X')
				{
					strcpy(prefix, "0X");
					prefixLength = 2;
				}
				else if (specifier == 'o')
				{
					strcpy(prefix, "0");
					prefixLength = 1;
				}
			}

			int numberWidth = lengthSpec;

			if (prefixLength > 0 && numberWidth >= prefixLength)
			{
				numberWidth -= prefixLength;
			}

			if (prefixLength > 0 && zeroPad && !leftJustify)
			{
				displayString(stream, prefix, &chars);

				__uint_str(
					integer,
					intStrBuffer,
					base,
					false,
					false,
					numberWidth,
					false,
					true);

				displayString(stream, intStrBuffer, &chars);
			}
			else
			{
				__uint_str(
					integer,
					intStrBuffer,
					base,
					false,
					false,
					numberWidth,
					leftJustify,
					zeroPad);

				if (!leftJustify)
				{
					displayString(stream, prefix, &chars);
					displayString(stream, intStrBuffer, &chars);
				}
				else
				{
					size_t currentLength =
						strlen(intStrBuffer);

					while (currentLength > 0 &&
						   intStrBuffer[currentLength - 1] == ' ')
					{
						intStrBuffer[--currentLength] = '\0';
					}

					displayString(stream, prefix, &chars);
					displayString(stream, intStrBuffer, &chars);

					int used =
						prefixLength + (int)currentLength;

					int padding = lengthSpec - used;

					if (padding > 0)
					{
						displayRepeatedCharacter(
							stream,
							' ',
							padding,
							&chars);
					}
				}
			}

			break;
		}

		case 'p':
		{
			void *pointer = va_arg(list, void *);
			uintptr_t value = (uintptr_t)pointer;

			int numberWidth = lengthSpec;

			if (numberWidth >= 2)
			{
				numberWidth -= 2;
			}
			else
			{
				numberWidth = 0;
			}

			if (zeroPad && !leftJustify)
			{
				displayString(stream, "0x", &chars);

				__uint_str(
					(uintmax_t)value,
					intStrBuffer,
					17,
					false,
					false,
					numberWidth,
					false,
					true);

				displayString(stream, intStrBuffer, &chars);
			}
			else if (!leftJustify)
			{
				__uint_str(
					(uintmax_t)value,
					intStrBuffer,
					17,
					false,
					false,
					0,
					false,
					false);

				int digitsLength =
					(int)strlen(intStrBuffer);

				int padding =
					lengthSpec - digitsLength - 2;

				if (padding < 0)
				{
					padding = 0;
				}

				displayRepeatedCharacter(
					stream,
					' ',
					padding,
					&chars);

				displayString(stream, "0x", &chars);
				displayString(stream, intStrBuffer, &chars);
			}
			else
			{
				__uint_str(
					(uintmax_t)value,
					intStrBuffer,
					17,
					false,
					false,
					0,
					false,
					false);

				displayString(stream, "0x", &chars);
				displayString(stream, intStrBuffer, &chars);

				int used =
					2 + (int)strlen(intStrBuffer);

				int padding = lengthSpec - used;

				if (padding > 0)
				{
					displayRepeatedCharacter(
						stream,
						' ',
						padding,
						&chars);
				}
			}

			break;
		}

		case 'n':
		{
			switch (length)
			{
			case 'H':
				*va_arg(list, signed char *) =
					(signed char)chars;
				break;

			case 'h':
				*va_arg(list, short *) =
					(short)chars;
				break;

			case 0:
				*va_arg(list, int *) = chars;
				break;

			case 'l':
				*va_arg(list, long *) =
					(long)chars;
				break;

			case 'q':
				*va_arg(list, long long *) =
					(long long)chars;
				break;

			case 'j':
				*va_arg(list, intmax_t *) =
					(intmax_t)chars;
				break;

			case 'z':
				*va_arg(list, size_t *) =
					(size_t)chars;
				break;

			case 't':
				*va_arg(list, ptrdiff_t *) =
					(ptrdiff_t)chars;
				break;

			default:
				break;
			}

			break;
		}

		case 'f':
		case 'F':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
		case 'a':
		case 'A':
		{
			char float_buffer[FLOAT_BUFFER_SIZE];

			long double value;

			if (length == 'L')
			{
				value = va_arg(list, long double);
			}
			else
			{
				value = (long double)va_arg(list, double);
			}

			format_long_double(
				value,
				specifier,
				float_buffer,
				sizeof(float_buffer),
				precSpec,
				precisionSpecified,
				altForm,
				plusSign,
				spaceNoSign);

			displayPaddedFloat(
				stream,
				float_buffer,
				lengthSpec,
				leftJustify,
				zeroPad,
				&chars);

			break;
		}

		default:
		{
			displayCharacter(stream, '%', &chars);

			if (specifier != '\0')
			{
				displayCharacter(stream, specifier, &chars);
			}

			break;
		}
		}
	}

	return chars;
}

int vprintf(const char *format, va_list list)
{
	return vfprintf(stdout, format, list);
}

int fprintf(FILE *stream, const char *format, ...)
{
	va_list list;

	va_start(list, format);
	int result = vfprintf(stream, format, list);
	va_end(list);

	return result;
}

int printf(const char *format, ...)
{
	va_list list;

	va_start(list, format);
	int result = vfprintf(stdout, format, list);
	va_end(list);

	return result;
}