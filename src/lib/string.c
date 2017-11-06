#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#if defined(OS_WINDOWS)
# include <sys/types.h>
#endif

#include "types.h"

// TODO kmp_search() can be optimized with strcmp() if it returns the position of the first mismatch; think about this

#if defined(OS_WINDOWS)
size_t __int64_length(__int64 number, unsigned base)
{
	size_t length = 1;
	while (number /= base) ++length;
	return length;
}
#endif

// WARNING: This function works properly only for positive integers and 0
// TODO deprecated; use format_uint_length instead
size_t integer_digits(intmax_t number, unsigned base)
{
	size_t length = 1;
	while (number /= base) ++length;
	return length;
}

// TODO: implement this
//size_t integer_digits_hex()

int string_diff(const struct string *s0, const struct string *s1)
{
	if (s0->length == s1->length) return memcmp(s0->data, s1->data, s0->length);
	else
	{
		int diff;
		if (s0->length < s1->length)
		{
			diff = memcmp(s0->data, s1->data, s0->length);
			return (diff ? diff : -s1->data[s0->length]);
		}
		else
		{
			diff = memcmp(s0->data, s1->data, s1->length);
			return (diff ? diff : s0->data[s1->length]);
		}
	}
}

struct string *string_alloc(const char *data, size_t length)
{
	struct string *result = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!result) return 0;
	result->data = (char *)(result + 1);
	result->length = length;
	if (data) memcpy(result->data, data, length);
	result->data[length] = 0;
	return result;
}

/*
char *str_dup(const char *data, size_t length)
{
	char *result = malloc(sizeof(char) * (length + 1));
	if (!result) return 0;
	memcpy(result, data, length);
	result[length] = 0;
	return result;
}

bool string_init(struct string *restrict string, const char *data, size_t length)
{
	string->data = str_dup(data, length);
	if (!string->data) return false;
	string->length = length;
	return true;
}
*/

struct string *string_init(struct string *restrict s, const char *data, size_t length)
{
	s->data = malloc(sizeof(char) * (length + 1));
	if (!s->data) return 0;
	memcpy(s->data, data, length);
	s->data[length] = 0;
	s->length = length;
	return s;
}

struct string *(string_concat)(const struct string *start, ...)
{
	size_t length;
	va_list strings;
	struct string *item, *result;

	// Calculate string length
	length = start->length;
	va_start(strings, start);
	while (item = va_arg(strings, struct string *))
		length += item->length;
	va_end(strings);

	// Allocate memory for the string
	result = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!result) return 0;
	result->data = (char *)(result + 1);
	result->length = length;

	// Copy the source strings
	memcpy(result->data, start->data, start->length);
	length = start->length;
	va_start(strings, start);
	while (item = va_arg(strings, struct string *))
	{
		memcpy(result->data + length, item->data, item->length);
		length += item->length;
	}
	va_end(strings);

	result->data[length] = 0;
	return result;
}

struct string *string_concat_alloc(const char *data_begin, size_t length_begin, const char *data_end, size_t length_end) //concatanate 2 strings and returns new string
{
	struct string *result = malloc(sizeof(struct string) + (sizeof(char) * (length_begin)) + (sizeof(char) * (length_end)) + sizeof(char));
	if (!result) return 0;
	result->data = (char *)(result + 1);
	result->length = length_begin+length_end;
	memcpy(result->data, data_begin, length_begin);
	memcpy(result->data+length_begin, data_end, length_end);
	result->data[length_begin+length_end] = 0;
	// TODO: free memory?
	return result;
}

// Returns the position of the Most Significant Bit set in a byte
static inline unsigned char msb_pos(unsigned char byte)
{
    static const unsigned char mask[] = {0x1,  0x3, 0xf};
    unsigned char r, s;
    // Round 0
    r = ((mask[2] & byte) < byte) << 2; // No need to use s here as r is initially zero
    byte >>= r;
    // Round 1
    s = ((mask[1] & byte) < byte) << 1;
    byte >>= s;
    r += s;
    // Round 2
    return r + ((mask[0] & byte) < byte); // Just return the position
}

