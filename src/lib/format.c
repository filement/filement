#include "format.h"

// TODO: fix dependency on libc

// TODO: possible future improvements:
//  format_sint			puts + sign for positive numbers
//  format_real
//  custom digits for format_hex and format_base64
//  format_base64 context (like in glib)
//  format_base64 custom alphabet
//    char *format_base64(char *restrict buffer, const uint8_t *restrict bytes, size_t length, uint32_t context, unsigned flush, char alphabet[65])
//		this is too complex; find a way to simplify it
//  length functions
//  memory allocation functions

// TODO implement format_real in order to use it in json_dump to increase the speed

// TODO: optimize for bases 8, 10, 16

static const unsigned char digits[64] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_", padding = '.';

// Writes string representation in base base of number in buffer.
// WARNING: buffer and number must be lvalues. TODO: explain what should base be
// _next specifies how to iterate buffer after each character (it's either ++ or -- ).
#define _format_digits(buffer, number, base, _next) do \
	{ \
		do *(buffer)_next = digits[(size_t)((number) % (base))]; \
		while ((number) /= (base)); \
	} while (false)

#define _format_uint_nofill_internal(buffer, number, base) do \
	{ \
		char *start = (buffer); \
		_format_digits((buffer), (number), (base), ++); \
		char *end = (buffer)--; \
		char swap; \
		/* reverse digits to obtain the number */ \
		while (start < (buffer)) \
		{ \
			swap = *(buffer); \
			*(buffer) = *start; \
			*start = swap; \
			++start, --(buffer); \
		} \
		return end; \
	} while (false)

char *_format_uint_nofill(char *restrict buffer, uint64_t number, uint8_t base)
{
	_format_uint_nofill_internal(buffer, number, base);
}

// If number can't fit in length bytes, the behavior is undefined.
char *_format_uint_fill(char *restrict buffer, uint64_t number, uint8_t base, uint16_t length, char fill)
{
	char *end = buffer + length, *position = end - 1;
	_format_digits(position, number, base, --);
	while (position >= buffer) *position-- = fill;
	return end;
}

uint16_t format_uint_length(uint64_t number, uint8_t base)
{
	uint16_t length = 1;
	while (number /= base) ++length;
	return length;
}

char *_format_int_nofill(char *restrict buffer, int64_t number, uint8_t base)
{
	if (number < 0)
	{
		*buffer++ = '-';
		number = -number;
	}
	_format_uint_nofill_internal(buffer, number, base);
}

// If number can't fit in length bytes, the behavior is undefined.
char *_format_int_fill(char *restrict buffer, int64_t number, uint8_t base, uint16_t length, char fill)
{
	uint64_t unumber;
	if (number < 0) unumber = -number;
	else unumber = number;

	char *end = buffer + length, *position = end - 1;
	_format_digits(position, unumber, base, --);
	if (number < 0) *position-- = '-';
	while (position >= buffer) *position-- = fill;

	return end;
}

uint16_t format_int_length(int64_t number, uint8_t base)
{
	uint16_t length = 1;
	if (number < 0)
	{
		number = -number;
		length += 1;
	}
	while (number /= base) ++length;
	return length;
}

/*#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

//#define rand() ((unsigned)(rand)())

#define T(t) ((t).tv_sec * 1000000 + (t).tv_usec)

#define start() gettimeofday(&s, 0)
#define end(str) (gettimeofday(&e, 0), fprintf(stderr, "%s%u\n", (str), (unsigned)(T(e) - T(s))))

#define N 1048576
//#define N 3

static int data[N];

int main(void)
{
	char buffer[4096];

	struct timeval s, e;
	size_t i, j;

	srand(time(0));

	start();
	for(i = 0; i < N; ++i)
		data[i] = rand();
	end("generated: ");

	size_t index;

	start();
	for(i = 0; i < N; ++i)
	{
		index = print_int(buffer, data[i], 10) - buffer;
		index = print_int(buffer + index, data[i + 1], 10) - buffer;
		index = print_int(buffer + index, data[i], 10) - buffer;
		index = print_int(buffer + index, data[i + 1], 10) - buffer;
		buffer[index] = 0;

		//print_int(print_int(print_int(buffer, data[i], 10), data[i + 1], 10), data[i], 10) = 0;
	}
	end("format: ");

	start();
	for(i = 0; i < N; i += 2)
		sprintf(buffer, "%d%d%d%d", data[i], data[i + 1], data[i], data[i + 1]);
	end("sprintf: ");


	*print_int(buffer, -138) = 0;
	printf("%s\n", buffer);
	*print_int(buffer, -138, 10) = 0;
	printf("%s\n", buffer);
	*print_int(buffer, -138, 16) = 0;
	printf("%s\n", buffer);

	*print_int(buffer, -138, 16, 6) = 0;
	printf("%s\n", buffer);

	*print_int(buffer, -138, 8, 6, '0') = 0;
	printf("%s\n", buffer);

	return 0;
}*/

