#include <stdbool.h>
#include <stdlib.h>

#include "buffer.h"

// WARNING: These must be powers of 2.
#define BUFFER_MIN 1024
#define BUFFER_MAX 65536

bool buffer_adjust(struct buffer *restrict buffer, size_t size)
{
	// Check buffer size and adjust it if necessary.
	// BUFFER_MIN <= size <= BUFFER_MAX
	// size % BUFFER_MIN == 0
	if (size > BUFFER_MAX) return false;
	if (size < BUFFER_MIN) size = BUFFER_MIN;
	else size = (size + BUFFER_MIN - 1) & ~(BUFFER_MIN - 1);

	if (buffer->_size < size)
	{
		char *new = realloc(buffer->data, size);
		if (!new) return false;
		buffer->data = new;
		buffer->_size = size;
	}
	return true;
}