static size_t utf8_read(unsigned *restrict result, const unsigned char *start, size_t length)
{
	size_t i = 0;

	unsigned char msb = msb_pos(~*start);
	size_t bytes = 7 - msb;
	if (!bytes) bytes = 1;
	if (bytes > length) return 0;

	if (result)
	{
		unsigned character = (start[i++] & ((1 << msb) - 1)) << ((bytes - 1) * 6);
		while (--bytes)
			character += (start[i++] & 0x3f) << ((bytes - 1) * 6);

		*result = character;
		return i;
	}
	else return bytes;
}

struct string *restrict string_serialize(unsigned char *restrict data, int length)
{
	size_t i = 0;
	size_t size = 0;

	while (i < length)
	{
		if ((0x1f < data[i]) && (data[i] < 0x7f)) // If this is a valid character in ASCII
		{
			if ((data[i] == '"') || (data[i] == '\\'))
				size += 2;
			else
				size += 1;
		}
		else if ((data[i] == '\t') || (data[i] == '\n')) size += 2;
		else if (data[i] <= 0x1f) size += 6;
		else
		{
			// Advance the position properly for multi-byte characters
			size_t bytes = utf8_read(0, data + i, length - i);
			size += bytes;
			i += bytes;
			continue;
		}
		++i;
	}

	struct string *serialized = malloc(sizeof(struct string) + sizeof(char) * (size + 1));
	if (!serialized) return 0;
	serialized->length = size;
	serialized->data = (char *)(serialized + 1);

	char *result = serialized->data;
	i = 0;
	while (i < length)
	{
		// Check for \t and \n to optimize size as they are commonly encountered
		if ((0x1f < data[i]) && (data[i] < 0x7f)) // If this is a valid character in ASCII
		{
			if ((data[i] == '"') || (data[i] == '\\'))
				*result++ = '\\';
			*result++ = data[i];
		}
		else if (data[i] == '\t')
		{
			*result++ = '\\';
			*result++ = 't';
		}
		else if (data[i] == '\n')
		{
			*result++ = '\\';
			*result++ = 'n';
		}
		else if (data[i] <= 0x1f)
		{
			static const char hex_digits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
			unsigned character;
			i += utf8_read(&character, data + i, length - i);

			// Write the encoded character
			result[0] = '\\';
			result[1] = 'u';
			result[2] = hex_digits[(character >> 12) & 0xf];
			result[3] = hex_digits[(character >> 8) & 0xf];
			result[4] = hex_digits[(character >> 4) & 0xf];
			result[5] = hex_digits[character & 0xf];
			result += 6;

			continue;
		}
		else
		{
			size_t bytes = utf8_read(0, data + i, length - i);
			while (bytes--)
				*result++ = data[i++];

			continue;
		}
		++i;
	}
	*result++ = 0;

	return serialized;
}

void *memdup(const void *restrict data, size_t size)
{
	void *result = malloc(size);
	if (!result) return 0;
	memcpy(result, data, size);
	return result;
}

// Generates table for kmp_search. pattern must be non-empty. The returned pointer must be freed with free().
size_t *restrict kmp_table(const struct string *pattern)
{
	size_t start = 0, pos = 2;

	// Initialize partial match table for the pattern
	size_t *restrict table = malloc(sizeof(size_t) * pattern->length);
	if (!table) return 0;

	// table[0] is not used
	if (pattern->length > 1) table[1] = 0;
	while (pos < pattern->length)
	{
		if (pattern->data[pos - 1] == pattern->data[start]) table[pos++] = ++start;
		else if (start) start = table[start];
		else table[pos++] = 0;
	}

	return table;
}

// Finds pattern in search using table generated by kmp_table. The string pattern must be non-empty. Returns ERROR_MISSING if the pattern is not found.
ssize_t kmp_search(const struct string *pattern, const size_t *table, const struct string *search)
{
	size_t start = 0, pos = 0;

	// TODO using tolower(search->data[start + pos]) and ensuring pattern is lower-case will make the search case-insensitive. is this necessary?

	// Cancel when the pattern can't fit in what's left of the string.
	while ((start + pattern->length) <= search->length)
	{
		if (search->data[start + pos] == pattern->data[pos])
		{
			// This is a match. Advance the position in the pattern.
			if (++pos == pattern->length) return start; // found
			continue;
		}
		else if (pos)
		{
			// Return the position in the pattern to the nearest possible matching position.
			start += pos - table[pos];
			pos = table[pos];
		}
		else ++start; // This symbol doesn't match so go to the next one
	}

	return ERROR_MISSING;
}