char *format_hex(char *restrict buffer, const uint8_t *restrict bytes, size_t length)
{
	size_t i;
	for(i = 0; i < length; ++i)
	{
		*buffer++ = digits[(bytes[i] >> 4) & 0x0f];
		*buffer++ = digits[bytes[i] & 0x0f];
	}
	return buffer;
}

char *format_base64(char *restrict buffer, const uint8_t *restrict bytes, size_t length)
{
	uint32_t block = 0;
	char *start = buffer;
	size_t index;
	size_t remaining; // TODO the name of this variable is not consistent with its usage

	for(index = 2; index < length; index += 3)
	{
		// Store block of 24 bits in block.
		block = (bytes[index - 2] << 16) | (bytes[index - 1] << 8) | bytes[index];

		*start++ = digits[block >> 18];
		*start++ = digits[(block >> 12) & 0x3f];
		*start++ = digits[(block >> 6) & 0x3f];
		*start++ = digits[block & 0x3f];
	}

	// Encode the remaining bytes.
	block = 0;
	switch (remaining = index - length)
	{
	case 2: // no more bytes
		return start;

	case 0: // 2 bytes remaining
		block |= (bytes[index - 1] << 8);
		start[2] = digits[(block >> 6) & 0x3f];
	case 1: // 1 byte remaining
		block |= (bytes[index - 2] << 16);
		start[0] = digits[block >> 18];
		start[1] = digits[(block >> 12) & 0x3f];
		return start + 3 - remaining;
	}
}

/*
format_base64_step()
format_base64_flush()

char *destination
uint8_t *source
size_t length
char alphabet[]
uint32_t *state
bool padding
*/

// TODO: fix this
size_t hex2bin(unsigned char *restrict dest, const unsigned char *src, size_t length)
{
	static const char digits[256] = {
		['0'] =  0, ['1'] =  1, ['2'] =  2, ['3'] =  3,
		['4'] =  4, ['5'] =  5, ['6'] =  6, ['7'] =  7,
		['8'] =  8, ['9'] =  9, ['a'] = 10, ['b'] = 11,
		['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15};
	size_t index;
	for(index = 0; (index * 2) < length; ++index)
		dest[index] = (digits[(int)src[index * 2]] << 4) | digits[(int)src[index * 2 + 1]];
	return index;
}

static const unsigned char base64_int[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,   62,    0, 0xff,
	   0,    1,    2,    3,    4,    5,    6,    7,    8,    9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff,   36,   37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,   48,   49,   50,
	  51,   52,   53,   54,   55,   56,   57,   58,   59,   60,   61, 0xff, 0xff, 0xff, 0xff,   63,
	0xff,   10,   11,   12,   13,   14,   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,
	  25,   26,   27,   28,   29,   30,   31,   32,   33,   34,   35, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
static const unsigned char BASE64_PADDING = '.';

// TODO move this to parse.c
// TODO fix this function
size_t parse_base64_length(const unsigned char *restrict data, size_t length)
{
	if (!length || (length % 4)) return 0;
	//if (length % 4) return ((length & ~0x3) / 4) * 3 + (length % 4) - 1;
	else return (length / 4) * 3 - (data[length - 1] == BASE64_PADDING) - (data[length - 2] == BASE64_PADDING);
}

// TODO move this to parse.c
// WARNING: This implementation doesn't handle = properly.
size_t parse_base64(const unsigned char *src, unsigned char *restrict dest, size_t length)
{
	uint32_t block = 0;
	unsigned char value;
	unsigned char phase = 0;

	if (!length || (length % 4)) return 0;

	const unsigned char *end = src + length;
	length = (length / 4) * 3;
	while (true)
	{
		value = base64_int[src[0]];
		if (value > 63) return -1;
		block = (block << 6) | value;

		++src;
		if (++phase == 4)
		{
			dest[0] = block >> 16;
			if (src[-2] == BASE64_PADDING) return length - 2;
			dest[1] = (block >> 8) & 0xff;
			if (src[-1] == BASE64_PADDING) return length - 1;
			dest[2] = block & 0xff;
			if (src == end) return length;

			dest += 3;
			block = 0;
			phase = 0;
		}
	}
}

/*#include <unistd.h>
int main(void)
{
	static char buff[4096];
	write(1, buff, format(buff, uint(15), int(-43, 16, 4), uint(1337, 10, 16, '0'), str(" <\n", 3), bin("\x7f\x2c", 2), str("\n", 1)) - buff);
	//write(1, buff, format(buff, uint(15), int(-43, 16, 4), int(6), uint(1337, 10, 32, '0'), int(-20)) - buff);
}*/
